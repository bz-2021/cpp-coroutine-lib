#include "thread.h"

#include <sys/syscall.h>
#include <iostream>
#include <unistd.h>

namespace coroutine {

// 被 thread_local 修饰的变量在不同线程中各有独立实体
static thread_local Thread* t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOWN";

pid_t Thread::GetThreadId() {
    // 系统调用获取线程 ID
    return syscall(SYS_gettid);
}

Thread* Thread::GetThis() {
    return t_thread;
}

const std::string& Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string& name) {
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string& name) : m_cb(cb), m_name(name) {
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {
        std::cerr << "pthread_create thread fail, rt = " << rt << " name = " << name << std::endl;
        throw std::logic_error("pthread_create error");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        // 将线程设置为分离状态，线程结束时资源会自动释放
        pthread_detach(m_thread);
        m_thread = 0;
    }
}

void Thread::join() {
    if (m_thread) {
        // 等待指定的线程结束
        int rt = pthread_join(m_thread, nullptr);
        if (rt) {
            std::cerr << "pthread_join failed, rt = " << rt << " name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg) {
    Thread* thread = (Thread*)arg;

    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = GetThreadId();
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb); // 交换回调函数，减少引用计数

    thread->m_semaphore.signal(); // 初始化完成，发送信号

    cb();
    return 0;
}
    
}