#include "ws_server.h"
#include "sylar/core/log/log.h"

namespace sylar
{
namespace http
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system"); ///< 日志对象

    /**
     * @brief WSServer构造函数
     * @details 初始化WebSocket服务器，创建Servlet调度器并设置服务器类型
     * @param worker 工作线程调度器
     * @param io_worker IO线程调度器
     * @param accept_worker 接收连接线程调度器
     */
    WSServer::WSServer(sylar::IOManager *worker, sylar::IOManager *io_worker,
                       sylar::IOManager *accept_worker)
        : TcpServer(worker, io_worker, accept_worker)
    {
        m_dispatch = std::make_shared<WSServletDispatch>();
        m_type = "websocket_server";
    }

    /**
     * @brief 处理客户端连接
     * @details 重写父类方法，实现WebSocket连接的完整生命周期管理：
     *          1. 接收并处理WebSocket握手
     *          2. 根据路径查找对应的Servlet
     *          3. 调用Servlet的onConnect方法
     *          4. 循环接收和处理WebSocket消息
     *          5. 连接关闭时调用Servlet的onClose方法
     * @param client 客户端Socket对象
     */
    void WSServer::handleClient(Socket::ptr client)
    {
        SYLAR_LOG_DEBUG(g_logger) << "handleClient " << *client;
        WSSession::ptr session = std::make_shared<WSSession>(client);
        do {
            // 处理WebSocket握手
            HttpRequest::ptr header = session->handleShake();
            if (!header) {
                SYLAR_LOG_DEBUG(g_logger) << "handleShake error";
                break;
            }

            // 根据路径查找对应的Servlet
            WSServlet::ptr servlet = m_dispatch->getWSServlet(header->getPath());
            if (!servlet) {
                SYLAR_LOG_DEBUG(g_logger) << "no match WSServlet";
                break;
            }

            // 调用Servlet的onConnect方法
            int rt = servlet->onConnect(header, session);
            if (rt) {
                SYLAR_LOG_DEBUG(g_logger) << "onConnect return " << rt;
                break;
            }

            // 循环接收和处理WebSocket消息
            while (true) {
                auto msg = session->recvMessage();
                if (!msg) {
                    break;
                }
                rt = servlet->handle(header, msg, session);
                if (rt) {
                    SYLAR_LOG_DEBUG(g_logger) << "handle return " << rt;
                    break;
                }
            }

            // 连接关闭时调用Servlet的onClose方法
            servlet->onClose(header, session);
        } while (0);
        session->close();
    }

} // namespace http
} // namespace sylar
