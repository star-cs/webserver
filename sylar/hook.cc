#include "hook.h"
#include <dlfcn.h>
#include "sylar.h"
#include "fd_manager.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <cstdarg>
#include <sys/ioctl.h>
#include "config.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar
{
// 判断当前线程是否需要 HOOK
static thread_local bool t_hook_enable = false;
static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout = sylar::Config::Lookup(
    "tcp.connect.timeout", 5000, "tcp connect timeout"
);

#define HOOK_FUN(XX) \
        XX(sleep) \
        XX(usleep) \
        XX(nanosleep) \
        XX(socket) \
        XX(connect) \
        XX(accept) \
        XX(read) \
        XX(readv) \
        XX(recv) \
        XX(recvfrom) \
        XX(recvmsg) \
        XX(write) \
        XX(writev) \
        XX(send) \
        XX(sendto) \
        XX(sendmsg) \
        XX(close) \
        XX(fcntl) \
        XX(ioctl) \
        XX(getsockopt) \
        XX(setsockopt)
void hook_init(){
    static bool is_inited = false;
    if(is_inited){
        return;
    }
    //保存原函数：hook_init() 通过 dlsym(RTLD_NEXT, "sleep") 获取系统原版 sleep 函数的地址，保存到 sleep_f 指针
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX 
}

static uint64_t s_connect_timeout = -1;
struct _HookIniter {
    _HookIniter(){
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();
        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
            SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from " << old_value << " to " << new_value;
            s_connect_timeout = new_value;
        });

    }
};
    
static _HookIniter  s_hook_initer;

bool is_hook_enable(){
    return t_hook_enable;
}

void set_hook_enable(bool flag){
    t_hook_enable = flag;
}


} // namespace sylar

struct timer_info{
    int cancelled = 0;
};

/**
 * 重点 ！！！
 * 
 * 模板函数，通用的 read-write api hook 操作
 * 
 * Args&& 万能引用，根据传入实参自动推导
 * 
 * 这里Args，可能是左值，也可能是右值
 * 
 * std::forward 保持参数的原始值类别
 */ 
template<typename OriginFun, typename ... Args>
static ssize_t do_io(int fd, 
                    OriginFun fun, 
                    const char* hook_fun_name, 
                    uint32_t event, 
                    int timeout_so,     // 读 / 写 超时 宏标签
                    Args&&... args)
{
    if(!sylar::t_hook_enable){
        return fun(fd, std::forward<Args>(args)...);
    }
    // fd 添加到 FdMgr
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx){   // 如果ctx为nullptr
        return fun(fd, std::forward<Args>(args)...);
    }

    // 如果ctx关闭
    if(ctx->isClose()){
        errno = EBADF;
        return -1;
    }
    
    // 不是socket 或者是 用户设定了非阻塞
    // 用户设定了非阻塞，意味着自行处理非阻塞逻辑
    if(!ctx->isSocket() || ctx->getUserNonblock()){
        return fun(fd, std::forward<Args>(args)...);
    }

    // 接下来，socket情况
    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    SYLAR_LOG_DEBUG(g_logger) << hook_fun_name << " event " << event;
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    // SYLAR_LOG_DEBUG(g_logger) << "test " << n;
    while(n == -1 && errno == EINTR){    // 系统调用被信号中断
        n = fun(fd, std::forward<Args>(args)...);
    }
    if(n == -1 && errno == EAGAIN){      // 非阻塞操作无法立即完成
        SYLAR_LOG_DEBUG(g_logger) << "hook doing " << hook_fun_name << " event " << event;
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        if(to != (uint64_t)-1){
            // 添加一个条件定时器，如果 tinfo 还在意味着 fd还没等到event触发。
            // 到了超时时间，就直接取消事件。
            timer = iom->addConditionTimer(to , [iom, winfo, fd, event](){   
                auto it = winfo.lock();
                if(!it || it->cancelled){
                    return;
                }
                it->cancelled = ETIMEDOUT;   
                iom->cancelEvent(fd, (sylar::IOManager::Event)event);
            }, winfo);
        }
        // 正式 注册事件。
        // 没有传入cb，相当于把当前协程传入。当事件触发，会回到这个协程继续运行。 妙 ~ 
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)event); 
        if(rt != 0){    //添加失败
            SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
            if(timer){  //超时删除事件的定时器不需要了。
                timer->cancel();        // 删除定时器的权利 交给了定时器
            }
            return rt;
        } else{         //添加成功
            sylar::Fiber::GetThis()->yield();
            // 如果再次回到这里，
            // 两种情况：
            // 1. 超时之前，事件触发。
            // 2. 超时，事件条件被cancelEvent，直接触发。
            if(timer){
                timer->cancel();
            }
            if(tinfo->cancelled){   // 情况2 
                errno = tinfo->cancelled;
                return -1;
            }
            goto retry; // 情况1，event已经满足，重新操作 fd。
        }
    }
    return n;
}



extern "C" {
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

// sleep
unsigned int sleep(unsigned int seconds){
    if(!sylar::t_hook_enable){
        return sleep_f(seconds);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    /**
     * C++规定成员函数指针的类型包含类信息，即使存在继承关系，&IOManager::schedule 和 &Scheduler::schedule 属于不同类型。
     * 通过强制转换，使得类型系统接受子类对象iom调用基类成员函数的合法性。
     * 
     * schedule是模板函数
     * 子类继承的是模板的实例化版本，而非原始模板
     * 直接取地址会导致函数签名包含子类类型信息
     * 
     * std::bind 的类型安全机制
     * bind要求成员函数指针类型与对象类型严格匹配。当出现以下情况时必须转换：
     * 
     * 总结，当需要绑定 子类对象调用父类模板成员函数，父类函数需要强转成父类
     * (存在多继承或虚继承导致this指针偏移)
     * 
     * 或者
     * std::bind(&Scheduler::schedule, static_cast<Scheduler*>(iom), fiber, -1)
     * 
     */
    iom->addTimer(seconds * 1000 , std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))
                                                &sylar::IOManager::schedule, iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int usleep(useconds_t usec){
    if(!sylar::t_hook_enable){
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))
                                            &sylar::IOManager::schedule, iom , fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem){
    if(!sylar::t_hook_enable){
        return nanosleep_f(req, rem);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    iom->addTimer(timeout_ms, std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))
                                            &sylar::IOManager::schedule, iom , fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

// socket
int socket(int domain, int type, int protocol){
    if(!sylar::t_hook_enable){
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if(fd == -1){
        return -1;
    }
    sylar::FdMgr::GetInstance()->get(fd, true); // 添加 FdMgr
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms){
    if(!sylar::t_hook_enable){
        return connect_f(fd, addr, addrlen);
    }

    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);

    if(!ctx || ctx->isClose()){
        errno = EBADF;
        return -1;
    }

    if(!ctx->isSocket()){
        return connect_f(fd, addr, addrlen);
    }

    if(ctx->getUserNonblock()){
        return connect_f(fd, addr, addrlen);
    }

    /**
     * 非阻塞connect调用会立即返回EINPROGRESS错误码，表示连接正在建立
     * 此时不需要也不能重复调用connect，否则可能触发EALREADY错误
     * 通过等待WRITE事件即可判断连接是否建立完成
     * 
     */
    int n = connect_f(fd, addr, addrlen);
    if(n == 0){
        return 0;
    }else if(n != -1 || errno != EINPROGRESS){
        return n;
    }

    sylar::IOManager* iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if(timeout_ms != (uint64_t)-1){
        iom->addConditionTimer(timeout_ms, [winfo, fd, iom](){
            auto it = winfo.lock();
            if(!it || it->cancelled){
                return;
            }
            it->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, sylar::IOManager::Event::WRITE);
        }, winfo);
    }

    int rt = iom->addEvent(fd, sylar::IOManager::Event::WRITE);
    if(rt == 0){
        sylar::Fiber::GetThis()->yield();
        if(timer){
            timer->cancel();
        }
        if(tinfo->cancelled){
            errno = tinfo->cancelled;
            return -1;
        }
    }else{
        if(timer) {
            timer->cancel();
        }
        SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)){
        return -1;
    }

    if(!error){
        return 0;
    }else{
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen){
    int fd = do_io(s, accept_f, "accept", sylar::IOManager::Event::READ, SO_RCVTIMEO, addr, addrlen);

    if(fd != -1){
        sylar::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

// read
ssize_t read(int fd, void *buf, size_t count){
    return do_io(fd, read_f, "read", sylar::IOManager::Event::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt){
    return do_io(fd, readv_f, "readv", sylar::IOManager::Event::READ, SO_RCVTIMEO, iov, iovcnt);
}  

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::Event::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen){
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::Event::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags){
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::Event::READ, SO_RCVTIMEO, msg, flags);
}

//write
ssize_t write(int fd, const void *buf, size_t count){
    return do_io(fd, write_f, "write", sylar::IOManager::Event::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt){
    return do_io(fd, writev_f, "writev", sylar::IOManager::Event::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags){
    return do_io(s, send_f, "send", sylar::IOManager::Event::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen){
    return do_io(s, sendto_f, "sendto", sylar::IOManager::Event::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags){
    return do_io(s, sendmsg_f, "sendmsg", sylar::IOManager::Event::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd){
    if(!sylar::t_hook_enable){
        return close_f(fd);
    }
    
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(ctx){
        auto iom = sylar::IOManager::GetThis();
        if(iom){ // 删除
            iom->cancelAll(fd);
        }
        sylar::FdMgr::GetInstance()->del(fd); // 从 FdMgr 中删除
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ... /* arg */ ){
    va_list va;
    va_start(va, cmd);
    switch(cmd){
        case F_SETFL:
            {
                int arg = va_arg(va, int);
                va_end(va);
                // 获取 FdCtx
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()){
                    return fcntl_f(fd, cmd, arg);
                }
                // 检查args，用户是否设置 非阻塞。
                // FdCtx里的m_userNonblock，这里设置。
                ctx->setUserNonblock(arg & O_NONBLOCK);

                // 要执行了，所以把 hook 非阻塞直接加上。
                if(ctx->getSysNonblock()){
                    arg |= O_NONBLOCK;
                }else{
                    arg &= ~O_NONBLOCK;
                }

                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()){
                    return arg;
                }
                // 设置 用户是否判断 非阻塞。
                if(ctx->getUserNonblock()){
                    return arg | O_NONBLOCK;
                }else{ // 如果之前就没有，那么需要恢复默认。（Hook默认加上了非阻塞）
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
    #ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
    #endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
    #ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
    #endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
             break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}


int ioctl(int d, unsigned long int request, ...){
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);
    // FIONBIO（设置非阻塞模式）
    if(FIONBIO == request){ // 主要用于处理文件描述符的非阻塞模式设置
        bool user_nonblock = !!*(int*)arg;   // 将参数转换为布尔值
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(d);
        if(!ctx || ctx->isClose() || !ctx->isSocket()){
            return ioctl_f(d, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d, request, arg);
}


int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen){
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}


int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    if(!sylar::t_hook_enable){
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET){
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO){   // 超时事件设置
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx){
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec* 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}


}