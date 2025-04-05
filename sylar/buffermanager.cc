#include "buffermanager.h"
#include <cassert>
#include <stdexcept>
#include <unistd.h>
#include <sys/eventfd.h>
#include "fiber.h"
#include "iomanager.h"
#include "timermanager.h"
#include <iostream>
namespace sylar {

/******************************** Buffer 实现 ********************************/
Buffer::Buffer(size_t buffer_size) 
    : m_buffer_size(buffer_size),
      m_threshold(0),
      m_linear_growth(0) {
    m_buffer.resize(m_buffer_size);
}

Buffer::Buffer(size_t buffer_size, size_t threshold, size_t linear_growth)
    : m_buffer_size(buffer_size),
      m_threshold(threshold),
      m_linear_growth(linear_growth) {
    m_buffer.resize(m_buffer_size);
}

void Buffer::push(const char* data, size_t len) {
    ToBeEnough(len);
    std::copy(data, data + len, &m_buffer[m_write_pos]);
    m_write_pos += len;
}

char* Buffer::readBegin(int len) {
    assert(static_cast<size_t>(len) < readableSize());
    return &m_buffer[m_read_pos];
}

bool Buffer::isEmpty() { 
    return m_write_pos == m_read_pos; 
}

void Buffer::swap(Buffer& buf) {
    m_buffer.swap(buf.m_buffer);
    std::swap(m_read_pos, buf.m_read_pos);
    std::swap(m_write_pos, buf.m_write_pos);
}

size_t Buffer::writeableSize() {
    return m_buffer.size() - m_write_pos;
}

size_t Buffer::readableSize() const {
    return m_write_pos - m_read_pos;
}

const char* Buffer::Begin() const {
    return &m_buffer[m_read_pos];
}

void Buffer::moveWritePos(int len) {
    assert(static_cast<size_t>(len) <= writeableSize());
    m_write_pos += len;
}

void Buffer::moveReadPos(int len) {
    assert(static_cast<size_t>(len) <= readableSize());
    m_read_pos += len;
}

void Buffer::Reset() {
    m_write_pos = 0;
    m_read_pos = 0;
}

void Buffer::ToBeEnough(size_t len) {
    while (len > writeableSize()) {
        size_t new_size = m_buffer.size();
        if (new_size < m_threshold) {
            new_size *= 2;
        } else {
            new_size += m_linear_growth;
        }
        m_buffer.resize(new_size);
    }
}

/***************************** BufferManager 实现 ***************************/
BufferManager::BufferManager(const functor& cb, 
                            AsyncType::Type asyncType,
                            size_t buffer_size,
                            size_t threshold,
                            size_t linear_growth,
                            size_t swap_threshold,
                            size_t swap_time,
                            IOManager* iom)
    :   m_stop(false),
        m_sem_producer(0),
        m_sem_consumer(0),
        m_asyncType(asyncType),
        m_buffer_productor(std::make_shared<Buffer>(buffer_size, threshold, linear_growth)),
        m_buffer_consumer(std::make_shared<Buffer>(buffer_size, threshold, linear_growth)),
        m_callback(cb),
        m_swap_threshold(swap_threshold),
        m_swap_time(swap_time)
{
    assert(iom != nullptr);

    assert(m_swap_threshold > 0);
    iom->schedule(std::bind(&BufferManager::swap_pop, this));

    m_timer = iom->addTimer(m_swap_time, std::bind(&BufferManager::swap_pop_one, this), true);
}

BufferManager::BufferManager(const functor& cb, const BufferParams& bufferParams)
    : BufferManager(
        cb,
        bufferParams.type,
        bufferParams.size,
        bufferParams.threshold,
        bufferParams.linear_growth,
        bufferParams.swap_threshold,
        bufferParams.swap_time,
        bufferParams.iom)
{
}

BufferManager::~BufferManager() {
    stop();
}

void BufferManager::stop() { 
    m_timer->cancel();      // 删除定时器
    m_stop = true;          // 设置停止标志，生产者 禁止写入，消费者 直接 任务 完毕。
    m_sem_consumer.notify();// 唤醒退出 任务。
    swap_pop_one();         // 获取到 生产者 缓存里 的 残余数据。（主线程来清理）
}

void BufferManager::push(const char* data, size_t len) {
    MutexType::Lock lock(m_mutex);
    
    while (true) {
        // 检查停止标志（最高优先级）
        if (m_stop) {
            return;
        }

        // 检查缓冲区空间是否足够
        if (len <= m_buffer_productor->writeableSize() || m_asyncType != AsyncType::ASYNC_SAFE) {
            break; // 空间足够或非安全模式，直接写入
        }

        // 异步安全模式下空间不足，触发消费者处理
        m_sem_consumer.notify();  // 通知消费者交换缓冲区
        
        // 释放锁并等待生产者信号量（消费者处理后会触发）
        lock.unlock();
        m_sem_producer.wait();    // 等待缓冲区释放
        lock.lock();
    }

    // 写入数据（确保未停止）
    if (!m_stop) {
        m_buffer_productor->push(data, len);
    }
}


void BufferManager::push(Buffer::ptr buffer) {
    push(buffer->Begin(), buffer->readableSize());
}

// 使用Timer，按照频率访问缓冲区
// 如果生产者没有就退出
void BufferManager::swap_pop_one(){
    {
        MutexType::Lock lock(m_mutex);
        if (!m_buffer_productor->isEmpty() && m_buffer_consumer->isEmpty() && !m_stop) {
            swap_buffers();
            m_sem_producer.notify();
        }else{
            return;
        }
    }
    try{
        m_callback(m_buffer_consumer);
    }catch(...){
        std::cerr << "BufferManager::swap_pop_timer() exception" << std::endl;
    }
    m_buffer_consumer->Reset();
}

void BufferManager::swap_pop() {
    while(true){
        {
            MutexType::Lock lock(m_mutex);
            if (m_stop) {
                return; // 退出线程
            }
            if (!m_buffer_productor->isEmpty() && m_buffer_consumer->isEmpty()
                && m_swap_threshold >= m_buffer_productor->writeableSize() && !m_stop) {
                swap_buffers();
                m_sem_producer.notify();
            }else{
                lock.unlock();
                m_sem_consumer.wait();
                lock.lock();
                continue;
            }
        }
        try{
            m_callback(m_buffer_consumer);
        }catch(...){
            std::cerr << "BufferManager::swap_pop() exception" << std::endl;
        }
        m_buffer_consumer->Reset();
    }
}

} // namespace sylar