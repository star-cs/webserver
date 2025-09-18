#include "http2_session.h"
#include "sylar/core/log/log.h"
#include "http2_server.h"

namespace sylar::http2
{
/**
 * @brief 全局日志记录器，用于HTTP/2会话相关的日志记录
 */
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * @brief 构造函数，初始化HTTP/2会话
 * @param[in] sock 与客户端通信的套接字对象
 * @param[in] server HTTP/2服务器指针，用于访问服务器配置和功能
 * @note 调用父类Http2SocketStream的构造函数，并将is_client参数设为false表示这是服务器端会话
 */
Http2Session::Http2Session(Socket::ptr sock, Http2Server *server)
    : Http2SocketStream(sock, false), m_server(server)
{
    // 记录会话创建日志，包含套接字信息和对象地址
    SYLAR_LOG_INFO(g_logger) << "Http2Session::Http2Session sock=" << m_socket << " - " << this;
}

/**
 * @brief 析构函数，清理会话资源
 * @note 当会话结束时被调用，记录会话销毁的日志信息
 */
Http2Session::~Http2Session()
{
    // 记录会话销毁日志，包含套接字信息和对象地址
    SYLAR_LOG_INFO(g_logger) << "Http2Session ::~Http2Session sock=" << m_socket << " - " << this;
}

/**
 * @brief 处理HTTP请求的核心方法
 * @param[in] req HTTP请求对象，包含客户端发送的请求信息
 * @param[in] stream 对应的HTTP/2流对象，用于发送响应
 * @note 该方法实现了请求处理的完整流程：创建响应对象、设置响应头、通过Servlet分发处理请求、发送响应、删除流
 */
void Http2Session::handleRequest(http::HttpRequest::ptr req, Http2Stream::ptr stream)
{
    // 防止重复处理同一个请求
    if (stream->getHandleCount() > 0) {
        return;
    }
    
    // 增加流的处理计数，表示已开始处理该请求
    stream->addHandleCount();
    
    // 创建HTTP响应对象，版本与请求保持一致，false表示不是WebSocket响应
    http::HttpResponse::ptr rsp = std::make_shared<http::HttpResponse>(req->getVersion(), false);
    
    // 设置请求的流ID，便于后续关联请求和响应
    req->setStreamId(stream->getId());
    
    // 记录调试日志，输出完整的请求信息
    SYLAR_LOG_DEBUG(g_logger) << *req;
    
    // 设置响应头中的服务器名称
    rsp->setHeader("server", m_server->getName());
    
    // 通过服务器的Servlet调度器处理请求，生成响应
    m_server->getServletDispatch()->handle(req, rsp, shared_from_this());
    
    // 发送HTTP响应，参数：
    // 1. 响应对象
    // 2. true表示发送结束流标志
    // 3. true表示异步发送
    stream->sendResponse(rsp, true, true);
    
    // 从流管理器中删除该流
    delStream(stream->getId());
}

/**
 * @brief 流关闭回调函数，当HTTP/2流关闭时被调用
 * @param[in] stream 关闭的流对象
 * @return 返回上下文对象，用于异步操作管理，此处返回nullptr
 * @note 该方法处理流关闭后的逻辑，包括错误处理和请求调度
 */
AsyncSocketStream::Ctx::ptr Http2Session::onStreamClose(Http2Stream::ptr stream)
{
    // 获取与流关联的HTTP请求对象
    auto req = stream->getRequest();
    
    // 如果请求对象为空，说明接收请求失败
    if (!req) {
        // 记录请求接收失败的日志，包含错误信息和远程地址
        SYLAR_LOG_DEBUG(g_logger) << "Http2Session recv http request fail, errno=" << errno
                                  << " errstr=" << strerror(errno) << " - "
                                  << getRemoteAddressString();
        
        // 发送GOAWAY帧通知客户端协议错误，参数：
        // 1. 最后处理的流ID
        // 2. 错误码：PROTOCOL_ERROR表示协议错误
        // 3. 附加调试信息（空字符串）
        sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
        
        // 从流管理器中删除该流
        delStream(stream->getId());
        
        return nullptr;
    }
    
    // 如果流的处理计数为0，表示该请求尚未被处理
    if (stream->getHandleCount() == 0) {
        // 将会话自身转换为智能指针，避免回调时对象被销毁
        auto self = getSelf();
        
        // 在工作线程中调度请求处理，参数：
        // 1. 绑定到当前对象的handleRequest方法
        // 2. 传递请求对象和流对象
        m_worker->schedule(std::bind(&Http2Session::handleRequest, self, req, stream));
    }
    
    return nullptr;
}

/**
 * @brief 头部接收完成回调函数，当接收到完整的HTTP头部时被调用
 * @param[in] stream 对应的流对象
 * @return 返回上下文对象，用于异步操作管理，此处返回nullptr
 * @note 该方法在HTTP/2头部接收完成后被调用，当前实现为空
 */
AsyncSocketStream::Ctx::ptr Http2Session::onHeaderEnd(Http2Stream::ptr stream)
{
    return nullptr;
}

/**
 * @brief 获取当前会话对象的智能指针
 * @return 返回指向当前对象的智能指针
 * @note 该方法用于在异步回调中安全地访问当前对象，避免对象在回调执行前被销毁
 */
Http2Session::ptr Http2Session::getSelf()
{
    // 使用dynamic_pointer_cast将shared_from_this()返回的基类智能指针转换为派生类智能指针
    return std::dynamic_pointer_cast<Http2Session>(shared_from_this());
}

} // namespace sylar::http2