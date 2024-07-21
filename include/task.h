#ifndef _TASK_H_
#define _TASK_H_

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <mutex>
#include <ucontext.h>
#include <unistd.h>

namespace coroutine {

class Task : public std::enable_shared_from_this<Task> {
public:
    enum State {
        READY,
        RUNNING,
        TERM
    };

    Task(std::function<void()> cb, size_t staskSize = 0, bool run_in_scheduler = true);
    ~Task();

    // 重用一个协程
    void reset(std::function<void()> cb);
    // 任务线程恢复执行
    void resume();
    // 任务线程让出执行权
    void yield();

    uint64_t getId() const { return m_id; }
    State getState() const { return m_state; }

    // 设置当前运行的协程
    static void SetThis(Task* t);
    // 获取当先运行的协程
    static std::shared_ptr<Task> GetThis();
    // 设置调度协程
    static void SetSchedulerTask(Task* t);
    // 获取当前运行的协程 ID
    static uint64_t GetTaskId();
    // 协程函数
    static void MainFunc();

private:
    Task();

    // ID
    uint64_t m_id = 0;
    // 栈大小
    uint32_t m_stack_size = 0;
    // 协程状态
    State m_state = READY;
    // 协程上下文
    ucontext_t m_ctx;
    // 协程栈指针
    void* m_stack = nullptr;
    // 协程函数
    std::function<void()> m_cb;
    // 是否让出执行权交给调度协程
    bool m_run_in_scheduler;

public:
    std::mutex m_mtx;
};
}

#endif