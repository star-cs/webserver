#ifndef __SYLAR_THREAD_H_
#define __SYLAR_THREAD_H_

#include <functional>
#include <memory>
#include "mutex.h"

namespace sylar{

class Thread{
public:
    typedef std::shared_ptr<Thread> ptr;
    Thread(std::function<void()> cb, const std::string& name);

    ~Thread();

    pid_t getId() const {return m_id;}

    const std::string& getName() const {return m_name;}

    void join();

    // 下面四个静态方法，是为了方便访问 在 thread.cc 里定义的 t_thread, t_thread_name 的。
    // 所以操作对象都是 t_thread t_thread_name
    static Thread *GetThis();

    static const std::string &GetName();
    
    static void SetName(const std::string &name);

    static void* run(void* args);
    
private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
private:
    // 线程id
    pid_t m_id= -1;
    // 线程
    pthread_t m_thread = 0;
    // 线程执行函数
    std::function<void()> m_cb;
    // 线程名
    std::string m_name;

    Semaphore m_semaphore;
};

}

#endif