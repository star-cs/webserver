#pragma once
#include "sylar/net/socket.h"
#include "sylar/net/streams/async_socket_stream.h"
#include "sylar/net/http/http.h"
#include "http2_socket_stream.h"

namespace sylar::http2
{

class Http2Server;

/**
 * @brief HTTP/2 会话类
 * @details 继承自 Http2SocketStream，专门处理 HTTP/2 协议的服务器端会话管理
 * 负责处理客户端连接、请求分发和响应生成等核心功能
 */
class Http2Session : public Http2SocketStream
{
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<Http2Session> ptr;

    /**
     * @brief 构造函数
     * @param[in] sock 套接字对象，用于与客户端通信
     * @param[in] server HTTP/2 服务器指针，用于访问服务器相关功能
     */
    Http2Session(Socket::ptr sock, Http2Server *server);

    /**
     * @brief 析构函数
     * @details 清理会话资源，关闭连接等
     */
    ~Http2Session();

protected:
    /**
     * @brief 获取自身的智能指针
     * @return 返回指向当前对象的智能指针
     * @note 用于在回调函数中安全地访问当前对象
     */
    Http2Session::ptr getSelf();

protected:
    /**
     * @brief 处理 HTTP 请求
     * @param[in] req HTTP 请求对象，包含客户端发送的请求信息
     * @param[in] stream 对应的 HTTP/2 流对象，用于发送响应
     * @note 这是一个虚函数，子类可以重写以提供自定义的请求处理逻辑
     */
    virtual void handleRequest(http::HttpRequest::ptr req, Http2Stream::ptr stream);

    /**
     * @brief 流关闭回调函数
     * @param[in] stream 关闭的流对象
     * @return 返回上下文对象，用于异步操作管理
     * @note 当 HTTP/2 流关闭时被调用，用于清理资源等
     */
    sylar::AsyncSocketStream::Ctx::ptr onStreamClose(Http2Stream::ptr stream) override;

    /**
     * @brief 头部接收完成回调函数
     * @param[in] stream 对应的流对象
     * @return 返回上下文对象，用于异步操作管理
     * @note 当接收到完整的 HTTP 头部时被调用，通常用于触发请求处理
     */
    sylar::AsyncSocketStream::Ctx::ptr onHeaderEnd(Http2Stream::ptr stream) override;

protected:
    Http2Server *m_server; ///< 指向所属的 HTTP/2 服务器对象
};

} // namespace sylar::http2