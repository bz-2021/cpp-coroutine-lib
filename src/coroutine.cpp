#include "coroutine.h"

static bool debug = false;

namespace coroutine {
// 当前线程上的协程控制信息

// 正在运行的协程
static thread_local Coroutine* t_cur_coroutine = nullptr;
// 主协程
static thread_local std::shared_ptr<Coroutine> t_main_coroutine = nullptr;
// 调度协程
static thread_local Coroutine* t_scheduler_coroutine = nullptr;
// 协程计数器
static std::atomic<uint64_t> s_coroutine_count{0};
// 协程 ID
static std::atomic<uint64_t> s_coroutine_id{0};

void Coroutine::SetThis(Coroutine* t) {
    t_cur_coroutine = t;
}

std::shared_ptr<Coroutine> Coroutine::GetThis() {
    if (t_cur_coroutine) {
        return t_cur_coroutine->shared_from_this();
    }
    std::shared_ptr<Coroutine> main_task(new Coroutine());
    t_main_coroutine = main_task;
    t_scheduler_coroutine = main_task.get(); // 调度协程默认为主线程

    assert(t_cur_coroutine == main_task.get());
    return t_cur_coroutine->shared_from_this();
}

void Coroutine::SetSchedulerCoroutine(Coroutine* t) {
    t_scheduler_coroutine = t;
}

uint64_t Coroutine::GetCoroutineId() {
    if (t_cur_coroutine) {
        return t_cur_coroutine->getId();
    }
    return (uint64_t)-1;
}

Coroutine::Coroutine() {
    SetThis(this);
    m_state = RUNNING;

    if (getcontext(&m_ctx)) {
        std::cerr << "Coroutine() failed" << std::endl;
        pthread_exit(NULL);
    }

    m_id = s_coroutine_id++;
    s_coroutine_count++;
    if (debug) std::cout << "Coroutine(): main id = " << m_id << std::endl;
}

Coroutine::Coroutine(std::function<void()> cb, size_t stack_size, bool run_in_scheduler):
m_cb(cb), m_run_in_scheduler(run_in_scheduler) {
    m_state = READY;

    m_stack_size = stack_size ? stack_size : 128000;
    m_stack = malloc(m_stack_size);

    if (getcontext(&m_ctx)) {
        std::cerr << "Coroutine(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed" << std::endl;
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    makecontext(&m_ctx, &Coroutine::MainFunc, 0);

    m_id = s_coroutine_id++;
    s_coroutine_count++;
    if (debug) std::cout << "Coroutine(): child id = " << m_id << std::endl;
}

Coroutine::~Coroutine() {
    s_coroutine_count--;
    if (m_stack) {
        free(m_stack);
    }
    if(debug) std::cout << "~Coroutine(): id = " << m_id << std::endl;
}

void Coroutine::reset(std::function<void()> cb) {
    assert(m_stack != nullptr && m_state == TERM);

    m_state = READY;
    m_cb = cb;

    if (getcontext(&m_ctx)) {
        std::cerr << "reset() failed" << std::endl;
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
	m_ctx.uc_stack.ss_sp = m_stack;
	m_ctx.uc_stack.ss_size = m_stack_size;
	makecontext(&m_ctx, &Coroutine::MainFunc, 0);
}

void Coroutine::resume() {
    assert(m_state == READY);
    
    m_state = RUNNING;

    // 为 true 表示该协程参与调度器调度，应该和调度协程进行上下文切换
    if (m_run_in_scheduler) {
        SetThis(this);
        if (swapcontext(&(t_scheduler_coroutine->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_scheduler_coroutine failed" << std::endl;
			pthread_exit(NULL); 
        }
    } else { // 不参与调度器调度，直接和主线程切换
        SetThis(this);
        if (swapcontext(&(t_main_coroutine->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_thread_coroutine failed" << std::endl;
			pthread_exit(NULL);
        }
    }   
}

void Coroutine::yield() {
    assert(m_state == RUNNING || m_state == TERM);

    if (m_state != TERM) {
        m_state = READY;
    }

    // 执行权给到调度协程
    if (m_run_in_scheduler) {
        SetThis(t_scheduler_coroutine);
        if (swapcontext(&m_ctx, &(t_scheduler_coroutine->m_ctx))) {
            std::cerr << "yield() to t_scheduler_coroutine failed\n";
			pthread_exit(NULL);
        }
    } else {
        SetThis(t_main_coroutine.get());
        if (swapcontext(&m_ctx, &(t_cur_coroutine->m_ctx))) {
            std::cerr << "yield() to t_thread_coroutine failed\n";
			pthread_exit(NULL);
        }
    }
}

void Coroutine::MainFunc() {
    std::shared_ptr<Coroutine> curr = GetThis();
    assert(curr != nullptr);

	curr->m_cb(); // 协程的入口函数
	curr->m_cb = nullptr;
	curr->m_state = TERM;

	// 运行完毕 -> 让出执行权
	auto raw_ptr = curr.get();
	curr.reset(); 
	raw_ptr->yield(); 
}
}