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

class Coroutine : public std::enable_shared_from_this<Coroutine> {
public:
    enum State {
        READY,
        RUNNING,
        TERM
    };

    /**
     * @brief 构造函数，用于创建用户协程
     * @details 初始化子协程的 ucontext_t 上下⽂和栈空间，
     * 要求必须传入协程的入口函数，以及可选的协程栈大小
     */
    Coroutine(std::function<void()> cb, size_t staskSize = 0, bool run_in_scheduler = true);
    
    ~Coroutine();

    /**
     * @brief 重置协程状态和入口函数，复用独立栈空间
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 协程切换到执行状态
     * @details 执行 swapcontext，前者变为 Ready，后者变为 Running
     */
    void resume();
    
    /**
     * @brief 当前协程让出执行权
     */
    void yield();

    /**
     * @brief 获取协程 ID
     */
    uint64_t getId() const { return m_id; }

    /**
     * @brief 获取协程状态
     */
    State getState() const { return m_state; }

    /**
     * @brief 设置正在运行的协程
     */
    static void SetThis(Coroutine* t);
    
    /**
     * @brief 返回正在执行的协程
     * @details 如果当前未创建协程，则创建第一个协程（主协程）
     * 其他协程 yield 后都要切回主协程重新选择新协程进行 resume
     * @attention 使用协程前必须显式调用一次 GetThis()
     */
    static std::shared_ptr<Coroutine> GetThis();
    
    /**
     * @brief 设置调度协程
     */
    static void SetSchedulerCoroutine(Coroutine* t);

    /**
     * @brief 获取当前运行的协程 ID
     */
    static uint64_t GetCoroutineId();
    
    /**
     * @brief 协程入口函数
     */
    static void MainFunc();

private:
    /**
     * @brief 无参构造函数
     * @attention 只用于创建线程的第一个协程，即线程主函数对应的协程，只在GetThis中调用
     */
    Coroutine();

    /// ID
    uint64_t m_id = 0;
    /// 栈大小
    uint32_t m_stack_size = 0;
    /// 协程状态
    State m_state = READY;
    /// 协程上下文
    ucontext_t m_ctx;
    /// 协程栈指针
    void* m_stack = nullptr;
    /// 协程函数
    std::function<void()> m_cb;
    /// 是否让出执行权交给调度协程
    bool m_run_in_scheduler;

public:
    std::mutex m_mtx;
};
}

#endif