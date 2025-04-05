#ifndef __SYLAR_BUF_MANAGER_H__
#define __SYLAR_BUF_MANAGER_H__

#include "mutex.h"
#include <memory>
#include <vector>
#include <sys/eventfd.h>
#include <functional>
#include <atomic>

namespace sylar {
    // log -> buffermanager -> iomanager -> log 存在循环依赖。
    // 预先声明
    class IOManager;  
    class Timer;
    
    class AsyncType { 
        public:
            enum Type{ASYNC_SAFE, ASYNC_UNSAFE, UNKNOW};
            static const char* ToString(AsyncType::Type type){
                switch (type) {
                    case ASYNC_SAFE:
                        return "ASYNC_SAFE";
                    case ASYNC_UNSAFE:
                        return "ASYNC_UNSAFE";
                    default:
                        return "UNKNOW";
                }
            }
            static AsyncType::Type FromString(const std::string& str){
                if (str == "ASYNC_SAFE") {
                    return ASYNC_SAFE;
                } else if (str == "ASYNC_UNSAFE") {
                    return ASYNC_UNSAFE;
                } else {
                    return UNKNOW;
                }
            }
        };
    
    struct BufferParams {
        AsyncType::Type type = AsyncType::Type::ASYNC_SAFE;
        size_t size = 0;
        size_t threshold = 0;
        size_t linear_growth = 0;
        size_t swap_threshold = 0;
        size_t swap_time = 0;
        IOManager* iom = nullptr;

        BufferParams() = default;

        BufferParams(AsyncType::Type async_type,
            size_t buffer_size,
            size_t thresh,
            size_t linear,
            size_t swap_thresh,
            size_t time,
            IOManager* io_manager)
        : type(async_type),
            size(buffer_size),
            threshold(thresh),
            linear_growth(linear),
            swap_threshold(swap_thresh),
            swap_time(time),
            iom(io_manager) {}

        bool isValid() const {
            if(size <= 0) 
                return false;

            if(iom == nullptr)
                return false;
            
            // 非安全模式时才检查 threshold 和 linear_growth
            if(type != AsyncType::Type::ASYNC_SAFE) {
                if(threshold <= size) 
                    return false;
                if(linear_growth <= 0) 
                    return false;
            }
            
            if(swap_threshold <= 0) 
                return false;
            if(swap_time <= 0) 
                return false;
            if(swap_threshold >= size) 
                return false;
            if(type == AsyncType::Type::UNKNOW) 
                return false;

            return true;
        }
    };

    

    class Buffer {
    public:
        using ptr = std::shared_ptr<Buffer>;

        Buffer(size_t buffer_size);
        Buffer(size_t buffer_size, size_t threshold, size_t linear_growth);
        
        void push(const char* data, size_t len);
        char* readBegin(int len);
        bool isEmpty();
        void swap(Buffer& buf);
        size_t writeableSize();
        size_t readableSize() const;
        const char* Begin() const;
        void moveWritePos(int len);
        void moveReadPos(int len);
        void Reset();

    protected:
        void ToBeEnough(size_t len);

    private:
        size_t m_buffer_size;
        size_t m_threshold;
        size_t m_linear_growth;
        std::vector<char> m_buffer;
        size_t m_write_pos = 0;
        size_t m_read_pos = 0;
    };

    using functor = std::function<void(Buffer::ptr&)>;

    class BufferManager {
    public:
        using MutexType = Spinlock;
        using ptr = std::shared_ptr<BufferManager>;
        
        BufferManager(const functor& cb, 
            AsyncType::Type asyncType,
            size_t buffer_size,
            size_t threshold,
            size_t linear_growth,
            size_t swap_threshold,
            size_t swap_time,
            IOManager* iom);

        BufferManager(const functor& cb, const BufferParams& bufferParams);

        ~BufferManager();
        
        void stop();
        void push(const char* data, size_t len);
        void push(Buffer::ptr buffer);
        
    protected:
        void swap_pop();
        void swap_pop_one();
    private:
        MutexType m_mutex;
        std::atomic<bool> m_stop;
        FiberSemaphore m_sem_producer;
        FiberSemaphore m_sem_consumer;
        AsyncType::Type m_asyncType;
        Buffer::ptr m_buffer_productor;
        Buffer::ptr m_buffer_consumer;
        functor m_callback;
        size_t m_swap_threshold;
        size_t m_swap_time;
        std::shared_ptr<Timer> m_timer;
        void swap_buffers() {
            std::swap(m_buffer_productor, m_buffer_consumer);
        }
    };


}

#endif