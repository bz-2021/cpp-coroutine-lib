#ifndef _IOMANAGER_H_
#define _IOMANAGER_H_

#include "scheduler.h"
#include "timer.h"

namespace coroutine {

/**
 * @brief IO协程调度支持为描述符注册可读和可写事件的回调函数，
 * 当描述符可读或可写时，执行对应的回调函数。支持取消事件。
 * @details 使用一对管道来tickle调度协程，当调度器空闲时，
 * idle协程通过epoll_wait阻塞在管道的读描述符上，等管道的可读事件。
 * 添加新任务时，tickle方法写管道，idle协程退出，调度器执行调度。
 */
class IOManager : public Scheduler, public TimerManager {
public:
    /**
     * @brief IO 事件，只关心 socket 的读和写事件
     */
    enum Event {
        NONE = 0x0,
        // READ == EPOLLIN
        READ = 0x1,
        // WRITE == EPOLLOUT
        WRITE = 0x4
    };

private:
    /**
     * @brief 描述符-事件类型-回调函数
     */
    struct FdContext {
        /**
         * @brief 读写事件上下文
         */
        struct EventContext 
        {
            // 执行事件回调的调度器
            Scheduler *scheduler = nullptr;
            // 事件回调协程
            std::shared_ptr<Coroutine> coroutine;
            // 回调函数
            std::function<void()> cb;
        };
        // 读事件上下文
        EventContext read;
        // 写事件上下文 
        EventContext write;
        // 文件描述符
        int fd = 0;
        // 事件类型
        Event events = NONE;

        std::mutex mutex;

        /**
         * @brief 获取事件上下文
         */
        EventContext& getEventContext(Event event);

        /**
         * @brief 重置事件上下文
         */
        void resetEventContext(EventContext &ctx);

        /**
         * @brief 触发事件
         * @details 根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
         */
        void triggerEvent(Event event);        
    };

public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");
    ~IOManager();

    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    bool delEvent(int fd, Event event);

    bool cancelEvent(int fd, Event event);

    bool cancelAll(int fd);

    static IOManager* GetThis();

protected:
    void tickle() override;
    
    bool stopping() override;
    /**
     * @details IO协程调度器在idle时会epoll_wait所有注册的fd，
     * 如果有fd满足条件，epoll_wait返回，从私有数据中拿到fd的上下文信息，
     * 并且执行其中的回调函数。
     */
    void idle() override;

    void onTimerInsertedAtFront() override;

    void contextResize(size_t size);

private:
    // epoll 文件句柄
    int m_epfd = 0;
    // 用于 tickle 的管道
    int m_tickleFds[2];
    // 当前等待执行的 IO 事件数量
    std::atomic<size_t> m_pendingEventCount = {0};
    // IOManager 的互斥锁
    std::shared_mutex m_mutex;
    // Socket 事件上下文的容器
    std::vector<FdContext*> m_fdContexts;
};

}

#endif