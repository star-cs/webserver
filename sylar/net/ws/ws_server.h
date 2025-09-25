#ifndef __SYLAR_HTTP_WS_SERVER_H__
#define __SYLAR_HTTP_WS_SERVER_H__

#include "sylar/net/tcp_server.h"
#include "ws_session.h"
#include "ws_servlet.h"

namespace sylar
{
namespace http
{

/**
 * @brief WebSocket服务器类
 * @details 继承自TcpServer，用于处理WebSocket连接和消息
 */
    class WSServer : public TcpServer
    {
    public:
        typedef std::shared_ptr<WSServer> ptr;

        /**
         * @brief 构造函数
         * @param worker 工作线程调度器
         * @param io_worker IO线程调度器
         * @param accept_worker 接收连接线程调度器
         */
        WSServer(sylar::IOManager *worker = sylar::IOManager::GetThis(),
                 sylar::IOManager *io_worker = sylar::IOManager::GetThis(),
                 sylar::IOManager *accept_worker = sylar::IOManager::GetThis());

        /**
         * @brief 获取WebSocket Servlet调度器
         * @return WSServletDispatch对象的智能指针
         */
        WSServletDispatch::ptr getWSServletDispatch() const { return m_dispatch; }
        
        /**
         * @brief 设置WebSocket Servlet调度器
         * @param v WSServletDispatch对象的智能指针
         */
        void setWSServletDispatch(WSServletDispatch::ptr v) { m_dispatch = v; }

    protected:
        /**
         * @brief 处理客户端连接
         * @details 重写父类方法，处理WebSocket握手和消息通信
         * @param client 客户端Socket对象
         */
        virtual void handleClient(Socket::ptr client) override;

    protected:
        WSServletDispatch::ptr m_dispatch; ///< WebSocket Servlet调度器
    };

} // namespace http
} // namespace sylar

#endif
