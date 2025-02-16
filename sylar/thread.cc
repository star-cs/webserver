#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar{

// 方便 后续可以在其他地方访问当前线程的信息。
static thread_local Thread *t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread::Thread(std::function<void()> cb, const std::string& name) :m_cb(cb), m_name(name){
    /**
     * int pthread_create(pthread_t*restrict tidp,const pthread_attr_t *restrict_attr,void*（*start_rtn)(void*),void *restrict arg);
        第一个参数为指向线程标识符的指针。
        二个参数用来设置线程属性。  默认为nullptr
        第三个参数是线程运行函数的起始地址。
        后一个参数是运行函数的参数。
        若成功则返回0，否则返回出错编号
     *  */    
    if(name.empty()){
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if(rt){
        SYLAR_LOG_ERROR(g_logger) << "pthead_create thread fail , rt = " << rt 
                                  << " name = " << m_name;
        throw std::logic_error("pthread_create error");
    }

    m_semaphore.wait(); // 条件变量，主线程一直等着所有线程创建完毕
}

Thread::~Thread(){
    if(m_thread){
        // pthread_detach()即主线程与子线程分离，子线程结束后，资源自动回收。
        pthread_detach(m_thread);
    }
}

void Thread::join(){
    if(m_thread){
        //pthread_join()即是子线程合入主线程，主线程阻塞等待子线程结束，然后回收子线程资源。
        int rt = pthread_join(m_thread, nullptr);
        if(rt){
            SYLAR_LOG_ERROR(g_logger) << "pthead_join thread fail , rt = " << rt 
                                  << " name = " << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

/**
 * 将非静态成员参数转到 static thread_loacl的变量上，方便后续的访问。
 */
void* Thread::run(void* args){
    Thread* thread = (Thread*) args;
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = GetThreadId();

    //pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());
    sylar::SetThreadName(thread->m_name);

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    thread->m_semaphore.notify();   // 通知传入的 线程，相当于是通知主线程，线程创建完成。

    cb();
    return 0;
}

Thread *Thread::GetThis(){
    return t_thread;
}

const std::string &Thread::GetName(){
    return t_thread_name;
}

void Thread::SetName(const std::string &name){
    if(name.empty()){
        return;
    }
    if(t_thread){
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

}   // end namespace 