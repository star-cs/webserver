#ifndef __HTTP_WS_SERVLET_H__
#define __HTTP_WS_SERVLET_H__

#include "sylar/net/ws/ws_session.h"
#include "sylar/core/thread.h"
#include "sylar/net/http/servlet.h"

namespace sylar
{
namespace http
{

/**
 * @brief WebSocket Servlet基类
 * @details 提供处理WebSocket连接和消息的接口
 */
    class WSServlet : public Servlet
    {
    public:
        typedef std::shared_ptr<WSServlet> ptr;
        
        /**
         * @brief 构造函数
         * @param name Servlet名称
         */
        WSServlet(const std::string &name) : Servlet(name) {}
        
        /**
         * @brief 虚析构函数
         */
        virtual ~WSServlet() {}

        /**
         * @brief 重写父类handle方法（HTTP）
         * @details WebSocket不使用此方法，返回0
         */
        virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                               sylar::http::HttpResponse::ptr response,
                               sylar::SocketStream::ptr session) override
        {
            return 0;
        }

        /**
         * @brief 处理WebSocket连接建立
         * @param header HTTP请求头
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t onConnect(sylar::http::HttpRequest::ptr header,
                                  sylar::http::WSSession::ptr session) = 0;
        
        /**
         * @brief 处理WebSocket连接关闭
         * @param header HTTP请求头
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t onClose(sylar::http::HttpRequest::ptr header,
                                sylar::http::WSSession::ptr session) = 0;
        
        /**
         * @brief 处理WebSocket消息
         * @param header HTTP请求头
         * @param msg WebSocket消息
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t handle(sylar::http::HttpRequest::ptr header,
                               sylar::http::WSFrameMessage::ptr msg,
                               sylar::http::WSSession::ptr session) = 0;
        
        /**
         * @brief 获取Servlet名称
         * @return Servlet名称
         */
        const std::string &getName() const { return m_name; }

    protected:
        std::string m_name; ///< Servlet名称
    };

/**
 * @brief 基于函数的WebSocket Servlet
 * @details 使用回调函数处理WebSocket连接和消息
 */
    class FunctionWSServlet : public WSServlet
    {
    public:
        typedef std::shared_ptr<FunctionWSServlet> ptr;
        typedef std::function<int32_t(sylar::http::HttpRequest::ptr header,
                                      sylar::http::WSSession::ptr session)> on_connect_cb; ///< 连接建立回调
        typedef std::function<int32_t(sylar::http::HttpRequest::ptr header,
                                      sylar::http::WSSession::ptr session)> on_close_cb;   ///< 连接关闭回调
        typedef std::function<int32_t(sylar::http::HttpRequest::ptr header,
                                      sylar::http::WSFrameMessage::ptr msg,
                                      sylar::http::WSSession::ptr session)> callback;      ///< 消息处理回调

        /**
         * @brief 构造函数
         * @param cb 消息处理回调函数
         * @param connect_cb 连接建立回调函数
         * @param close_cb 连接关闭回调函数
         */
        FunctionWSServlet(callback cb, on_connect_cb connect_cb = nullptr,
                          on_close_cb close_cb = nullptr);

        /**
         * @brief 处理WebSocket连接建立
         * @param header HTTP请求头
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t onConnect(sylar::http::HttpRequest::ptr header,
                                  sylar::http::WSSession::ptr session) override;
        
        /**
         * @brief 处理WebSocket连接关闭
         * @param header HTTP请求头
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t onClose(sylar::http::HttpRequest::ptr header,
                                sylar::http::WSSession::ptr session) override;
        
        /**
         * @brief 处理WebSocket消息
         * @param header HTTP请求头
         * @param msg WebSocket消息
         * @param session WebSocket会话
         * @return 0表示成功，非0表示错误
         */
        virtual int32_t handle(sylar::http::HttpRequest::ptr header,
                               sylar::http::WSFrameMessage::ptr msg,
                               sylar::http::WSSession::ptr session) override;

    protected:
        callback m_callback;       ///< 消息处理回调函数
        on_connect_cb m_onConnect; ///< 连接建立回调函数
        on_close_cb m_onClose;     ///< 连接关闭回调函数
    };

/**
 * @brief WebSocket Servlet调度器
 * @details 根据URL路径匹配对应的WebSocket Servlet
 */
    class WSServletDispatch : public ServletDispatch
    {
    public:
        typedef std::shared_ptr<WSServletDispatch> ptr;
        typedef RWMutex RWMutexType; ///< 读写锁类型

        /**
         * @brief 构造函数
         */
        WSServletDispatch();
        
        /**
         * @brief 添加WebSocket Servlet
         * @param uri 路径
         * @param cb 消息处理回调函数
         * @param connect_cb 连接建立回调函数
         * @param close_cb 连接关闭回调函数
         */
        void addServlet(const std::string &uri, FunctionWSServlet::callback cb,
                        FunctionWSServlet::on_connect_cb connect_cb = nullptr,
                        FunctionWSServlet::on_close_cb close_cb = nullptr);
        
        /**
         * @brief 添加WebSocket通配符Servlet
         * @param uri 通配符路径
         * @param cb 消息处理回调函数
         * @param connect_cb 连接建立回调函数
         * @param close_cb 连接关闭回调函数
         */
        void addGlobServlet(const std::string &uri, FunctionWSServlet::callback cb,
                            FunctionWSServlet::on_connect_cb connect_cb = nullptr,
                            FunctionWSServlet::on_close_cb close_cb = nullptr);
        
        /**
         * @brief 根据路径获取WebSocket Servlet
         * @param uri 路径
         * @return WSServlet对象的智能指针
         */
        WSServlet::ptr getWSServlet(const std::string &uri);
    };

} // namespace http
} // namespace sylar

#endif
