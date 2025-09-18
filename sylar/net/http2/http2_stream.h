#pragma once

#include "frame.h"
#include "sylar/core/mutex.h"
#include <functional>
#include <unordered_map>
#include "sylar/net/http/http.h"
#include "sylar/core/ds/blocking_queue.h"
#include "hpack.h"

namespace sylar::http2
{

/*
                                +--------+
                        send PP |        | recv PP
                       ,--------|  idle  |--------.
                      /         |        |         \
                     v          +--------+          v
              +----------+          |           +----------+
              |          |          | send H /  |          |
       ,------| reserved |          | recv H    | reserved |------.
       |      | (local)  |          |           | (remote) |      |
       |      +----------+          v           +----------+      |
       |          |             +--------+             |          |
       |          |     recv ES |        | send ES     |          |
       |   send H |     ,-------|  open  |-------.     | recv H   |
       |          |    /        |        |        \    |          |
       |          v   v         +--------+         v   v          |
       |      +----------+          |           +----------+      |
       |      |   half   |          |           |   half   |      |
       |      |  closed  |          | send R /  |  closed  |      |
       |      | (remote) |          | recv R    | (local)  |      |
       |      +----------+          |           +----------+      |
       |           |                |                 |           |
       |           | send ES /      |       recv ES / |           |
       |           | send R /       v        send R / |           |
       |           | recv R     +--------+   recv R   |           |
       | send R /  `----------->|        |<-----------'  send R / |
       | recv R                 | closed |               recv R   |
       `----------------------->|        |<----------------------'
                                +--------+

          send:   endpoint sends this frame
          recv:   endpoint receives this frame

          H:  HEADERS frame (with implied CONTINUATIONs)
          PP: PUSH_PROMISE frame (with implied CONTINUATIONs)
          ES: END_STREAM flag
          R:  RST_STREAM frame
*/

class Http2SocketStream;

/**
 * @brief HTTP/2 流类，代表一个 HTTP/2 连接中的单个流
 * @details 实现了 HTTP/2 流的创建、管理、数据收发等功能
 *          每个流都有唯一的流 ID，独立的状态管理和流量控制窗口
 */
class Http2Stream : public std::enable_shared_from_this<Http2Stream>
{
public:
    friend class Http2SocketStream;
    typedef std::shared_ptr<Http2Stream> ptr; ///< 智能指针类型定义
    typedef std::function<int32_t(Frame::ptr)> frame_handler; ///< 帧处理函数类型定义

    /**
     * @brief HTTP/2 流状态枚举
     * @details 定义了 HTTP/2 协议中流的七种可能状态
     */
    enum class State {
        IDLE = 0x0,              ///< 空闲状态：流初始化后的状态
        OPEN = 0x1,              ///< 打开状态：流已建立，可以发送和接收数据
        CLOSED = 0x2,            ///< 关闭状态：流已完全关闭
        RESERVED_LOCAL = 0x3,    ///< 本地保留状态：服务端预留用于推送
        RESERVED_REMOTE = 0x4,   ///< 远程保留状态：客户端预留用于推送
        HALF_CLOSE_LOCAL = 0x5,  ///< 本地半关闭状态：本端已发送完数据，但仍可接收
        HALF_CLOSE_REMOTE = 0x6  ///< 远程半关闭状态：对端已发送完数据，但仍可发送
    };

    /**
     * @brief 构造函数，创建 HTTP/2 流对象
     * @param[in] stm 所属的 HTTP/2 套接字流
     * @param[in] id 流 ID
     */
    Http2Stream(std::shared_ptr<Http2SocketStream> stm, uint32_t id);

    /**
     * @brief 析构函数，清理流资源
     */
    ~Http2Stream();

    /**
     * @brief 获取流 ID
     * @return 流的唯一标识符
     */
    uint32_t getId() const { return m_id; }

    /**
     * @brief 获取流处理计数
     * @return 当前处理计数
     */
    uint8_t getHandleCount() const { return m_handleCount; }

    /**
     * @brief 增加流处理计数
     */
    void addHandleCount();

    /**
     * @brief 将流状态转换为字符串表示
     * @param[in] state HTTP/2 流状态
     * @return 状态对应的字符串
     */
    static std::string StateToString(State state);

    /**
     * @brief 处理接收到的帧
     * @param[in] frame 接收到的帧对象
     * @param[in] is_client 是否为客户端模式
     * @return 处理结果，>=0 表示成功，<0 表示失败
     */
    int32_t handleFrame(Frame::ptr frame, bool is_client);

    /** 
     * @brief 发送 HTTP/2 帧
     * @param[in] frame 要发送的帧
     * @param[in] async 是否异步发送
     * @return 发送结果，>=0 表示成功，<0 表示失败
     */
    int32_t sendFrame(Frame::ptr frame, bool async);

    /**
     * @brief 发送 HTTP 响应
     * @param[in] rsp HTTP 响应对象
     * @param[in] end_stream 是否结束流
     * @param[in] async 是否异步发送
     * @return 发送结果，>=0 表示成功，<0 表示失败
     */
    int32_t sendResponse(sylar::http::HttpResponse::ptr rsp, bool end_stream, bool async);

    /**
     * @brief 发送 HTTP 请求
     * @param[in] req HTTP 请求对象
     * @param[in] end_stream 是否结束流
     * @param[in] async 是否异步发送
     * @return 发送结果，>=0 表示成功，<0 表示失败
     */
    int32_t sendRequest(sylar::http::HttpRequest::ptr req, bool end_stream, bool async);

    /**
     * @brief 发送 HTTP 头部
     * @param[in] headers 头部字段映射表
     * @param[in] end_stream 是否结束流
     * @param[in] async 是否异步发送
     * @return 发送结果，>=0 表示成功，<0 表示失败
     */
    int32_t sendHeaders(const std::map<std::string, std::string> &headers, bool end_stream,
                        bool async);

    /**
     * @brief 获取所属的 HTTP/2 套接字流
     * @return HTTP/2 套接字流智能指针
     */
    std::shared_ptr<Http2SocketStream> getSockStream() const;

    /**
     * @brief 获取流的当前状态
     * @return 流状态枚举值
     */
    State getState() const { return m_state; }

    /**
     * @brief 获取 HTTP 请求对象
     * @return HTTP 请求对象智能指针
     */
    http::HttpRequest::ptr getRequest() const { return m_request; }

    /**
     * @brief 获取 HTTP 响应对象
     * @return HTTP 响应对象智能指针
     */
    http::HttpResponse::ptr getResponse() const { return m_response; }

    /**
     * @brief 更新发送窗口大小
     * @param[in] diff 窗口大小的差值
     * @return 处理结果，0 表示成功
     */
    int32_t updateSendWindowByDiff(int32_t diff);

    /**
     * @brief 更新接收窗口大小
     * @param[in] diff 窗口大小的差值
     * @return 处理结果，0 表示成功
     */
    int32_t updateRecvWindowByDiff(int32_t diff);

    /**
     * @brief 获取发送窗口大小
     * @return 发送窗口当前大小
     */
    int32_t getSendWindow() const { return m_sendWindow; }

    /**
     * @brief 获取接收窗口大小
     * @return 接收窗口当前大小
     */
    int32_t getRecvWindow() const { return m_recvWindow; }

    /**
     * @brief 获取帧处理函数
     * @return 帧处理函数
     */
    frame_handler getFrameHandler() const { return m_handler; }

    /**
     * @brief 设置帧处理函数
     * @param[in] v 要设置的帧处理函数
     */
    void setFrameHandler(frame_handler v) { m_handler = v; }

    /**
     * @brief 获取指定名称的头部字段值
     * @param[in] name 头部字段名称
     * @return 头部字段值，如果不存在返回空字符串
     */
    std::string getHeader(const std::string &name) const;

    /**
     * @brief 初始化 HTTP 请求对象
     */
    void initRequest();

    /**
     * @brief 关闭流，清理相关资源
     */
    void close();

    /**
     * @brief 结束流，发送 END_STREAM 标志
     */
    void endStream();

    /**
     * @brief 获取数据主体内容
     * @return 数据主体字符串
     */
    std::string getDataBody();

    /**
     * @brief 接收数据帧
     * @return 数据帧智能指针
     */
    DataFrame::ptr recvData();

    /**
     * @brief 发送数据
     * @param[in] data 要发送的数据
     * @param[in] end_stream 是否结束流
     * @param[in] async 是否异步发送
     * @return 发送结果，>=0 表示成功，<0 表示失败
     */
    int32_t sendData(const std::string &data, bool end_stream, bool async);

    /**
     * @brief 获取是否为流式处理
     * @return 是否为流式处理
     */
    bool getIsStream() const { return m_isStream; }

    /**
     * @brief 设置是否为流式处理
     * @param[in] v 是否为流式处理
     */
    void setIsStream(bool v) { m_isStream = v; }

private:
    /**
     * @brief 处理 HEADERS 帧
     * @param[in] frame 接收到的帧对象
     * @param[in] is_client 是否为客户端模式
     * @return 处理结果，>=0 表示成功，<0 表示失败
     */
    int32_t handleHeadersFrame(Frame::ptr frame, bool is_client);

    /**
     * @brief 处理 DATA 帧
     * @param[in] frame 接收到的帧对象
     * @param[in] is_client 是否为客户端模式
     * @return 处理结果，>=0 表示成功，<0 表示失败
     */
    int32_t handleDataFrame(Frame::ptr frame, bool is_client);

    /**
     * @brief 处理 RST_STREAM 帧
     * @param[in] frame 接收到的帧对象
     * @param[in] is_client 是否为客户端模式
     * @return 处理结果，>=0 表示成功，<0 表示失败
     */
    int32_t handleRstStreamFrame(Frame::ptr frame, bool is_client);

    /**
     * @brief 通用的窗口大小更新方法
     * @param[in] window_size 指向窗口大小变量的指针
     * @param[in] diff 窗口大小的差值
     * @return 处理结果，0 表示成功
     */
    int32_t updateWindowSizeByDiff(int32_t *window_size, int32_t diff);

private:
    std::weak_ptr<Http2SocketStream> m_stream; ///< 所属的 HTTP/2 套接字流弱引用
    State m_state; ///< 当前流状态
    uint8_t m_handleCount; ///< 处理计数
    bool m_isStream; ///< 是否为流式处理
    uint32_t m_id; ///< 流 ID
    http::HttpRequest::ptr m_request; ///< HTTP 请求对象
    http::HttpResponse::ptr m_response; ///< HTTP 响应对象
    HPack::ptr m_recvHPack; ///< HPack 解码器（用于接收头部）
    frame_handler m_handler; ///< 帧处理函数
    //std::string m_body; // 注释掉的成员变量，原始实现使用阻塞队列存储数据帧
    sylar::ds::BlockingQueue<DataFrame> m_data; ///< 数据帧阻塞队列

    int32_t m_sendWindow = 0; ///< 发送窗口大小
    int32_t m_recvWindow = 0; ///< 接收窗口大小
};

//class StreamClient : public std::enable_shared_from_this<StreamClient> {
//public:
//    typedef std::shared_ptr<StreamClient> ptr;
//
//    static StreamClient::ptr Create(Http2Stream::ptr stream);
//
//    int32_t sendData(const std::string& data, bool end_stream);
//    DataFrame::ptr recvData();
//
//    Http2Stream::ptr getStream() { return m_stream;}
//    int32_t close();
//private:
//    int32_t onFrame(Frame::ptr frame);
//private:
//    Http2Stream::ptr m_stream;
//    sylar::ds::BlockingQueue<DataFrame> m_data;
//};

/**
 * @brief HTTP/2 流管理器
 * @details 负责管理 HTTP/2 连接中的所有流，提供添加、删除、获取和遍历流的功能
 */
class Http2StreamManager
{
public:
    typedef std::shared_ptr<Http2StreamManager> ptr; ///< 智能指针类型定义
    typedef sylar::RWSpinlock RWMutexType; ///< 读写锁类型定义

    /**
     * @brief 根据流 ID 获取流对象
     * @param[in] id 流 ID
     * @return 流对象智能指针，如果不存在返回 nullptr
     */
    Http2Stream::ptr get(uint32_t id);

    /**
     * @brief 添加流对象到管理器中
     * @param[in] stream 流对象智能指针
     */
    void add(Http2Stream::ptr stream);

    /**
     * @brief 从管理器中删除指定 ID 的流对象
     * @param[in] id 要删除的流 ID
     */
    void del(uint32_t id);

    /**
     * @brief 清空所有流对象并关闭它们
     */
    void clear();

    /**
     * @brief 对所有流对象执行回调函数
     * @param[in] cb 回调函数，参数为流对象智能指针
     */
    void foreach (std::function<void(Http2Stream::ptr)> cb);

private:
    RWMutexType m_mutex; ///< 保护流映射的读写锁
    std::unordered_map<uint32_t, Http2Stream::ptr> m_streams; ///< 流 ID 到流对象的映射
};

} // namespace sylar::http2