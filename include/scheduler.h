#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "coroutine.h"
#include "thread.h"
#include <mutex>
#include <vector>

namespace coroutine {

/**
 * @brief N-M 的协程调度器
 * @details 内部维护一个任务队列和一个调度线程池，线程池从任务队列里按顺序取任务执行。
 * 调度方式：先来先服务、最短作业优先、最高相应比优先、时间片轮转等。当前调度器使用的先来先服务算法。
 * 调度器支持将协程或者一个函数作为调度任务。
 */
class Scheduler {
public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();

    const std::string& getName() const { return m_name; }

    static Scheduler* GetThis();

protected:
    void SetThis();

public:
    /**
     * @brief 添加调度任务
     * @tparam TaskOrCb 协程对象或者函数指针
     */
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
            tickle(); // 唤醒 idle 函数
        }
    }

    /**
     * @brief 启动调度器
     */
    virtual void start();

    /**
     * @brief 停止调度器，等所有任务完成后返回
     */
    virtual void stop();

protected:
    /**
     * @brief 有新任务，通知协程调度器
     */
    virtual void tickle();

    /**
     * @brief 协程调度函数
     */
    virtual void run();

    /**
     * @brief 无任务调度时执行 idle
     */
    virtual void idle();

    /**
     * @brief 返回是否可以停止
     */
    virtual bool stopping();
    
    /**
     * @brief 返回是否有空闲线程
     * @details 当调度协程进入 idle 时空闲线程数+1，从 idle 协程返回时空闲线程数-1
     */
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

    // 协程调度器名称
    std::string m_name;
    // 互斥锁
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

    /**
     * @brief 主线程（调度器线程）是否用作工作线程
     * @details 当主线程不参与调度时，即 use_caller 为 false，就需要创建其他线程进行协程调度
     * 此时只需要让新线程的入口函数作为调度协程，main 函数只需要向调度器添加任务，在适当时机停止调度器即可
     * 调度器停止时，main 函数要等待调度线程结束后再退出。
     * 当 use_caller 为 true 时，main 函数主协程运行，向调度器中添加调度任务
     * 开始调度时，main 函数让出执行权，切换到调度协程，调度协程从任务队列中按顺序执行任务
     * 每次执行一个任务，调度协程都要让出执行权，切换到该任务的协程中，结束后切回调度协程，执行下一个任务
     * 所有任务完成后，调度协程切换回主协程，以保证程序顺利结束。
     */
    bool m_useCaller;
    
    // m_useCaller 为 true 时，额外调度线程
    std::shared_ptr<Coroutine> m_schedulerTask;
    // m_useCaller 为 true 时，主线程 ID
    int m_rootThread = -1;
    // 是否正在关闭
    bool m_stopping = false;
};
}
#endif