#ifndef __SYLAR_HTTP_WS_CONNECTION_H__
#define __SYLAR_HTTP_WS_CONNECTION_H__

#include "sylar/net/http/http_connection.h"
#include "ws_session.h"

namespace sylar
{
namespace http
{

/**
 * @brief WebSocket连接类
 * @details 继承自HttpConnection，用于WebSocket客户端连接
 */
    class WSConnection : public HttpConnection
    {
    public:
        typedef std::shared_ptr<WSConnection> ptr;
        
        /**
         * @brief 构造函数
         * @param sock Socket对象
         * @param owner 是否拥有Socket所有权
         */
        WSConnection(Socket::ptr sock, bool owner = true);
        
        /**
         * @brief 创建WebSocket连接（通过URL）
         * @param url WebSocket服务器URL
         * @param timeout_ms 超时时间（毫秒）
         * @param headers 自定义HTTP头
         * @return 包含HttpResult和WSConnection的pair
         */
        static std::pair<HttpResult::ptr, WSConnection::ptr>
        Create(const std::string &url, uint64_t timeout_ms,
               const std::map<std::string, std::string> &headers = {});
        
        /**
         * @brief 创建WebSocket连接（通过Uri对象）
         * @param uri WebSocket服务器URI对象
         * @param timeout_ms 超时时间（毫秒）
         * @param headers 自定义HTTP头
         * @return 包含HttpResult和WSConnection的pair
         */
        static std::pair<HttpResult::ptr, WSConnection::ptr>
        Create(Uri::ptr uri, uint64_t timeout_ms,
               const std::map<std::string, std::string> &headers = {});
        
        /**
         * @brief 接收WebSocket消息
         * @return 成功返回WSFrameMessage对象，失败返回nullptr
         */
        WSFrameMessage::ptr recvMessage();
        
        /**
         * @brief 发送WebSocket消息
         * @param msg 要发送的消息
         * @param fin 是否为消息的最后一帧
         * @return 成功返回发送的字节数，失败返回-1
         */
        int32_t sendMessage(WSFrameMessage::ptr msg, bool fin = true);
        
        /**
         * @brief 发送WebSocket消息
         * @param msg 要发送的消息数据
         * @param opcode 操作码，默认为文本帧
         * @param fin 是否为消息的最后一帧
         * @return 成功返回发送的字节数，失败返回-1
         */
        int32_t sendMessage(const std::string &msg, int32_t opcode = WSFrameHead::TEXT_FRAME,
                            bool fin = true);
        
        /**
         * @brief 发送PING帧
         * @return 成功返回发送的字节数，失败返回-1
         */
        int32_t ping();
        
        /**
         * @brief 发送PONG帧
         * @return 成功返回发送的字节数，失败返回-1
         */
        int32_t pong();
    };

} // namespace http
} // namespace sylar

#endif
