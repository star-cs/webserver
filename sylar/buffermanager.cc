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

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

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
    RWMutexType::WriteLock lock(m_mutex);
    ToBeEnough(len);
    std::copy(data, data + len, &m_buffer[m_write_pos]);
    m_write_pos += len;
}

void Buffer::push(const std::string& str){
    push(str.c_str(), str.size());
}


char* Buffer::readBegin(int len) {
    RWMutexType::ReadLock lock(m_mutex);
    assert(static_cast<size_t>(len) < readableSize());
    return &m_buffer[m_read_pos];
}

bool Buffer::isEmpty() {
    RWMutexType::ReadLock lock(m_mutex);
    return m_write_pos == m_read_pos; 
}

void Buffer::swap(Buffer& buf) {
    RWMutexType::WriteLock lock(m_mutex);
    m_buffer.swap(buf.m_buffer);
    std::swap(m_read_pos, buf.m_read_pos);
    std::swap(m_write_pos, buf.m_write_pos);
}

size_t Buffer::writeableSize() {
    RWMutexType::ReadLock lock(m_mutex);
    return m_buffer.size() - m_write_pos;
}

size_t Buffer::readableSize() const {
    RWMutexType::ReadLock lock(m_mutex);
    return m_write_pos - m_read_pos;
}

const char* Buffer::Begin() const {
    RWMutexType::ReadLock lock(m_mutex);
    return &m_buffer[m_read_pos];
}

void Buffer::moveWritePos(int len) {
    RWMutexType::WriteLock lock(m_mutex);
    assert(static_cast<size_t>(len) <= writeableSize());
    m_write_pos += len;
}

void Buffer::moveReadPos(int len) {
    RWMutexType::WriteLock lock(m_mutex);
    assert(static_cast<size_t>(len) <= readableSize());
    m_read_pos += len;
}

void Buffer::Reset() {
    RWMutexType::WriteLock lock(m_mutex);
    m_write_pos = 0;
    m_read_pos = 0;
}

std::string Buffer::dump() const{
    RWMutexType::ReadLock lock(m_mutex);
    return std::string(Begin(), readableSize());
}

void Buffer::ToBeEnough(size_t len) {
    RWMutexType::WriteLock lock(m_mutex);
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
                            size_t swap_time,
                            IOManager* iom)
    :   m_stop(false),
        m_swap_status(false),
        m_asyncType(asyncType),
        m_buffer_productor(std::make_shared<Buffer>(buffer_size, threshold, linear_growth)),
        m_buffer_consumer(std::make_shared<Buffer>(buffer_size, threshold, linear_growth)),
        m_callback(cb),
        m_swap_time(swap_time)
{
    assert(iom != nullptr);

    iom->schedule(std::bind(&BufferManager::ThreadEntry, this));
    m_timer = iom->addTimer(m_swap_time, std::bind(&BufferManager::TimerThreadEntry, this), true);
}

BufferManager::BufferManager(const functor& cb, const BufferParams& bufferParams)
    : BufferManager(
        cb,
        bufferParams.type,
        bufferParams.size,
        bufferParams.threshold,
        bufferParams.linear_growth,
        bufferParams.swap_time,
        bufferParams.iom)
{
}

BufferManager::~BufferManager() {
    SYLAR_LOG_DEBUG(g_logger) << "BufferManager destructor called.";
    stop();
}

void BufferManager::stop() { 
    m_timer->cancel();              // 删除定时器
    m_stop = true;          
    m_cond_consumer.notify_one();   // 唤醒，m_stop=true 满足条件
}

void BufferManager::push(const char* data, size_t len) {
    MutexType::Lock lock(m_mutex);

    if(m_asyncType == AsyncType::ASYNC_SAFE){
        if (len > m_buffer_productor->writeableSize()) {
            SYLAR_LOG_DEBUG(g_logger) << "notify consumer";
            m_cond_consumer.notify_one();
        }
        m_cond_producer.wait(lock, [&](){
            return (m_stop || (len <= m_buffer_productor->writeableSize()));
        });
    }
        
    if(m_stop){
        throw std::runtime_error("BufferManager is stopped");
    }
    m_buffer_productor->push(data, len);
    SYLAR_LOG_DEBUG(g_logger) << "m_buffer_productor writeableSize: " << m_buffer_productor->writeableSize();

}


void BufferManager::push(Buffer::ptr buffer) {
    push(buffer->Begin(), buffer->readableSize());
}

// 使用Timer，按照频率访问缓冲区
// 如果生产者没有就退出
void BufferManager::TimerThreadEntry(){
    {
        MutexType::Lock lock(m_mutex);

        if ((!m_buffer_productor->isEmpty() && m_buffer_consumer->isEmpty()) || m_stop) {
            swap_buffers();
        
            if(m_asyncType == AsyncType::ASYNC_SAFE){
                m_cond_producer.notify_all();
            }
        }else{
            return;
        }
    }
    {
        MutexType::Lock lock(m_swap_mutex);
        m_callback(m_buffer_consumer);
        m_buffer_consumer->Reset();
    }
}

void BufferManager::ThreadEntry() {
    while(true){
        {
            MutexType::Lock lock(m_mutex);
            SYLAR_LOG_DEBUG(g_logger) << "ThreadEntry started.";
            m_cond_consumer.wait(lock, [&](){
                return m_stop || (!m_buffer_productor->isEmpty() && m_buffer_consumer->isEmpty());
            });

            swap_buffers();

            if(m_asyncType == AsyncType::ASYNC_SAFE){
                m_cond_consumer.notify_all();
            }
        }
        {
            MutexType::Lock lock(m_swap_mutex);
            m_callback(m_buffer_consumer);
            m_buffer_consumer->Reset();
            if(m_stop && m_buffer_productor->isEmpty()) return;
        }
    }
}

std::ostream &operator<<(std::ostream &os, Buffer &buf){
    os << buf.dump();
    return os;
}


} // namespace sylar