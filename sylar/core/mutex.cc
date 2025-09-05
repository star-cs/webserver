#include "mutex.h"
#include "sylar/core/common/macro.h"
#include "scheduler.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar {
    
Semaphore::Semaphore(uint32_t count) {
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    sem_destroy(&m_semaphore);
}

void Semaphore::wait() {
    if(sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() {
    if(sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

FiberSemaphore::FiberSemaphore(size_t initial_concurrency)
    :m_concurrency(initial_concurrency) {
}

FiberSemaphore::~FiberSemaphore() {
    SYLAR_ASSERT(m_waiters.empty());
}

bool FiberSemaphore::tryWait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return true;
        }
        return false;
    }
}

void FiberSemaphore::show(){
    // 新增调试打印
    std::cout << "m_waiters content (" << m_waiters.size() << " items):" << std::endl;
    for(const auto& item : m_waiters) {
        std::cout << "  Scheduler: " << item.first->getName()
                << " | Fiber: " << item.second->getId() << std::endl;; // 假设Fiber类有getId()方法
    }
}

void FiberSemaphore::wait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        
        show();

        if(m_concurrency > 0u) {
            --m_concurrency;
            return;
        }
        m_waiters.push_back(std::make_pair(Scheduler::GetThis(), Fiber::GetThis()));
        
        show();
    }
    Fiber::GetThis()->yield();
}

void FiberSemaphore::notify() {
    MutexType::Lock lock(m_mutex);
    show();
    if(!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        SYLAR_LOG_DEBUG(g_logger) << "Notifying Fiber ID: " << next.second->getId();
        next.first->schedule(next.second);
    } else {
        ++m_concurrency;
        SYLAR_LOG_DEBUG(g_logger) << "No waiters. Incrementing concurrency to: " << m_concurrency;
    }
    show();
}


void FiberCondition::wait(MutexType::Lock& lock){
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        m_waiters.push_back(std::make_pair(Scheduler::GetThis(), Fiber::GetThis()));
        // printWaiters();
    }
    lock.unlock();
    Fiber::GetThis()->yield();
    lock.lock();
}

void FiberCondition::notify_one(){
    MutexType::Lock lock(m_mutex);
    if (!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->schedule(next.second);
    }
}

void FiberCondition::notify_all() {
    MutexType::Lock lock(m_mutex);
    for (auto& waiter : m_waiters) {
        waiter.first->schedule(waiter.second);
    }
    m_waiters.clear();
}


void FiberCondition::printWaiters() const {
    std::cout << "Current waiters (" << m_waiters.size() << "):\n";
    for (auto& waiter : m_waiters) {
        // 假设 Fiber 有 getId() 方法，根据实际Fiber实现调整输出
        std::cout << "  Scheduler@" << waiter.first->getName()
                  << " - FiberID:" << waiter.second->getId() 
                  << std::endl;
    }
}


}