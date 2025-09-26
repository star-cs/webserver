#include "ws_connection.h"

namespace sylar
{
namespace http
{

    /**
     * @brief WSConnection构造函数
     * @param sock Socket对象
     * @param owner 是否拥有Socket所有权
     */
    WSConnection::WSConnection(Socket::ptr sock, bool owner) : HttpConnection(sock, owner)
    {
    }

    /**
     * @brief 创建WebSocket连接（通过URL）
     * @details 解析URL并调用Create(Uri::ptr)方法创建连接
     * @param url WebSocket服务器URL
     * @param timeout_ms 超时时间（毫秒）
     * @param headers 自定义HTTP头
     * @return 包含HttpResult和WSConnection的pair
     */
    std::pair<HttpResult::ptr, WSConnection::ptr>
    WSConnection::Create(const std::string &url, uint64_t timeout_ms,
                         const std::map<std::string, std::string> &headers)
    {
        // 解析URL创建Uri对象
        Uri::ptr uri = Uri::Create(url);
        if (!uri) {
            return std::make_pair(std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_URL,
                                                               nullptr, "invalid url:" + url),
                                  nullptr);
        }
        return Create(uri, timeout_ms, headers);
    }

    /**
     * @brief 创建WebSocket连接（通过Uri对象）
     * @details 完整的WebSocket客户端连接建立流程
     * @param uri WebSocket服务器URI对象
     * @param timeout_ms 超时时间（毫秒）
     * @param headers 自定义HTTP头
     * @return 包含HttpResult和WSConnection的pair
     */
    std::pair<HttpResult::ptr, WSConnection::ptr>
    WSConnection::Create(Uri::ptr uri, uint64_t timeout_ms,
                         const std::map<std::string, std::string> &headers)
    {
        // 创建服务器地址
        Address::ptr addr = uri->createAddress();
        if (!addr) {
            return std::make_pair(std::make_shared<HttpResult>((int)HttpResult::Error::INVALID_HOST,
                                                               nullptr,
                                                               "invalid host: " + uri->getHost()),
                                  nullptr);
        }

        // 创建TCP Socket
        Socket::ptr sock = Socket::CreateTCP(addr);
        if (!sock) {
            return std::make_pair(
                std::make_shared<HttpResult>((int)HttpResult::Error::CREATE_SOCKET_ERROR, nullptr,
                                             "create socket fail: " + addr->toString()
                                                 + " errno=" + std::to_string(errno)
                                                 + " errstr=" + std::string(strerror(errno))),
                nullptr);
        }

        // 连接服务器
        if (!sock->connect(addr)) {
            return std::make_pair(std::make_shared<HttpResult>((int)HttpResult::Error::CONNECT_FAIL,
                                                               nullptr,
                                                               "connect fail: " + addr->toString()),
                                  nullptr);
        }

        // 设置接收超时
        sock->setRecvTimeout(timeout_ms);
        WSConnection::ptr conn = std::make_shared<WSConnection>(sock);

        // 创建HTTP请求
        HttpRequest::ptr req = std::make_shared<HttpRequest>();
        req->setPath(uri->getPath());
        req->setQuery(uri->getQuery());
        req->setFragment(uri->getFragment());
        req->setMethod(HttpMethod::GET);

        // 检查是否已包含必要的HTTP头
        bool has_host = false;
        bool has_conn = false;
        for (auto &i : headers) {
            if (strcasecmp(i.first.c_str(), "connection") == 0) {
                has_conn = true;
            } else if (!has_host && strcasecmp(i.first.c_str(), "host") == 0) {
                has_host = !i.second.empty();
            }

            req->setHeader(i.first, i.second);
        }

        // 设置WebSocket相关HTTP头
        req->setWebsocket(true);
        if (!has_conn) {
            req->setHeader("connection", "Upgrade");
        }
        req->setHeader("Upgrade", "websocket");
        req->setHeader("Sec-webSocket-Version", "13");
        req->setHeader("Sec-webSocket-Key", sylar::base64encode(random_string(16)));
        if (!has_host) {
            req->setHeader("Host", uri->getHost());
        }

        // 发送HTTP请求
        int rt = conn->sendRequest(req);
        if (rt == 0) {
            return std::make_pair(
                std::make_shared<HttpResult>((int)HttpResult::Error::SEND_CLOSE_BY_PEER, nullptr,
                                             "send request closed by peer: " + addr->toString()),
                nullptr);
        }
        if (rt < 0) {
            return std::make_pair(std::make_shared<HttpResult>(
                                      (int)HttpResult::Error::SEND_SOCKET_ERROR, nullptr,
                                      "send request socket error errno=" + std::to_string(errno)
                                          + " errstr=" + std::string(strerror(errno))),
                                  nullptr);
        }

        // 接收HTTP响应
        auto rsp = conn->recvResponse();
        if (!rsp) {
            return std::make_pair(
                std::make_shared<HttpResult>((int)HttpResult::Error::TIMEOUT, nullptr,
                                             "recv response timeout: " + addr->toString()
                                                 + " timeout_ms:" + std::to_string(timeout_ms)),
                nullptr);
        }

        // 验证WebSocket握手是否成功
        if (rsp->getStatus() != HttpStatus::SWITCHING_PROTOCOLS) {
            return std::make_pair(
                std::make_shared<HttpResult>(50, rsp, "not websocket server " + addr->toString()),
                nullptr);
        }

        // 握手成功，返回连接
        return std::make_pair(std::make_shared<HttpResult>((int)HttpResult::Error::OK, rsp, "ok"),
                              conn);
    }

    /**
     * @brief 接收WebSocket消息
     * @details 调用WSRecvMessage函数接收消息，设置为客户端模式
     * @return 成功返回WSFrameMessage对象，失败返回nullptr
     */
    WSFrameMessage::ptr WSConnection::recvMessage()
    {
        return WSRecvMessage(this, true);
    }

    /**
     * @brief 发送WebSocket消息
     * @details 调用WSSendMessage函数发送消息，设置为客户端模式
     * @param msg 要发送的消息
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSConnection::sendMessage(WSFrameMessage::ptr msg, bool fin)
    {
        return WSSendMessage(this, msg, true, fin);
    }

    /**
     * @brief 发送WebSocket消息
     * @details 创建消息对象并调用WSSendMessage函数发送，设置为客户端模式
     * @param msg 要发送的消息数据
     * @param opcode 操作码，默认为文本帧
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSConnection::sendMessage(const std::string &msg, int32_t opcode, bool fin)
    {
        return WSSendMessage(this, std::make_shared<WSFrameMessage>(opcode, msg), true, fin);
    }

    /**
     * @brief 发送PING帧
     * @details 调用WSPing函数发送PING帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSConnection::ping()
    {
        return WSPing(this);
    }

    /**
     * @brief 发送PONG帧
     * @details 调用WSPong函数发送PONG帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSConnection::pong()
    {
        return WSPong(this);
    }

} // namespace http
} // namespace sylar
