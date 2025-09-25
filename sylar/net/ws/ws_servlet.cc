#include "sylar/net/ws/ws_servlet.h"
#include "sylar/net/http/http_session.h"
#include "sylar/log.h"

namespace sylar
{
namespace http
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system"); ///< 静态日志对象

/**
 * @brief FunctionWSServlet构造函数
 * @param cb 消息处理回调函数
 * @param connect_cb 连接建立回调函数
 * @param close_cb 连接关闭回调函数
 */
FunctionWSServlet::FunctionWSServlet(callback cb, on_connect_cb connect_cb,
                                     on_close_cb close_cb)
    : WSServlet("FunctionWSServlet"), m_callback(cb),
      m_onConnect(connect_cb), m_onClose(close_cb)
{}

/**
 * @brief 处理WebSocket连接建立
 * @param header HTTP请求头
 * @param session WebSocket会话
 * @return 0表示成功，非0表示错误
 */
int32_t FunctionWSServlet::onConnect(HttpRequest::ptr header,
                                     WSSession::ptr session)
{
    if (m_onConnect) {
        return m_onConnect(header, session);
    }
    return 0;
}

/**
 * @brief 处理WebSocket连接关闭
 * @param header HTTP请求头
 * @param session WebSocket会话
 * @return 0表示成功，非0表示错误
 */
int32_t FunctionWSServlet::onClose(HttpRequest::ptr header,
                                   WSSession::ptr session)
{
    if (m_onClose) {
        return m_onClose(header, session);
    }
    return 0;
}

/**
 * @brief 处理WebSocket消息
 * @param header HTTP请求头
 * @param msg WebSocket消息
 * @param session WebSocket会话
 * @return 0表示成功，非0表示错误
 */
int32_t FunctionWSServlet::handle(HttpRequest::ptr header,
                                  WSFrameMessage::ptr msg,
                                  WSSession::ptr session)
{
    if (m_callback) {
        return m_callback(header, msg, session);
    }
    return 0;
}

/**
 * @brief WSServletDispatch构造函数
 */
WSServletDispatch::WSServletDispatch()
    : ServletDispatch("WSServletDispatch")
{}

/**
 * @brief 添加WebSocket Servlet
 * @param uri 路径
 * @param cb 消息处理回调函数
 * @param connect_cb 连接建立回调函数
 * @param close_cb 连接关闭回调函数
 */
void WSServletDispatch::addServlet(const std::string &uri,
                                  FunctionWSServlet::callback cb,
                                  FunctionWSServlet::on_connect_cb connect_cb,
                                  FunctionWSServlet::on_close_cb close_cb)
{
    // 创建新的FunctionWSServlet对象并添加到m_datas映射中
    ServletDispatch::addServlet(uri, std::make_shared<FunctionWSServlet>(cb, connect_cb, close_cb));
}

/**
 * @brief 添加WebSocket通配符Servlet
 * @param uri 通配符路径
 * @param cb 消息处理回调函数
 * @param connect_cb 连接建立回调函数
 * @param close_cb 连接关闭回调函数
 */
void WSServletDispatch::addGlobServlet(const std::string &uri,
                                     FunctionWSServlet::callback cb,
                                     FunctionWSServlet::on_connect_cb connect_cb,
                                     FunctionWSServlet::on_close_cb close_cb)
{
    // 创建新的FunctionWSServlet对象并添加到m_globs映射中
    ServletDispatch::addGlobServlet(uri, std::make_shared<FunctionWSServlet>(cb, connect_cb, close_cb));
}

/**
 * @brief 根据路径获取WebSocket Servlet
 * @param uri 路径
 * @return WSServlet对象的智能指针
 */
WSServlet::ptr WSServletDispatch::getWSServlet(const std::string &uri)
{
    // 查找精确匹配的Servlet
    auto slt = getMatchedServlet(uri);
    if (slt) {
        // 转换为WSServlet类型并返回
        return std::dynamic_pointer_cast<WSServlet>(slt->servlet);
    }
    return nullptr;
}

} // namespace http
} // namespace sylar
