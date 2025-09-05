#ifndef __SYLAR_FD_MANAGER_H__
#define __SYLAR_FD_MANAGER_H__

#include <vector>
#include <memory>
#include "mutex.h"
#include "sylar/core/common/singleton.h"

namespace sylar
{

class FdCtx : public std::enable_shared_from_this<FdCtx>
{
public:
    typedef std::shared_ptr<FdCtx> ptr;
    FdCtx(int fd);
    ~FdCtx();

    bool init();

    bool isInit() const { return m_isInit; }

    bool isSocket() const { return m_isSocket; }

    bool isClose() const { return m_isClosed; }

    void setUserNonblock(bool v) { m_userNonblock = v; }

    bool getUserNonblock() const { return m_userNonblock; }

    void setSysNonblock(bool v) { m_sysNonblock = v; }

    bool getSysNonblock() const { return m_sysNonblock; }

    /**
     * @brief 设置超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @param[in] v 时间毫秒
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief 获取超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @return 超时时间毫秒
     */
    int getTimeout(int type);

private:
    bool m_isInit : 1;
    bool m_isSocket : 1;
    bool m_sysNonblock : 1;  // 是否 hook 非阻塞
    bool m_userNonblock : 1; // 是否 用户主动设置 非阻塞
    bool m_isClosed : 1;
    int m_fd;
    uint64_t m_recvTimeout; // 读超时时间毫秒
    uint64_t m_sendTimeout; // 写超时时间毫秒
};

class FdManager
{
public:
    typedef RWMutex RWMutexType;

    FdManager();

    /**
     * @brief 获取/创建文件句柄类FdCtx
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建
     * @return 返回对应文件句柄类FdCtx::ptr
     */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类
     * @param[in] fd 文件句柄
     */
    void del(int fd);

private:
    RWMutexType m_mutex;
    std::vector<FdCtx::ptr> m_datas;
};

// 文件句柄管理类 单例化
typedef Singleton<FdManager> FdMgr;

} // namespace sylar

#endif