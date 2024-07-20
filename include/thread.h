#ifndef _THREAD_H_
#define _THREAD_H_

#include <mutex>
#include <condition_variable>
#include <functional>
#include <pthread.h>

namespace coroutine {

// Semaphore 是用于线程同步的信号量
class Semaphore {
public:
    explicit Semaphore(int count_ = 0) : count(count_) {}

    // P 操作
    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        // 进入等待状态，直到其他线程调用 signal() 
        while (count == 0) {
            cv.wait(lock);
        }
        count--;
    }

    // V 操作
    void signal() {
        std::unique_lock<std::mutex> lock(mtx);
        count++;
        // 唤醒一个等待线程
        cv.notify_one();
    }

private:
    std::mutex mtx;
    // https://en.cppreference.com/w/cpp/thread#Condition_variables
    std::condition_variable cv;
    int count;
};

class Thread {
public:
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();

    pid_t getId() const { return m_id; }
    const std::string& getName() const { return m_name; }

    void join();

    static pid_t GetThreadId();
    static Thread* GetThis();
    static const std::string& GetName();
    static void SetName(const std::string& name);

private:
    static void* run(void* arg);
    
    pid_t m_id = -1;
    pthread_t m_thread = 0;

    std::function<void()> m_cb;
    std::string m_name;

    Semaphore m_semaphore;
};

}

#endif