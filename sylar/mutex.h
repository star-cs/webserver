#ifndef __SYLAR_MUTEX_H__
#define __SYLAR_MUTEX_H__

#include <semaphore.h>
#include <stdint.h>
#include <thread>
#include <atomic>

namespace sylar{

class Semaphore{
public:
    Semaphore(uint32_t count = 0);

    ~Semaphore();

    void wait();

    void notify();
private:
    sem_t m_semaphore;
};

// 局部锁的模板实现  RAII
template<class T>
struct ScopedLockImpl{
public:
    ScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.lock();
        m_locked = true;
    }

    ~ScopedLockImpl(){
        unlock();
    }

    void lock(){
        if(!m_locked){
            m_mutex.lock();
            m_locked = true;
        }
    }

    void unlock(){
        if(m_locked){
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    // mutex
    T& m_mutex;
    bool m_locked;
};

// 局部读锁模板实现
template<class T>
struct ReadScopedLockImpl{
public:
    ReadScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.rdlock();
        m_locked = true;
    }

    ~ReadScopedLockImpl(){
        unlock();
    }

    void lock(){
        if(!m_locked){
            m_mutex.rdlock();
            m_locked = true;
        }
    }

    void unlock(){
        if(m_locked){
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    // mutex
    T& m_mutex;
    bool m_locked;
};

// 局部写锁模板实现
template<class T>
struct WriteScopedLockImpl{
public:
    WriteScopedLockImpl(T& mutex) : m_mutex(mutex) {
        m_mutex.wrlock();
        m_locked = true;
    }

    ~WriteScopedLockImpl(){
        unlock();
    }

    void lock(){
        if(!m_locked){
            m_mutex.wrlock();
            m_locked = true;
        }
    }

    void unlock(){
        if(m_locked){
            m_mutex.unlock();
            m_locked = false;
        }
    }

private:
    // mutex
    T& m_mutex;
    bool m_locked;
};


// 读写互斥量
class RWMutex{
public:
    typedef ReadScopedLockImpl<RWMutex> ReadLock;
    typedef WriteScopedLockImpl<RWMutex> WriteLock;

    RWMutex(){
        pthread_rwlock_init(&m_lock, nullptr);
    }

    ~RWMutex(){
        pthread_rwlock_destroy(&m_lock);
    }

    void rdlock(){
        pthread_rwlock_rdlock(&m_lock);
    }

    void wrlock(){
        pthread_rwlock_wrlock(&m_lock);
    }

    void unlock(){
        pthread_rwlock_unlock(&m_lock);
    }

private:
    pthread_rwlock_t m_lock;
};


class Mutex{
public:
    typedef ScopedLockImpl<Mutex> Lock;
    Mutex(){
        pthread_mutex_init(&m_mutex, nullptr);
    }

    ~Mutex(){
        pthread_mutex_destroy(&m_mutex);
    }

    void lock(){
        pthread_mutex_lock(&m_mutex);
    }
    
    void unlock(){
        pthread_mutex_unlock(&m_mutex);
    }

private:
    pthread_mutex_t m_mutex;
};


// 自旋锁
class SpinkLock{
public:
    typedef ScopedLockImpl<SpinkLock> Lock;
    SpinkLock(){
        pthread_spin_init(&m_mutex, 0);
    }

    ~SpinkLock(){
        pthread_spin_destroy(&m_mutex);
    }

    void lock(){
        pthread_spin_lock(&m_mutex);
    }
    
    void unlock(){
        pthread_spin_unlock(&m_mutex);
    }

private:
    pthread_spinlock_t m_mutex;
};

class CASLock{
public:
    typedef ScopedLockImpl<SpinkLock> Lock;
    CASLock(){
        m_mutex.clear(); 
    }

    ~CASLock(){
    }

    void lock(){
        while(std::atomic_flag_test_and_set_explicit(&m_mutex, std::memory_order_acquire));
    }
    
    void unlock(){
        std::atomic_flag_clear_explicit(&m_mutex, std::memory_order_release);
    }

private:
    std::atomic_flag m_mutex;
};

}
#endif