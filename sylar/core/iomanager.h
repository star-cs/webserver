#ifndef __SYLAR_IO_MANAGER_H__
#define __SYLAR_IO_MANAGER_H__
#include <unordered_map>

#include "scheduler.h"
#include "timermanager.h"

namespace sylar {

class IOManager : public Scheduler , public TimerManager{
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;

    /**
     * IO事件，继承 epoll事件的定义
     * 
     * 模拟epocll_event里的 或 操作添加 触发事件
     */
    enum Event{
        NONE = 0x0,
        READ = 0x1,
        WRITE = 0x4,  
    };

    static IOManager* GetThis();
    
private:
    /**
     * 
     * 
     */
    struct FdContext{
        typedef Mutex MutexType;

        struct EventContext{
            ///执行事件回调的调度器
            Scheduler* scheduler = nullptr;
            /// 回调协程
            Fiber::ptr fiber;
            /// 回调函数
            std::function<void()> cb;
        };

        EventContext& getEventContext(Event event);
        void resetEventContext(EventContext& ctx);
        void triggerEvent(Event event);

        // 读事件上下文
        EventContext read;

        // 写事件上下文
        EventContext write;

        // 事件关联的句柄
        int fd = 0;

        // 注册事件
        Event events = NONE;

        MutexType mutex;
    };

public:
    
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");

    ~IOManager();

    // 下面对上下文的操作，事件只能 READ，WRITE分开操作

    /**
     * 添加事件
     * fd socket句柄
     * event 事件类型
     * cb 事件回调函数
     * 
     * @return 添加成功返回0，失败返回-1
     */

    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    
    /**
     * 删除事件
     * 
     * 删除，不触发
     */
    bool delEvent(int fd, Event event);

    /**
     * 清除事件
     * 
     * 清除事件触发条件，直接触发掉事件
     */
    bool cancelEvent(int fd, Event event);

    bool cancelAll(int fd);
    
    void contextResize(size_t size);

protected:
    /**
      * @brief 通知协程调度器有任务了
      */
    void tickle();


    /**
     * 是否可以停止
     */
    bool stopping();
 
    /**
    * @brief 返回是否可以停止
    * 和上面一个方法，区别是能调用
    */
    bool stopping(uint64_t& next_timeout);

    /**
    * @brief 无任务调度时执行idle协程
    */
    void idle();

    /**
     * 当有定时器插入到头部时，要重新更新epoll_wait的超时事件
     * 这里是唤醒idle协程以便使用新的超时时间
     */
    void onTimerInsertedAtFront() override;
private:
    // epoll 文件句柄
    int m_epfd = 0;
    // pipe 句柄 fd[0]读端 fd[1]写端
    int m_tickleFds[2];
    // 当前等待执行的IO事件数量
    std::atomic<size_t> m_pendingEventCount = {0};

    RWMutexType m_mutex;

    std::vector<FdContext *> m_fdContexts;
};

}

#endif