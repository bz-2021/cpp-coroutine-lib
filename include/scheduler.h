#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "coroutine.h"
#include "thread.h"
#include <mutex>
#include <vector>

namespace coroutine {

class Scheduler {
public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();

    const std::string& getName() const { return m_name; }

    static Scheduler* GetThis();

protected:
    void SetThis();

public:
    template <class TaskOrCb>
    void scheduleLock(TaskOrCb tc, int thread = -1) {
        bool need_tickle;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            need_tickle = m_tasks.empty();
            
            ScheduleTask task(tc, thread);
            if (task.coroutine || task.cb) {
                m_tasks.push_back(task);
            }
        }

        if (need_tickle) {
            tickle();
        }
    }

    virtual void start();
    virtual void stop();

protected:
    virtual void tickle();
    virtual void run();
    virtual void idle();
    virtual bool stopping();
    bool hasIdelThreads() { return m_idleThreadCount; }

private:
    // 任务
    struct ScheduleTask {
        std::shared_ptr<Coroutine> coroutine;
        std::function<void()> cb;
        int thread;

        ScheduleTask(): coroutine(nullptr), cb(nullptr), thread(-1) {}
        ScheduleTask(std::shared_ptr<Coroutine> t, int _thread): coroutine(t), thread(_thread) {}
        ScheduleTask(std::function<void()> t, int _thread): cb(t), thread(_thread) {}
        
        ScheduleTask(std::shared_ptr<Coroutine>* t, int _thread) {
            coroutine.swap(*t);
            thread = _thread;
        }
        ScheduleTask(std::function<void()>* f, int _thread) {
            cb.swap(*f);
            thread = _thread;
        }

        void reset() {
            coroutine = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };

    std::string m_name;
    std::mutex m_mutex;
    // 线程池
    std::vector<std::shared_ptr<Thread>> m_threads;
    // 任务队列
    std::vector<ScheduleTask> m_tasks;
    // 工作线程的线程ID
    std::vector<int> m_threadIds;
    // 需额外创建的线程数
    size_t m_threadCount = 0;
    // 活跃线程数
    std::atomic<size_t> m_activeThreadCount= {0};
    // 空闲线程数
    std::atomic<size_t> m_idleThreadCount = {0};

    // 主线程是否用作工作线程
    bool m_useCaller;
    // 是？额外调度线程
    std::shared_ptr<Coroutine> m_schedulerTask;
    // 是？主线程 ID
    int m_rootThread = -1;
    // 是否正在关闭
    bool m_stopping = false;
};
}
#endif