#pragma once

#include "sylar/core/mutex.h"
#include "sylar/net/streams/async_socket_stream.h"
#include "frame.h"
#include "hpack.h"
#include "http2_stream.h"
#include "sylar/net/http/http_connection.h"
#include "http2_protocol.h"

namespace sylar
{
namespace http2
{

    /**
     * @brief HTTP/2 套接字流类
     * @details 继承自 AsyncSocketStream，实现 HTTP/2 协议的套接字流处理，负责 HTTP/2
     * 帧的发送、接收和处理 管理 HTTP/2 连接中的流、HPack 压缩表、流量控制窗口等核心功能
     */
    class Http2SocketStream : public AsyncSocketStream
    {
    public:
        friend class http2::Http2Stream;                ///< 允许 Http2Stream 访问私有成员
        typedef std::shared_ptr<Http2SocketStream> ptr; ///< 智能指针类型定义
        typedef sylar::RWSpinlock RWMutexType;          ///< 读写锁类型定义

        /**
         * @brief 构造函数
         * @param sock 套接字指针
         * @param client 是否为客户端模式
         */
        Http2SocketStream(Socket::ptr sock, bool client);

        /**
         * @brief 析构函数
         */
        ~Http2SocketStream();

        /**
         * @brief 发送 HTTP/2 帧
         * @param frame 要发送的帧指针
         * @param async 是否异步发送
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendFrame(Frame::ptr frame, bool async);

        /**
         * @brief 发送数据到指定流
         * @param stream 目标流指针
         * @param data 要发送的数据
         * @param async 是否异步发送
         * @param end_stream 是否结束流
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendData(Http2Stream::ptr stream, const std::string &data, bool async,
                         bool end_stream);

        /**
         * @brief 处理客户端握手
         * @return 是否握手成功
         */
        bool handleShakeClient();

        /**
         * @brief 处理服务器握手
         * @return 是否握手成功
         */
        bool handleShakeServer();

        /**
         * @brief 发送 HTTP 请求
         * @param req HTTP 请求对象
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP 响应结果
         */
        http::HttpResult::ptr request(http::HttpRequest::ptr req, uint64_t timeout_ms);

        /**
         * @brief 处理接收的 SETTINGS 帧
         * @param frame 接收到的帧对象
         */
        void handleRecvSetting(Frame::ptr frame);

        /**
         * @brief 处理发送的 SETTINGS 帧
         * @param frame 要发送的帧对象
         */
        void handleSendSetting(Frame::ptr frame);

        /**
         * @brief 发送 GOAWAY 帧
         * @param last_stream_id 最后处理的流 ID
         * @param error 错误码
         * @param debug 调试信息
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendGoAway(uint32_t last_stream_id, uint32_t error, const std::string &debug);

        /**
         * @brief 发送 SETTINGS 帧
         * @param items SETTINGS 项列表
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendSettings(const std::vector<SettingsItem> &items);

        /**
         * @brief 发送 SETTINGS ACK 帧
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendSettingsAck();

        /**
         * @brief 发送 RST_STREAM 帧
         * @param stream_id 流 ID
         * @param error_code 错误码
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendRstStream(uint32_t stream_id, uint32_t error_code);

        /**
         * @brief 发送 PING 帧
         * @param ack 是否为 ACK
         * @param v 8字节的PING值
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendPing(bool ack, uint64_t v);

        /**
         * @brief 发送 WINDOW_UPDATE 帧
         * @param stream_id 流 ID（0表示连接级窗口）
         * @param n 窗口增量
         * @return 发送的字节数，<0 表示发送失败
         */
        int32_t sendWindowUpdate(uint32_t stream_id, uint32_t n);

        /**
         * @brief 创建新的流（自动分配流 ID）
         * @return 新创建的流指针
         */
        Http2Stream::ptr newStream();

        /**
         * @brief 创建指定 ID 的流
         * @param id 指定的流 ID
         * @return 创建的流指针
         */
        Http2Stream::ptr newStream(uint32_t id);

        /**
         * @brief 获取指定 ID 的流
         * @param id 流 ID
         * @return 流指针，不存在则返回nullptr
         */
        Http2Stream::ptr getStream(uint32_t id);

        /**
         * @brief 删除指定 ID 的流
         * @param id 流 ID
         */
        void delStream(uint32_t id);

        /**
         * @brief 获取发送 HPack 动态表
         * @return 动态表引用
         */
        DynamicTable &getSendTable() { return m_sendTable; }

        /**
         * @brief 获取接收 HPack 动态表
         * @return 动态表引用
         */
        DynamicTable &getRecvTable() { return m_recvTable; }

        /**
         * @brief 获取本端设置
         * @return 设置引用
         */
        Http2Settings &getOwnerSettings() { return m_owner; }

        /**
         * @brief 获取对端设置
         * @return 设置引用
         */
        Http2Settings &getPeerSettings() { return m_peer; }

        /**
         * @brief 是否使用 SSL
         * @return 是否使用 SSL
         */
        bool isSsl() const { return m_ssl; }

        // StreamClient::ptr openStreamClient(sylar::http::HttpRequest::ptr request);

        /**
         * @brief 打开一个新的流用于发送请求
         * @param request HTTP 请求对象
         * @return 创建的流指针
         */
        Http2Stream::ptr openStream(sylar::http::HttpRequest::ptr request);

        /**
         * @brief 连接关闭时的回调
         */
        void onClose() override;

    protected:
        /**
         * @brief 帧发送上下文
         */
        struct FrameSendCtx : public SendCtx {
            typedef std::shared_ptr<FrameSendCtx> ptr;
            Frame::ptr frame; ///< 要发送的帧

            /**
             * @brief 执行发送操作
             * @param stream 异步套接字流指针
             * @return 是否发送成功
             */
            virtual bool doSend(AsyncSocketStream::ptr stream) override;
        };

        /**
         * @brief 请求上下文
         */
        struct RequestCtx : public Ctx {
            typedef std::shared_ptr<RequestCtx> ptr;
            http::HttpRequest::ptr request;   ///< HTTP 请求对象
            http::HttpResponse::ptr response; ///< HTTP 响应对象

            /**
             * @brief 执行发送操作
             * @param stream 异步套接字流指针
             * @return 是否发送成功
             */
            virtual bool doSend(AsyncSocketStream::ptr stream) override;
        };

        /**
         * @brief 流上下文
         */
        struct StreamCtx : public Ctx {
            typedef std::shared_ptr<StreamCtx> ptr;
            http::HttpRequest::ptr request; ///< HTTP 请求对象

            /**
             * @brief 执行发送操作
             * @param stream 异步套接字流指针
             * @return 是否发送成功
             */
            virtual bool doSend(AsyncSocketStream::ptr stream) override;
        };

        /**
         * @brief 执行接收操作
         * @return 上下文指针
         */
        virtual Ctx::ptr doRecv() override;

    protected:
        /**
         * @brief 处理 WINDOW_UPDATE 帧
         * @param frame 接收到的帧对象
         */
        void handleWindowUpdate(Frame::ptr frame);

        /**
         * @brief 处理接收到的数据帧
         * @param frame 接收到的帧对象
         * @param stream 对应的流指针
         */
        void handleRecvData(Frame::ptr frame, Http2Stream::ptr stream);

    protected:
        /**
         * @brief 更新设置
         * @param sts 设置对象引用
         * @param frame SETTINGS 帧对象
         */
        void updateSettings(Http2Settings &sts, SettingsFrame::ptr frame);
        // virtual void handleRequest(http::HttpRequest::ptr req, Http2Stream::ptr stream);

        /**
         * @brief 根据差值更新发送窗口
         * @param diff 窗口差值
         */
        void updateSendWindowByDiff(int32_t diff);

        /**
         * @brief 根据差值更新接收窗口
         * @param diff 窗口差值
         */
        void updateRecvWindowByDiff(int32_t diff);

        /**
         * @brief 超时回调
         * @param ctx 上下文指针
         */
        void onTimeOut(AsyncSocketStream::Ctx::ptr ctx) override;

        /**
         * @brief 流关闭回调（纯虚函数，由派生类实现）
         * @param stream 关闭的流指针
         * @return 上下文指针
         */
        virtual AsyncSocketStream::Ctx::ptr onStreamClose(Http2Stream::ptr stream) = 0;

        /**
         * @brief 头部结束回调（纯虚函数，由派生类实现）
         * @param stream 对应的流指针
         * @return 上下文指针
         */
        virtual AsyncSocketStream::Ctx::ptr onHeaderEnd(Http2Stream::ptr stream) = 0;

    protected:
        DynamicTable m_sendTable;       ///< 发送 HPack 动态表
        DynamicTable m_recvTable;       ///< 接收 HPack 动态表
        FrameCodec::ptr m_codec;        ///< 帧编解码器
        uint32_t m_sn;                  ///< 流序列号计数器
        bool m_isClient;                ///< 是否为客户端
        bool m_ssl;                     ///< 是否使用 SSL
        Http2Settings m_owner;          ///< 本端设置
        Http2Settings m_peer;           ///< 对端设置
        Http2StreamManager m_streamMgr; ///< 流管理器

        RWMutexType m_mutex;           ///< 读写锁
        std::list<Frame::ptr> m_waits; ///< 等待发送的帧队列

        int32_t m_sendWindow = DEFAULT_INITIAL_WINDOW_SIZE; ///< 发送窗口大小
        int32_t m_recvWindow = DEFAULT_INITIAL_WINDOW_SIZE; ///< 接收窗口大小
    };

} // namespace http2
} // namespace sylar