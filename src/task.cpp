#include "task.h"

static bool debug = false;

namespace coroutine {
// 当前线程上的协程控制信息

// 正在运行的协程
static thread_local Task* t_task = nullptr;
// 主协程
static thread_local std::shared_ptr<Task> t_thread_task = nullptr;
// 调度协程
static thread_local Task* t_scheduler_task = nullptr;
// 协程计数器
static std::atomic<uint64_t> s_task_count{0};
// 协程 ID
static std::atomic<uint64_t> s_task_id{0};

void Task::SetThis(Task* t) {
    t_task = t;
}

// 创建主协程
std::shared_ptr<Task> Task::GetThis() {
    if (t_task) {
        return t_task->shared_from_this();
    }
    std::shared_ptr<Task> main_task(new Task());
    t_thread_task = main_task;
    t_scheduler_task = main_task.get(); // 调度协程默认为主线程

    assert(t_task == main_task.get());
    return t_task->shared_from_this();
}

void Task::SetSchedulerTask(Task* t) {
    t_scheduler_task = t;
}

uint64_t Task::GetTaskId() {
    if (t_task) {
        return t_task->getId();
    }
    return (uint64_t)-1;
}

Task::Task() {
    SetThis(this);
    m_state = RUNNING;

    if (getcontext(&m_ctx)) {
        std::cerr << "Task() failed" << std::endl;
        pthread_exit(NULL);
    }

    m_id = s_task_id++;
    s_task_count++;
    if (debug) std::cout << "Task(): main id = " << m_id << std::endl;
}

Task::Task(std::function<void()> cb, size_t stack_size, bool run_in_scheduler):
m_cb(cb), m_run_in_scheduler(run_in_scheduler) {
    m_state = READY;

    m_stack_size = stack_size ? stack_size : 128000;
    m_stack = malloc(m_stack_size);

    if (getcontext(&m_ctx)) {
        std::cerr << "Task(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed" << std::endl;
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    makecontext(&m_ctx, &Task::MainFunc, 0);

    m_id = s_task_id++;
    s_task_count++;
    if (debug) std::cout << "Task(): child id = " << m_id << std::endl;
}

Task::~Task() {
    s_task_count--;
    if (m_stack) {
        free(m_stack);
    }
    if(debug) std::cout << "~Task(): id = " << m_id << std::endl;
}

// 重用一个协程
void Task::reset(std::function<void()> cb) {
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
	makecontext(&m_ctx, &Task::MainFunc, 0);
}

void Task::resume() {
    assert(m_state == READY);
    
    m_state = RUNNING;

    if (m_run_in_scheduler) {
        SetThis(this);
        if (swapcontext(&(t_scheduler_task->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_scheduler_task failed" << std::endl;
			pthread_exit(NULL); 
        }
    } else {
        SetThis(this);
        if (swapcontext(&(t_thread_task->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_thread_task failed" << std::endl;
			pthread_exit(NULL);
        }
    }   
}

void Task::yield() {
    assert(m_state == RUNNING || m_state == TERM);

    if (m_state != TERM) {
        m_state = READY;
    }

    if (m_run_in_scheduler) {
        SetThis(t_scheduler_task);
        if (swapcontext(&m_ctx, &(t_scheduler_task->m_ctx))) {
            std::cerr << "yield() to t_scheduler_task failed\n";
			pthread_exit(NULL);
        }
    } else {
        SetThis(t_thread_task.get());
        if (swapcontext(&m_ctx, &(t_thread_task->m_ctx))) {
            std::cerr << "yield() to t_thread_task failed\n";
			pthread_exit(NULL);
        }
    }
}

void Task::MainFunc() {
    std::shared_ptr<Task> curr = GetThis();
    assert(curr != nullptr);

	curr->m_cb(); 
	curr->m_cb = nullptr;
	curr->m_state = TERM;

	// 运行完毕 -> 让出执行权
	auto raw_ptr = curr.get();
	curr.reset(); 
	raw_ptr->yield(); 
}
}