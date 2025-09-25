#ifndef __SYLAR_HTTP_WS_SESSION_H__
#define __SYLAR_HTTP_WS_SESSION_H__

#include "sylar/core/config/config.h"
#include "sylar/net/http/http_session.h"
#include <stdint.h>

namespace sylar
{
namespace http
{

/**
 * @brief WebSocket帧头部结构
 * @details 按照WebSocket协议规范定义的帧格式，使用1字节对齐
 */
#pragma pack(1)
    struct WSFrameHead {
        /**
         * @brief WebSocket操作码枚举
         * @details 定义了WebSocket协议中支持的各种操作类型
         */
        enum OPCODE {
            CONTINUE = 0,        ///< 数据分片帧，用于多帧消息的后续部分
            TEXT_FRAME = 1,      ///< 文本数据帧，包含UTF-8编码的文本
            BIN_FRAME = 2,       ///< 二进制数据帧，包含任意二进制数据
            CLOSE = 8,           ///< 关闭连接帧，通知对方关闭连接
            PING = 0x9,          ///< PING帧，用于心跳检测
            PONG = 0xA           ///< PONG帧，对PING帧的响应
        };
        
        uint32_t opcode : 4;    ///< 操作码，指定帧的类型
        bool rsv3 : 1;          ///< 保留位3，必须为0
        bool rsv2 : 1;          ///< 保留位2，必须为0
        bool rsv1 : 1;          ///< 保留位1，必须为0
        bool fin : 1;           ///< 结束标志，1表示此帧是消息的最后一帧
        uint32_t payload : 7;   ///< 负载长度的低7位
        bool mask : 1;          ///< 掩码标志，客户端发送的帧必须为1

        /**
         * @brief 将帧头部信息转换为字符串
         * @return 包含帧头部信息的字符串
         */
        std::string toString() const;
    };
#pragma pack()

/**
 * @brief WebSocket消息类
 * @details 封装WebSocket消息，包含操作码和数据内容
 */
    class WSFrameMessage
    {
    public:
        typedef std::shared_ptr<WSFrameMessage> ptr;
        
        /**
         * @brief 构造函数
         * @param opcode 操作码
         * @param data 消息数据
         */
        WSFrameMessage(int opcode = 0, const std::string &data = "");

        /**
         * @brief 获取操作码
         * @return 操作码值
         */
        int getOpcode() const { return m_opcode; }
        
        /**
         * @brief 设置操作码
         * @param v 操作码值
         */
        void setOpcode(int v) { m_opcode = v; }

        /**
         * @brief 获取消息数据（只读）
         * @return 消息数据的常量引用
         */
        const std::string &getData() const { return m_data; }
        
        /**
         * @brief 获取消息数据（可修改）
         * @return 消息数据的引用
         */
        std::string &getData() { return m_data; }
        
        /**
         * @brief 设置消息数据
         * @param v 要设置的消息数据
         */
        void setData(const std::string &v) { m_data = v; }

    private:
        int m_opcode;           ///< 消息操作码
        std::string m_data;     ///< 消息数据内容
    };

/**
 * @brief WebSocket会话类
 * @details 继承自HttpSession，用于处理WebSocket协议通信
 */
    class WSSession : public HttpSession
    {
    public:
        typedef std::shared_ptr<WSSession> ptr;
        
        /**
         * @brief 构造函数
         * @param sock Socket对象
         * @param owner 是否拥有Socket所有权
         */
        WSSession(Socket::ptr sock, bool owner = true);

        /**
         * @brief 处理WebSocket握手
         * @return 成功返回HttpRequest对象，失败返回nullptr
         */
        HttpRequest::ptr handleShake();

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

    private:
        /**
         * @brief 处理服务端握手
         * @return 成功返回true，失败返回false
         */
        bool handleServerShake();
        
        /**
         * @brief 处理客户端握手
         * @return 成功返回true，失败返回false
         */
        bool handleClientShake();
    };

    extern sylar::ConfigVar<uint32_t>::ptr g_websocket_message_max_size; ///< WebSocket消息最大长度配置
    
    /**
     * @brief 接收WebSocket消息
     * @param stream 数据流
     * @param client 是否为客户端模式
     * @return 成功返回WSFrameMessage对象，失败返回nullptr
     */
    WSFrameMessage::ptr WSRecvMessage(Stream *stream, bool client);
    
    /**
     * @brief 发送WebSocket消息
     * @param stream 数据流
     * @param msg 要发送的消息
     * @param client 是否为客户端模式
     * @param fin 是否为消息的最后一帧
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSSendMessage(Stream *stream, WSFrameMessage::ptr msg, bool client, bool fin);
    
    /**
     * @brief 发送PING帧
     * @param stream 数据流
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSPing(Stream *stream);
    
    /**
     * @brief 发送PONG帧
     * @param stream 数据流
     * @return 成功返回发送的字节数，失败返回-1
     */
    int32_t WSPong(Stream *stream);

} // namespace http
} // namespace sylar

#endif
