#ifndef __SYLAR_TIMEMANAGER_H__
#define __SYLAR_TIMEMANAGER_H__

#include <memory>
#include <set>
#include <vector>

#include "mutex.h"

namespace sylar{


class TimerManager;
/**
 * 定时器
 */
class Timer : public std::enable_shared_from_this<Timer> {
friend class TimerManager;
public:
    typedef std::shared_ptr<Timer> ptr;

    // 取消定时器
    bool cancel();

    // 刷新设置定时器的执行时间
    bool refresh();

    // 重置定时器事件
    bool reset(uint64_t ms, bool from_now);
    
// 构造函数定义为私有方法，只能通过TimerManager类来创建Timer对象
private:
    /**
     * ms 定时器执行的回调事件
     * cb 回调函数
     * recurring 是否循环执行
     * manager 定时器管理器
     */
    Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager);

    /**
     * next 执行的时间戳（毫秒）（绝对时间）
     */
    Timer(uint64_t next);

private:
    /// 执行周期
    uint64_t m_ms = 0;
    /// 精确的执行时间
    uint64_t m_next = 0;
    /// 回调函数
    std::function<void()> m_cb;
    /// 是否循环定时器
    bool m_recurring = false;
    /// 定时器管理器
    TimerManager* m_manager = nullptr;

private:
    /**
     * 定时器比较仿函数
     */
    struct Comparator{
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };

};


class TimerManager{
friend class Timer;
public:
    typedef RWMutex RWMutexType;

    TimerManager();

    virtual ~TimerManager();

    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring = false);
    
    // 附带条件的添加定时器
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

    // 获取下一个最近的定时器
    uint64_t getNextTimer();

    /**
     * @brief 获取需要执行的定时器的回调函数列表
     * @param[out] cbs 回调函数数组
     */
    void listExpiredCb(std::vector<std::function<void()> >& cbs);

    /**
     * @brief 是否有定时器
     */
    bool hasTimer();


    /**
     * 检测服务器时间是否被调后了
     * 
     */
    bool detectClockRollover(uint64_t now_ms);

   
protected:
    /// 存在添加的定时器 的 执行时间 已经过了的情况
    /// 不懂~怎么操作 ~ 
    virtual void onTimerInsertedAtFront() = 0;

    /**
     * @brief 将定时器添加到管理器中
     * 
     * 在这里添加了 m_tickled 
     * 保证当事件处于front，只会执行一次 onTimerInsertedAtFront()，唤醒 epoll_wait，处理事件~
     */
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);

private:
    RWMutexType m_mutex;
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    /// 是否触发onTimerInsertedAtFront
    bool m_tickled = false;
    /// 上次执行时间
    uint64_t m_previouseTime = 0;
};


}




#endif