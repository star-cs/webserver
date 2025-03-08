#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "util.h"
#include "iomanager.h"
#include "macro.h"
#include "log.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar{

enum EpollCtlOp{  
};

static std::ostream &operator<<(std::ostream& os, const EpollCtlOp& op){
    switch((int)op){
#define XX(ctl) \
    case ctl: \
        return os << #ctl;

        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
#undef XX
    default:
        return os << (int)op;
    }
}

static std::ostream& operator<<(std::ostream& os, EPOLL_EVENTS events){
    if(!events){
        return os << "0";
    }
    bool first = true;

#define XX(E)           \
    if(events & E){     \
        if(!first){     \
            os << "|";  \
        }               \
        os << #E;       \
        first = false;  \
    }            
    
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX

    return os;
}

IOManager* IOManager::GetThis(){
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

IOManager::FdContext::EventContext& IOManager::FdContext::getEventContex(Event event){
    switch(event){
    case IOManager::READ:
        return read;
    case IOManager::WRITE:
        return write;
    default:
        SYLAR_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContex invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext& ctx){
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}
 

void IOManager::FdContext::triggerEvent(Event event){
    // 确保待触发的事件必须已经注册过
    /**
     * cancelEvent 清除事件，不再关注该事件，直接触发对应的上下文任务
     * 注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件后都要重新添加。~ 
     */
    SYLAR_ASSERT(events & event);   
    events = (Event)(events & ~event);
    EventContext& ev_ctx = getEventContex(event);
    if(ev_ctx.cb){
        ev_ctx.scheduler->schedule(ev_ctx.cb);
    } else{
        ev_ctx.scheduler->schedule(ev_ctx.fiber);
    }
    resetEventContext(ev_ctx);
    return;
}


IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name){
    
    m_epfd = epoll_create(1);
    SYLAR_ASSERT(m_epfd > 0);

    int ret = pipe(m_tickleFds);        // [0]读端， [1]写端
    SYLAR_ASSERT(ret == 0);

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;       // 边沿触发  绑定pipe读端。 我们就可以对pipe写端写入，以tickle协程
    ev.data.fd = m_tickleFds[0];
 
    // 非阻塞方式，配合边缘触发
    // epoll_wait，当为 EL 的时候，即使没有数据获取到，也不会阻塞
    ret = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!ret);

    ret = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &ev);
    SYLAR_ASSERT(!ret);

    contextResize(32);

    start(); 
} 

IOManager::~IOManager(){
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);
    for(size_t i = 0 ; i < m_fdContexts.size() ; ++i){
        if(m_fdContexts[i]){
            delete m_fdContexts[i];
        }
    }
}

int IOManager::addEvent(int fd, IOManager::Event event, std::function<void()> cb){
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() > fd){
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    }else{
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        if((int)m_fdContexts.size() <= fd){
            contextResize(fd * 1.5);
        }
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(fd_ctx->events & event){
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                                  << " event=" << (EPOLL_EVENTS)event
                                  << "原本的 fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 将新添加的事件加入epoll_wait，使用epoll_wait私有指针存储 FdContext
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt){
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd 
                                << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                << rt << "(" << errno << ") (" << strerror(errno) << ") 原本的 fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    ++m_pendingEventCount;

    // 对epevent里的 fd_ctx 设置
    fd_ctx->events = (Event)(fd_ctx->events | event);
    // event是新的事件，设置对应的 EventContext
    IOManager::FdContext::EventContext& ev_ctx = fd_ctx->getEventContex(event);
    ev_ctx.scheduler = Scheduler::GetThis();
    if(cb){
        ev_ctx.cb.swap(cb);
    }else{
        ev_ctx.fiber = Fiber::GetThis();    // 如果回调函数 cb 为空，则把当前协程当成回调执行体 ~ （？这个不懂）
        SYLAR_ASSERT2(ev_ctx.fiber->getState() == Fiber::RUNNING, "state=" << ev_ctx.fiber->getState());
    }
    SYLAR_LOG_INFO(g_logger) << "addEvent";

    return 0;
}


bool IOManager::delEvent(int fd, Event event){
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() >= fd){
        SYLAR_LOG_ERROR(g_logger) << "delEvent fd error";
        return false;
    }
    
    fd_ctx = m_fdContexts[fd];

    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    // 删除的fd对应上下文的events肯定是要和event有交集~
    if(!(fd_ctx->events & event)){
        SYLAR_LOG_ERROR(g_logger) << "delEvent event error";
        return false;
    }

    // 删除后事件
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;    // 如果没了就是全删，如果还有事件就是修改
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt){
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd 
                                << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                << rt << "(" << errno << ") (" << strerror(errno) << ") 原本的 fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        return false;
    }

    --m_pendingEventCount;

    // 修改 fd_ctx，重置事件，以及删除的event事件对应的EventContext需要被reset
    fd_ctx->events = new_events;
    FdContext::EventContext& ev_ctx = fd_ctx->getEventContex(event);
    fd_ctx->resetEventContext(ev_ctx);
    return true;
}


bool IOManager::cancelEvent(int fd, Event event){
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() >= fd){
        SYLAR_LOG_ERROR(g_logger) << "cancelEvent fd error";
        return false;
    }

    fd_ctx = m_fdContexts[fd];

    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!(fd_ctx->events & event)){
        SYLAR_LOG_ERROR(g_logger) << "canalEvent event error";
        return false;
    }


    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt){
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd 
                                << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                << rt << "(" << errno << ") (" << strerror(errno) << ") 原本的  fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        return false;
    }

    fd_ctx->triggerEvent(event);

    --m_pendingEventCount;

    return true;
}

bool IOManager::cancelAll(int fd){
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd){
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events){
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd 
                                << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                << rt << "(" << errno << ") (" << strerror(errno) << ") 原本的  fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        return false;
    }

    if(fd_ctx->events & READ){
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }

    if(fd_ctx->events & WRITE){
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

void IOManager::contextResize(size_t size){
    m_fdContexts.resize(size);
    for(size_t i = 0 ; i < size ; i ++){
        if(!m_fdContexts[i]){
            m_fdContexts[i] = new FdContext();
            m_fdContexts[i]->fd = i;
        }
    }
}


/**
 * 通知调度协程，也就是让 协程 Scheduler::run() 中 idle 中退出
 * Scheduler::run() 每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
 * 如果没有调度线程处于idle状态，那就没必要发通知了。
 */
void IOManager::tickle(){
    SYLAR_LOG_DEBUG(g_logger) << "IOManager::tickle()";
    if(!(hasIdleThreads())){
        return;
    }
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}



bool IOManager::stopping(){
    return m_pendingEventCount == 0 && Scheduler::stopping();
}


/**
 * 调度器无调度任务会执行 idle 协程，对于 IO 调度器，idle 状态需要关注两个：
 * 1. 有没有新的调度任务，对应 Scheduler::schedule(); 如果有新的调度任务，那应该立即退出idle状态，并执行相应的任务；
 * 2. 关注当前注册的所有IO事件有没有触发，如果有触发，那就执行 ~  IO事件对应的 回调函数
 */
void IOManager::idle(){
    SYLAR_LOG_DEBUG(g_logger) << "IOManager::idle()";

    // 一次epoll_wait最多检测 256 个就绪事件
    const uint64_t MAX_EVENTS = 256;
    epoll_event* events = new epoll_event[MAX_EVENTS]();
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ev){
        delete[] ev;
    });

    while(true){
        if(stopping()){
            SYLAR_LOG_DEBUG(g_logger) << "name=" << getName() << " idle stopping exit";
            break;
        }
        static const int MAX_TIMEOUT = 5000;
        int rt = epoll_wait(m_epfd, events, MAX_EVENTS, MAX_TIMEOUT);

        if(rt < 0){
            if(errno == EINTR){ //在任何请求的事件发生或超时到期之前，信号处理程序中断了该调用
                continue;
            }
            SYLAR_LOG_ERROR(g_logger) << "epoll_wait(" << m_epfd << ") (rt="
                << rt << ") (errno=" << errno << ") (errstr:" << strerror(errno) << ")";
            break;
        }

        for(int i = 0 ; i < rt; ++i){
            epoll_event& event = events[i];
            // ticklefd[0]用于通知协程调度，这时只需要把管道读完
            // ??? 这有啥用
            if(event.data.fd == m_tickleFds[0]){ 
                uint8_t dummy[256];
                while(read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }
            // 如果不是 pipe 的读端触发，那就对应 IO事件被触发了。
            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);

            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd 的读写事件，否则有可能出现注册的事件永远执行不到的情况
             */

            // event.events 只有当前就绪的事件类型
            if(event.events & (EPOLLERR | EPOLLHUP)){
                // 过滤掉当前fd实际关注的事件类型 （如 READ / WRITE）
                // 确保只触发已注册的事件回调
                event.events |= (EPOLLERR | EPOLLHUP) & fd_ctx->events;
            }

            int real_events = NONE;
            if(event.events & EPOLLIN){
                real_events |= READ;
            }
            if(event.events & EPOLLOUT){
                real_events |= WRITE;
            }

            // 如果触发了的事件，和 上下文里的 事件任务 没有重合，那就取消
            if((fd_ctx->events & real_events) == NONE){
                continue;
            }

            // 执行完real_events后的events
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd_ctx->fd 
                                << ", " << (EPOLL_EVENTS)event.events << "):"
                                << rt << "(" << errno << ") (" << strerror(errno) << ") 原本的  fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        
                continue;
            }

            if(real_events & READ){
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if(real_events & WRITE){
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }

        } // end for

        /**
         * 处理完所有的事件后，idle协程yield，让 调度协程 Scheduler::run 重新检查是否有新任务要调度
         * 上面 triggleEvent 实际也只是把对应的fiber重新加入调度，要执行的话还是需要从idle协程退出。
         */
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        raw_ptr->yield();

    } // end while(true)

}

} // end namespace sylar