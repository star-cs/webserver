#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "fd_manager.h"
#include "sylar/core/log/log.h"
#include "sylar/core/util/util.h"
#include "hook.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar
{

FdCtx::FdCtx(int fd)
    : m_isInit(false), m_isSocket(false), m_sysNonblock(false), m_userNonblock(false),
      m_isClosed(false), m_fd(fd), m_recvTimeout(-1), m_sendTimeout(-1)
{
    init();
}

FdCtx::~FdCtx()
{
}

bool FdCtx::init()
{
    if (m_isInit) {
        return true;
    }
    m_recvTimeout = -1;
    m_sendTimeout = -1;
    /**
         * stat族
         * 
         * 获取fd信息
         * int fstat(int filedes, struct stat *buf);
         * 返回值: 执行成功则返回0，失败返回-1，错误代码存于errno
         * 
         * 查看 stat 里的 st_mode 属性
         * 
         * 常用宏
            S_ISLNK(st_mode):是否是一个连接.
            S_ISREG是否是一个常规文件.
            S_ISDIR是否是一个目录
            S_ISCHR是否是一个字符设备.
            S_ISBLK是否是一个块设备
            S_ISFIFO是否是一个FIFO文件.
            S_ISSOCK是否是一个SOCKET文件. 
         */
    struct stat fd_stat;
    if (-1 == fstat(m_fd, &fd_stat)) {
        m_isInit = false;
        m_isSocket = false;
    } else {
        m_isInit = true;
        m_isSocket = S_ISSOCK(fd_stat.st_mode);
    }

    if (m_isSocket) {
        /**
             * 文件描述符标志（F_GETFD/F_SETFD）控制进程级行为
             * 文件状态标志（F_GETFL/F_SETFL）控制文件访问方式
             * 标准规定O_NONBLOCK属于文件状态标志，必须通过F_GETFL/F_SETFL操作
             */
        // 这里不能使用fcntl，因为hook fcntl会再次 FdManager::get()
        int flags = fcntl_f(m_fd, F_GETFL, 0);
        if (!(flags & O_NONBLOCK)) {
            // 同上
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK); // 设置非阻塞。
        }
        m_sysNonblock = true;
    } else {
        m_sysNonblock = false;
    }

    m_userNonblock = false;
    m_isClosed = false;
    return m_isInit;
}

/**
     * SO_RCVTIMEO：用来设置socket接收数据的超时时间
     * SO_SNDTIMEO： 用来设置socket发送数据的超时时间；
     */

void FdCtx::setTimeout(int type, uint64_t v)
{
    if (type == SO_RCVTIMEO) {
        m_recvTimeout = v;
    } else {
        m_sendTimeout = v;
    }
}

int FdCtx::getTimeout(int type)
{
    if (type == SO_RCVTIMEO) {
        return m_recvTimeout;
    } else {
        return m_sendTimeout;
    }
}

FdManager::FdManager()
{
    m_datas.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create)
{
    if (fd == -1) {
        return nullptr;
    }

    RWMutexType::ReadLock lock(m_mutex);
    /**
         * 总结：
         * 1. auto_create = false 
         *      有空间返回 m_datas[fd]，但不保证一定是有效的（可能是init()->fcntl()->get()，此时fdCtx还没创建好；也可能是创建好的）。
         *      无空间返回 nullptr。
         * 2. auto_create = true
         *      初始化 FdCtx，socket（Hook）会用。
         */
    if ((int)m_datas.size() <= fd) {
        if (auto_create == false) {
            // 没空间，并且不创建 退出
            return nullptr;
        }
    } else {
        // 已经有，或者 不需要创建
        // 两种情况：
        // 1. 正常获取 FdCtx
        // 2. FdCtx还没创建完，init()里调用 fcntl（hook），fcntl里又调用了 get，这个时候直接返回 nullptr。
        if (m_datas[fd] || !auto_create) {
            return m_datas[fd];
        }
    }
    lock.unlock();

    RWMutexType::WriteLock lock2(m_mutex);
    FdCtx::ptr fd_ctx(new FdCtx(fd)); //创建
    if (fd >= (int)m_datas.size()) {
        m_datas.resize(fd * 1.5);
    }
    m_datas[fd] = fd_ctx;
    return fd_ctx;
}

void FdManager::del(int fd)
{
    RWMutexType::WriteLock lock(m_mutex);
    if ((int)m_datas.size() <= false) {
        return;
    }
    m_datas[fd].reset(); //里面存的是 FdCtx::ptr，reset即可。
}
} // namespace sylar