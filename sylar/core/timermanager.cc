#include "sylar/core/timermanager.h"
#include "sylar/core/util/util.h"

namespace sylar
{

bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const
{
    if (!lhs && !rhs) {
        return false;
    }
    // 空的放前面？？？
    if (!lhs) {
        return true;
    }

    if (!rhs) {
        return false;
    }

    if (lhs->m_next < rhs->m_next) {
        return true;
    }

    if (lhs->m_next > rhs->m_next) {
        return false;
    }

    return lhs.get() < rhs.get();
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager *manager)
    : m_ms(ms), m_cb(cb), m_recurring(recurring), m_manager(manager)
{
    m_next = GetCurrentMS() + m_ms; // 执行的 绝对时间
}

Timer::Timer(uint64_t next) : m_next(next)
{
}

// 取消定时器
bool Timer::cancel()
{
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if (m_cb) {
        m_cb = nullptr;
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

// 刷新设置定时器的执行时间
bool Timer::refresh()
{
    // TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    // if(!m_cb){
    //     return false;
    // }

    // auto it = m_manager->m_timers.find(shared_from_this());
    // if(it == m_manager->m_timers.end()){
    //     return false;
    // }

    // m_manager->m_timers.erase(it);
    // m_next = GetCurrentTimeMS() + m_ms;
    // m_manager->m_timers.insert(shared_from_this());
    // return true;
    return reset(m_ms, true);
}

// 重置定时器事件
bool Timer::reset(uint64_t ms, bool from_now)
{
    if (m_ms == ms && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    // 如果 执行的周期不变，也没从现在开始 from_now false。那就退出。
    if (!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);

    uint64_t start = 0;
    if (from_now) {
        start = GetCurrentMS();
    } else {
        start = m_next - m_ms; // 原本的事件开始的事件。
    }
    m_ms = ms;
    m_next = start + m_ms;

    m_manager->addTimer(shared_from_this(), lock);
    return true;
}

TimerManager::TimerManager()
{
    m_previouseTime = GetCurrentMS();
}

TimerManager::~TimerManager()
{
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring)
{
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

// 增加代码复用，让addConditionTimer 可以调用 addTiemr
// 太妙了 ~~~
// 若直接使用std::shared_ptr参数，当定时器回调持有该shared_ptr时，会强制延长关联对象的生命周期，即使外部已不再需要该对象。
// 用weak_ptr则不会增加引用计数，允许关联对象在外部引用归零时正常析构。

// 这种模式常用于需要对象关联生命周期的定时任务，例如：
// 网络连接超时检测（当连接已关闭时无需触发超时回调）
// 资源释放校验（当资源持有者已销毁时取消清理操作）
static void OnTimer(std::weak_ptr<void> weak_ptr, std::function<void()> cb)
{
    std::shared_ptr<void> it = weak_ptr.lock();
    if (it) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb,
                                           std::weak_ptr<void> weak_cond, bool recurring)
{
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer()
{
    RWMutexType::ReadLock lock(m_mutex);
    // 获取下一次最近执行事件的 相对事件，重置 m_tickle
    m_tickled = false;
    if (m_timers.empty()) {
        return ~0ull;
    }

    const Timer::ptr &next = *m_timers.begin();
    uint64_t now_ms = GetCurrentMS();
    if (now_ms >= next->m_next) {
        return 0;
    } else {
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> > &cbs)
{
    uint64_t now_ms = GetCurrentMS();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty()) {
            return;
        }
    }

    RWMutexType::WriteLock lock(m_mutex);
    bool rollover = detectClockRollover(now_ms);
    if (!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    Timer::ptr now_timer(new Timer(now_ms));
    // 获取到第一个 发生时间 < 现在。即需要执行的事件
    // 如果系统时间被往前调整了 1个小时，就把全部定时器的事件 返回。
    // 这个就比较粗暴了~
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
    while (it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }

    expired.insert(expired.begin(), m_timers.begin(), it);
    m_timers.erase(m_timers.begin(), it);
    cbs.reserve(expired.size());

    for (auto &timer : expired) {
        cbs.push_back(timer->m_cb);
        // 如果事件需要重复执行，再次写回 m_timers
        if (timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock &lock)
{
    auto it = m_timers.insert(val).first;
    bool at_front = (it == m_timers.begin()) && !m_tickled;
    if (at_front) {
        m_tickled = true;
    }
    lock.unlock();

    if (at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::hasTimer()
{
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

bool TimerManager::detectClockRollover(uint64_t now_ms)
{
    bool rollover = false;
    if (now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

} // namespace sylar
