#include "ws_session.h"
#include "sylar/core/log/log.h"
#include "sylar/net/endian.h"
#include <string.h>

namespace sylar
{
namespace http
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system"); ///< 日志对象

    /**
     * @brief WebSocket消息最大长度配置
     * @details 默认值为32MB
     */
    sylar::ConfigVar<uint32_t>::ptr g_websocket_message_max_size = sylar::Config::Lookup(
        "websocket.message.max_size", (uint32_t)1024 * 1024 * 32, "websocket message max size");

    /**
     * @brief WSSession构造函数
     * @param sock Socket对象
     * @param owner 是否拥有Socket所有权
     */
    WSSession::WSSession(Socket::ptr sock, bool owner) : HttpSession(sock, owner)
    {
    }

    /**
     * @brief 处理WebSocket握手
     * @details 接收HTTP请求并验证WebSocket握手条件，完成后返回请求对象
     * @return 成功返回HttpRequest对象，失败返回nullptr
     */
    HttpRequest::ptr WSSession::handleShake()
    {
        HttpRequest::ptr req;
        do {
            req = recvRequest();
            if (!req) {
                SYLAR_LOG_INFO(g_logger) << "invalid http request";
                break;
            }
            // 检查Upgrade头部是否为websocket
            if (strcasecmp(req->getHeader("Upgrade").c_str(), "websocket")) {
                SYLAR_LOG_INFO(g_logger) << "http header Upgrade != websocket";
                break;
            }
            // 检查Connection头部是否为Upgrade
            if (strcasecmp(req->getHeader("Connection").c_str(), "Upgrade")) {
                SYLAR_LOG_INFO(g_logger) << "http header Connection != Upgrade";
                break;
            }
            // 检查WebSocket版本是否为13
            if (req->getHeaderAs<int>("Sec-webSocket-Version") != 13) {
                SYLAR_LOG_INFO(g_logger) << "http header Sec-webSocket-Version != 13";
                break;
            }
            // 获取并检查Sec-WebSocket-Key头部
            std::string key = req->getHeader("Sec-WebSocket-Key");
            if (key.empty()) {
                SYLAR_LOG_INFO(g_logger) << "http header Sec-WebSocket-Key = null";
                break;
            }

            // 计算Sec-WebSocket-Accept值
            std::string v = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            v = sylar::base64encode(sylar::sha1sum(v));
            req->setWebsocket(true);

            // 创建并设置响应
            auto rsp = req->createResponse();
            rsp->setStatus(HttpStatus::SWITCHING_PROTOCOLS);
            rsp->setWebsocket(true);
            rsp->setReason("Web Socket Protocol Handshake");
            rsp->setHeader("Upgrade", "websocket");
            rsp->setHeader("Connection", "Upgrade");
            rsp->setHeader("Sec-WebSocket-Accept", v);

            // 发送响应
            sendResponse(rsp);
            SYLAR_LOG_DEBUG(g_logger) << *req;
            SYLAR_LOG_DEBUG(g_logger) << *rsp;
            return req;
        } while (false);
        if (req) {
            SYLAR_LOG_INFO(g_logger) << *req;
        }
        return nullptr;
    }

    /**
     * @brief WSFrameMessage构造函数
     * @param opcode 操作码
     * @param data 消息数据
     */
    WSFrameMessage::WSFrameMessage(int opcode, const std::string &data)
        : m_opcode(opcode), m_data(data)
    {
    }

    /**
     * @brief 将帧头部信息转换为字符串
     * @return 包含帧头部信息的字符串
     */
    std::string WSFrameHead::toString() const
    {
        std::stringstream ss;
        ss << "[WSFrameHead fin=" << fin << " rsv1=" << rsv1 << " rsv2=" << rsv2 << " rsv3=" << rsv3
           << " opcode=" << opcode << " mask=" << mask << " payload=" << payload << "]";
        return ss.str();
    }

    /**
     * @brief 接收WebSocket消息
     * @details 调用WSRecvMessage函数接收消息，设置为服务端模式
     * @return 成功返回WSFrameMessage对象，失败返回nullptr
     */
    WSFrameMessage::ptr WSSession::recvMessage()
    {
        return WSRecvMessage(this, false);
    }

    /**
     * @brief 发送WebSocket消息
     * @details 调用WSSendMessage函数发送消息，设置为服务端模式
     * @param msg 要发送的消息
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSession::sendMessage(WSFrameMessage::ptr msg, bool fin)
    {
        return WSSendMessage(this, msg, false, fin);
    }

    /**
     * @brief 发送WebSocket消息
     * @details 创建消息对象并调用WSSendMessage函数发送，设置为服务端模式
     * @param msg 要发送的消息数据
     * @param opcode 操作码，默认为文本帧
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSession::sendMessage(const std::string &msg, int32_t opcode, bool fin)
    {
        return WSSendMessage(this, std::make_shared<WSFrameMessage>(opcode, msg), false, fin);
    }

    /**
     * @brief 发送PING帧
     * @details 调用WSPing函数发送PING帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSession::ping()
    {
        return WSPing(this);
    }

    /**
     * @brief 接收WebSocket消息
     * @details 从流中读取WebSocket帧，解析头部和负载数据
     * @param stream 数据流
     * @param client 是否为客户端模式
     * @return 成功返回WSFrameMessage对象，失败返回nullptr
     */
    WSFrameMessage::ptr WSRecvMessage(Stream *stream, bool client)
    {
        int opcode = 0;
        std::string data;
        int cur_len = 0;
        do {
            WSFrameHead ws_head;
            if (stream->readFixSize(&ws_head, sizeof(ws_head)) <= 0) {
                break;
            }
            SYLAR_LOG_DEBUG(g_logger) << "WSFrameHead " << ws_head.toString();

            // 处理PING帧
            if (ws_head.opcode == WSFrameHead::PING) {
                SYLAR_LOG_INFO(g_logger) << "PING";
                if (WSPong(stream) <= 0) {
                    break;
                }
            } 
            // 忽略PONG帧
            else if (ws_head.opcode == WSFrameHead::PONG) {
            } 
            // 处理数据帧
            else if (ws_head.opcode == WSFrameHead::CONTINUE
                       || ws_head.opcode == WSFrameHead::TEXT_FRAME
                       || ws_head.opcode == WSFrameHead::BIN_FRAME) {
                // 服务端要求客户端发送的帧必须有掩码
                if (!client && !ws_head.mask) {
                    SYLAR_LOG_INFO(g_logger) << "WSFrameHead mask != 1";
                    break;
                }
                
                // 解析负载长度
                uint64_t length = 0;
                if (ws_head.payload == 126) {
                    uint16_t len = 0;
                    if (stream->readFixSize(&len, sizeof(len)) <= 0) {
                        break;
                    }
                    length = sylar::byteswapOnLittleEndian(len);
                } else if (ws_head.payload == 127) {
                    uint64_t len = 0;
                    if (stream->readFixSize(&len, sizeof(len)) <= 0) {
                        break;
                    }
                    length = sylar::byteswapOnLittleEndian(len);
                } else {
                    length = ws_head.payload;
                }

                // 检查消息长度是否超过限制
                if ((cur_len + length) >= g_websocket_message_max_size->getValue()) {
                    SYLAR_LOG_WARN(g_logger)
                        << "WSFrameMessage length > " << g_websocket_message_max_size->getValue()
                        << " (" << (cur_len + length) << ")";
                    break;
                }

                // 读取掩码和数据
                char mask[4] = {0};
                if (ws_head.mask) {
                    if (stream->readFixSize(mask, sizeof(mask)) <= 0) {
                        break;
                    }
                }
                data.resize(cur_len + length);
                if (stream->readFixSize(&data[cur_len], length) <= 0) {
                    break;
                }
                
                // 应用掩码
                if (ws_head.mask) {
                    for (int i = 0; i < (int)length; ++i) {
                        data[cur_len + i] ^= mask[i % 4];
                    }
                }
                cur_len += length;

                // 设置操作码（仅第一帧）
                if (!opcode && ws_head.opcode != WSFrameHead::CONTINUE) {
                    opcode = ws_head.opcode;
                }

                // 如果是消息的最后一帧，返回完整消息
                if (ws_head.fin) {
                    SYLAR_LOG_DEBUG(g_logger) << data;
                    return std::make_shared<WSFrameMessage>(opcode, std::move(data));
                }
            } else {
                SYLAR_LOG_DEBUG(g_logger) << "invalid opcode=" << ws_head.opcode;
            }
        } while (true);
        stream->close();
        return nullptr;
    }

    /**
     * @brief 发送WebSocket消息
     * @details 将消息封装为WebSocket帧并发送
     * @param stream 数据流
     * @param msg 要发送的消息
     * @param client 是否为客户端模式
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSendMessage(Stream *stream, WSFrameMessage::ptr msg, bool client, bool fin)
    {
        do {
            WSFrameHead ws_head;
            memset(&ws_head, 0, sizeof(ws_head));
            ws_head.fin = fin;
            ws_head.opcode = msg->getOpcode();
            ws_head.mask = client;
            
            // 设置负载长度
            uint64_t size = msg->getData().size();
            if (size < 126) {
                ws_head.payload = size;
            } else if (size < 65536) {
                ws_head.payload = 126;
            } else {
                ws_head.payload = 127;
            }

            // 发送帧头部
            if (stream->writeFixSize(&ws_head, sizeof(ws_head)) <= 0) {
                break;
            }
            
            // 发送扩展长度
            if (ws_head.payload == 126) {
                uint16_t len = size;
                len = sylar::byteswapOnLittleEndian(len);
                if (stream->writeFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
            } else if (ws_head.payload == 127) {
                uint64_t len = sylar::byteswapOnLittleEndian(size);
                if (stream->writeFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
            }
            
            // 客户端模式需要添加掩码
            if (client) {
                char mask[4];
                uint32_t rand_value = rand();
                memcpy(mask, &rand_value, sizeof(mask));
                std::string &data = msg->getData();
                for (size_t i = 0; i < data.size(); ++i) {
                    data[i] ^= mask[i % 4];
                }

                if (stream->writeFixSize(mask, sizeof(mask)) <= 0) {
                    break;
                }
            }
            
            // 发送数据
            if (stream->writeFixSize(msg->getData().c_str(), size) <= 0) {
                break;
            }
            return size + sizeof(ws_head);
        } while (0);
        stream->close();
        return -1;
    }

    /**
     * @brief 发送PONG帧
     * @details 调用WSPong函数发送PONG帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSession::pong()
    {
        return WSPong(this);
    }

    /**
     * @brief 发送PING帧
     * @details 发送一个空的PING帧用于心跳检测
     * @param stream 数据流
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSPing(Stream *stream)
    {
        WSFrameHead ws_head;
        memset(&ws_head, 0, sizeof(ws_head));
        ws_head.fin = 1;
        ws_head.opcode = WSFrameHead::PING;
        int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
        if (v <= 0) {
            stream->close();
        }
        return v;
    }

    /**
     * @brief 发送PONG帧
     * @details 发送一个空的PONG帧作为对PING帧的响应
     * @param stream 数据流
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSPong(Stream *stream)
    {
        WSFrameHead ws_head;
        memset(&ws_head, 0, sizeof(ws_head));
        ws_head.fin = 1;
        ws_head.opcode = WSFrameHead::PONG;
        int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
        if (v <= 0) {
            stream->close();
        }
        return v;
    }

} // namespace http
} // namespace sylar
