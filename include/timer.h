#ifndef _TIMER_H_
#define _TIMER_H_

#include <memory>
#include <vector>
#include <set>
#include <shared_mutex>
#include <assert.h>
#include <functional>
#include <mutex>

namespace coroutine {

class TimerManager;

/**
 * @brief 基于 epoll 超时实现毫秒级定时器
 * @details 通过定时器实现给服务器注册定时事件
 */
class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;
public:
    // 从时间堆中删除timer
    bool cancel();
    // 刷新timer
    bool refresh();
    // 重设timer的超时时间
    bool reset(uint64_t ms, bool from_now);

private:
    Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);
 
private:
    // 是否循环
    bool m_recurring = false;
    // 超时时间
    uint64_t m_ms = 0;
    // 绝对超时时间
    std::chrono::time_point<std::chrono::system_clock> m_next;
    // 超时时触发的回调函数
    std::function<void()> m_cb;
    // 管理此timer的管理器
    TimerManager* m_manager = nullptr;

private:
    // 实现最小堆的比较函数
    struct Comparator 
    {
        bool operator()(const std::shared_ptr<Timer>& lhs, const std::shared_ptr<Timer>& rhs) const;
    };
};

class TimerManager 
{
    friend class Timer;
public:
    TimerManager();
    virtual ~TimerManager();

    // 添加timer
    std::shared_ptr<Timer> addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);

    // 添加条件timer
    std::shared_ptr<Timer> addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

    // 拿到堆中最近的超时时间
    uint64_t getNextTimer();

    // 取出所有超时定时器的回调函数
    void listExpiredCb(std::vector<std::function<void()>>& cbs);

    // 堆中是否有timer
    bool hasTimer();

protected:
    // 当一个最早的timer加入到堆中 -> 调用该函数
    virtual void onTimerInsertedAtFront() {};

    // 添加timer
    void addTimer(std::shared_ptr<Timer> timer);

private:
    // 当系统时间改变时 -> 调用该函数
    bool detectClockRollover();

private:
    std::shared_mutex m_mutex;
    // 时间堆
    std::set<std::shared_ptr<Timer>, Timer::Comparator> m_timers;
    // 在下次getNextTime()执行前 onTimerInsertedAtFront()是否已经被触发了 -> 在此过程中 onTimerInsertedAtFront()只执行一次
    bool m_tickled = false;
    // 上次检查系统时间是否回退的绝对时间
    std::chrono::time_point<std::chrono::system_clock> m_previouseTime;
};

}

#endif