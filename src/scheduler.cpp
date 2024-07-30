#include "scheduler.h"

static bool debug = false;

namespace coroutine {

static thread_local Scheduler* t_scheduler = nullptr;

Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

void Scheduler::SetThis() {
    t_scheduler = this;
}

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name):
m_useCaller(use_caller), m_name(name) {
    assert(threads > 0 && Scheduler::GetThis() == nullptr);

    SetThis();
    Thread::SetName(m_name);

    if (use_caller) {
        threads--;
        // 创建主线程
        Coroutine::GetThis();
        m_schedulerTask.reset(new Coroutine(std::bind(&Scheduler::run, this), 0, false));
        Coroutine::SetSchedulerCoroutine(m_schedulerTask.get());

        m_rootThread = Thread::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    }
    m_threadCount = threads;
    if (debug) std::cout << "Scheduler::Scheduler() success" << std::endl;
}

Scheduler::~Scheduler() {
    assert(stopping() == true);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
    if (debug) std::cout << "Scheduler::~Scheduler() success" << std::endl;
}

void Scheduler::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping) {
        std::cerr << "Scheduler is stopping" << std::endl;
        return;
    }

    assert(m_threads.empty());
    m_threads.resize(m_threadCount);
	for(size_t i = 0; i < m_threadCount; i++)
	{
		m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
		m_threadIds.push_back(m_threads[i]->getId());
	}
	if(debug) std::cout << "Scheduler::start() success\n";
}

void Scheduler::run() {
    int thread_id = Thread::GetThreadId();
    if (debug) std::cout << "Schedule::run() starts in thread: " << thread_id << std::endl;

    SetThis();

    // 创建主协程
    if (thread_id != m_rootThread) {
        Coroutine::GetThis();
    }

    std::shared_ptr<Coroutine> idle_task = std::make_shared<Coroutine>(std::bind(&Scheduler::idle, this));
    ScheduleTask task;

    while (true) {
        task.reset();
        bool tickle_me = false; // 是否要 tickle 其他线程
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历任务队列
            while (it != m_tasks.end()) {
                if (it->thread != -1 && it->thread != thread_id) {
                    // 指定了调度线程，但是不在当前线程上调度，标记下需要通知其他线程进行调度，然后跳过
                    it++;
                    tickle_me = true;
                    continue;
                }
                // 一个未指定线程，或是指定了当前线程的任务
                assert(it->coroutine || it->cb);
                // 准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it);
                m_activeThreadCount++;
                break;
            }
            // 任务队列还有剩余，tickle 以下其他线程
            tickle_me = tickle_me || (it != m_tasks.end());
        }

        if (tickle_me) {
            tickle();
        }

        if (task.coroutine) {
			{					
				std::lock_guard<std::mutex> lock(task.coroutine->m_mtx);
				// resume 协程，完成后活跃线程数减1
                if(task.coroutine->getState()!=Coroutine::TERM) {
					task.coroutine->resume();	
				}
			}
			m_activeThreadCount--;
			task.reset();

        } else if(task.cb) {
			std::shared_ptr<Coroutine> cb_fiber = std::make_shared<Coroutine>(task.cb);
			{
				std::lock_guard<std::mutex> lock(cb_fiber->m_mtx);
				cb_fiber->resume();
			}
			m_activeThreadCount--;
			task.reset();

		} else {
            // 任务队列空了，调度 idle 协程
            if (idle_task->getState() == Coroutine::TERM) {
            	if(debug) std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                break;
            }
			m_idleThreadCount++;
			idle_task->resume();			
			m_idleThreadCount--;
		}
    }
}

void Scheduler::stop() {
	if(debug) std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;
	
	if (stopping()) {
		return;
	}

	m_stopping = true;	

    if (m_useCaller) {
        assert(GetThis() == this);
    } else {
        assert(GetThis() != this);
    }
	
	for (size_t i = 0; i < m_threadCount; i++) {
		tickle();
	}

	if (m_schedulerTask) {
		tickle();
	}

	if (m_schedulerTask) {
		m_schedulerTask->resume();
		if(debug) std::cout << "m_schedulerFiber ends in thread:" << Thread::GetThreadId() << std::endl;
	}

	std::vector<std::shared_ptr<Thread>> thrs;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		thrs.swap(m_threads);
	}

	for(auto &i : thrs) {
		i->join();
	}
	if (debug) std::cout << "Schedule::stop() ends in thread:" << Thread::GetThreadId() << std::endl;
}

void Scheduler::tickle() {}

void Scheduler::idle() {
	while(!stopping()) {
		if(debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;	
		sleep(1);	
		Coroutine::GetThis()->yield();
	}
}

bool Scheduler::stopping() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

}