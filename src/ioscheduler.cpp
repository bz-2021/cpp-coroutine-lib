#include <unistd.h>    
#include <sys/epoll.h> 
#include <fcntl.h>     
#include <cstring>

#include "ioscheduler.h"

static bool debug = true;

namespace coroutine {

IOManager* IOManager::GetThis() 
{
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event) 
{
    assert(event==READ || event==WRITE);    
    switch (event) 
    {
    case READ:
        return read;
    case WRITE:
        return write;
    }
    throw std::invalid_argument("Unsupported event type");
}

void IOManager::FdContext::resetEventContext(EventContext &ctx) 
{
    ctx.scheduler = nullptr;
    ctx.coroutine.reset();
    ctx.cb = nullptr;
}

// no lock
void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    assert(events & event);

    // delete event 
    events = (Event)(events & ~event);
    
    // trigger
    EventContext& ctx = getEventContext(event);
    if (ctx.cb) 
    {
        // call ScheduleTask(std::function<void()>* f, int thr)
        ctx.scheduler->scheduleLock(&ctx.cb);
    } 
    else 
    {
        // call ScheduleTask(std::shared_ptr<coroutine>* f, int thr)
        ctx.scheduler->scheduleLock(&ctx.coroutine);
    }

    // reset event context
    resetEventContext(ctx);
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name): 
Scheduler(threads, use_caller, name), TimerManager()
{
    // 创建 epoll 实例
    m_epfd = epoll_create(5000);
    assert(m_epfd > 0);

    // 创建管道
    int rt = pipe(m_tickleFds);
    assert(!rt);

    // 注册管道读句柄的可读事件，用于tickle调度线程
    epoll_event event;
    event.events  = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    // 设置非阻塞的方式，配合边缘触发
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    assert(!rt);

    // 将管道读描述符加入epoll多路复用，如果管道可读，idle中的epoll_wait会返回
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    assert(!rt);

    contextResize(32);

    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (m_fdContexts[i]) 
        {
            delete m_fdContexts[i];
        }
    }
}

// no lock
void IOManager::contextResize(size_t size) 
{
    m_fdContexts.resize(size);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (m_fdContexts[i]==nullptr) 
        {
            m_fdContexts[i] = new FdContext();
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) 
{
    // attemp to find FdContext 
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);
    
    // the event has already been added
    if(fd_ctx->events & event) 
    {
        return -1;
    }

    // add new event
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    ++m_pendingEventCount;

    // update fdcontext
    fd_ctx->events = (Event)(fd_ctx->events | event);

    // update event context
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.coroutine && !event_ctx.cb);
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) 
    {
        event_ctx.cb.swap(cb);
    } 
    else 
    {
        event_ctx.coroutine = Coroutine::GetThis();
        assert(event_ctx.coroutine->getState() == Coroutine::RUNNING);
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    // attemp to find FdContext 
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    // the event doesn't exist
    if (!(fd_ctx->events & event)) 
    {
        return false;
    }

    // delete the event
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }


    --m_pendingEventCount;

    // update fdcontext
    fd_ctx->events = new_events;

    // update event context
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    // attemp to find FdContext 
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    // the event doesn't exist
    if (!(fd_ctx->events & event)) 
    {
        return false;
    }

    // delete the event
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "cancelEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    --m_pendingEventCount;

    // update fdcontext, event context and trigger
    fd_ctx->triggerEvent(event);    
    return true;
}

bool IOManager::cancelAll(int fd) {
    // attemp to find FdContext 
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);
    
    // none of events exist
    if (!fd_ctx->events) 
    {
        return false;
    }

    // delete all events
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "IOManager::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    // update fdcontext, event context and trigger
    if (fd_ctx->events & READ) 
    {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }

    if (fd_ctx->events & WRITE) 
    {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    assert(fd_ctx->events == 0);
    return true;
}

/**
 * @brief 通知调度器有任务要调度
 * @details 写管道让idle协程从epoll_wait退出，
 * 待idle协程yield之后Scheduler::run就可以调度其他任务
 */
void IOManager::tickle() 
{
    // 如果当前没有空闲调度线程，那就没必要发通知
    if(!hasIdelThreads()) 
    {
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);
    assert(rt == 1);
}

bool IOManager::stopping() 
{
    uint64_t timeout = getNextTimer();
    // no timers left and no pending events left with the Scheduler::stopping()
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

/**
 * @brief Idle 协程所执行的函数
 * @details 对于IO协程调度来说，应阻塞在等待IO事件上，idle退出的时机是epoll_wait返回，对应的操作是tickle或注册的IO事件就绪
 * 调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() 
{    
    static const uint64_t MAX_EVNETS = 256;
    std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVNETS]);

    while (true) 
    {
        if(debug) std::cout << "IOManager::idle(),run in thread: " << Thread::GetThreadId() << std::endl; 

        if(stopping()) 
        {
            if(debug) std::cout << "name = " << getName() << " idle exits in thread: " << Thread::GetThreadId() << std::endl;
            break;
        }

        int rt = 0;
        while(true)
        {
            static const uint64_t MAX_TIMEOUT = 5000;
            uint64_t next_timeout = getNextTimer();
            next_timeout = std::min(next_timeout, MAX_TIMEOUT);

            // 阻塞在epoll_wait上，等待事件发生
            rt = epoll_wait(m_epfd, events.get(), MAX_EVNETS, (int)next_timeout);

            if(rt < 0 && errno == EINTR) 
            {
                continue;
            } 
            else 
            {
                break;
            }
        };

        // collect all timers overdue
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) 
        {
            for(const auto& cb : cbs) 
            {
                scheduleLock(cb);
            }
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for (int i = 0; i < rt; ++i) 
        {
            epoll_event& event = events[i];
            // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可，
            // 本轮idle结束Scheduler::run会重新执行协程调度
            if (event.data.fd == m_tickleFds[0]) 
            {
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            // 通过epoll_event的私有指针获取FdContext
            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            std::lock_guard<std::mutex> lock(fd_ctx->mutex);

            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             */
            if (event.events & (EPOLLERR | EPOLLHUP)) 
            {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            // events happening during this turn of epoll_wait
            int real_events = NONE;
            if (event.events & EPOLLIN) 
            {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) 
            {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) 
            {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait，
            // 如果剩下的事件为0，表示这个fd已经不需要关注了，直接从epoll中删除
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) 
            {
                std::cerr << "idle::epoll_ctl failed: " << strerror(errno) << std::endl; 
                continue;
            }

            // 处理已经发生的事件，也就是让调度器调度指定的函数或协程
            if (real_events & READ) 
            {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) 
            {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        Coroutine::GetThis()->yield();
  
    } // end while(true)
}

void IOManager::onTimerInsertedAtFront() 
{
    tickle();
}

} // end namespace coroutine