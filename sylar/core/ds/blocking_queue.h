#ifndef __SYLAR_DS_BLOCKING_QUEUE_H__
#define __SYLAR_DS_BLOCKING_QUEUE_H__

#include "sylar/core/mutex.h"

namespace sylar {
namespace ds {

/**
 * @brief 阻塞队列
 * @details 提供线程安全的阻塞队列实现，支持多线程环境下的数据生产和消费
 * 使用信号量实现阻塞功能，互斥锁保证数据访问的线程安全
 * @tparam T 队列中存储的数据类型
 */
template<class T>
class BlockingQueue {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<BlockingQueue> ptr;
    /// 数据类型定义（使用智能指针）
    typedef std::shared_ptr<T> data_type;
    /// 互斥锁类型定义
    typedef Spinlock MutexType;

    /**
     * @brief 向队列尾部添加元素
     * @param[in] data 要添加的元素
     * @return 添加元素后队列的大小
     * @note 该操作是线程安全的，添加完成后会通知等待的线程
     */
    size_t push(const data_type& data) {
        MutexType::Lock lock(m_mutex);
        m_datas.push_back(data);
        size_t size = m_datas.size();
        lock.unlock();  // 先解锁再通知，避免线程被唤醒后立即阻塞
        m_sem.notify();  // 通知等待的线程有新数据可用
        return size;
    }

    /**
     * @brief 从队列头部取出元素
     * @return 队列头部的元素
     * @note 如果队列为空，当前线程会阻塞等待直到有新元素被添加
     * 该操作是线程安全的
     */
    data_type pop() {
        m_sem.wait();  // 如果队列为空，会阻塞等待
        MutexType::Lock lock(m_mutex);
        auto v = m_datas.front();
        m_datas.pop_front();
        return v;
    }

    /**
     * @brief 获取队列当前大小
     * @return 队列中元素的数量
     * @note 该操作是线程安全的
     */
    size_t size() {
        MutexType::Lock lock(m_mutex);
        return m_datas.size();
    }

    /**
     * @brief 判断队列是否为空
     * @return 队列为空返回true，否则返回false
     * @note 该操作是线程安全的
     */
    bool empty() {
        MutexType::Lock lock(m_mutex);
        return m_datas.empty();
    }

    /**
     * @brief 通知所有等待的线程
     * @note 强制唤醒所有等待在该队列上的线程
     * 通常用于关闭队列或紧急情况
     */
    void notifyAll() {
        m_sem.notifyAll();
    }
private:
    sylar::FiberSemaphore m_sem;  ///< 信号量，用于实现阻塞功能
    MutexType m_mutex;            ///< 互斥锁，保证数据访问的线程安全
    std::list<data_type> m_datas; ///< 存储队列元素的列表
};

}
}

#endif

/*
使用注意事项：
1. 该阻塞队列适用于生产者-消费者模型，线程安全
2. 当队列为空时，pop() 操作会阻塞当前线程直到有新元素加入
3. 避免在持有互斥锁的情况下进行耗时操作，以减少锁竞争
4. notifyAll() 方法会唤醒所有等待的线程，可能导致惊群效应，谨慎使用
5. 队列中存储的是智能指针类型，确保资源正确管理，避免内存泄漏
6. 使用 Spinlock 作为互斥锁类型，适用于锁持有时间短的场景
*/
