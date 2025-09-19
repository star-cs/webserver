#include "http2_socket_stream.h"

namespace sylar::http2
{
// 日志记录器
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// HTTP/2客户端连接前导字符串，用于标识HTTP/2连接
static const std::string CLIENT_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

/**
 * @brief Http2SocketStream构造函数
 * @param[in] sock Socket连接
 * @param[in] client 是否为客户端模式
 */
Http2SocketStream::Http2SocketStream(Socket::ptr sock, bool client)
    : AsyncSocketStream(sock, true), m_sn(client ? -1 : 0), m_isClient(client), m_ssl(false)
{
    m_codec = std::make_shared<FrameCodec>();
    SYLAR_LOG_INFO(g_logger) << "Http2SocketStream::Http2SocketStream sock=" << sock << " - "
                             << this;
}

/**
 * @brief Http2SocketStream析构函数
 */
Http2SocketStream::~Http2SocketStream()
{
    SYLAR_LOG_INFO(g_logger) << "Http2SocketStream::~Http2SocketStream " << this;
}

/**
 * @brief 连接关闭时的回调函数
 * 清理所有流管理器中的流资源
 */
void Http2SocketStream::onClose()
{
    SYLAR_LOG_INFO(g_logger) << "******** onClose " << getLocalAddressString() << " - "
                             << getRemoteAddressString();
    m_streamMgr.clear();
}

/**
 * @brief 客户端模式下的HTTP/2握手过程
 * 发送客户端前导字符串和初始SETTINGS帧
 * @return 是否握手成功
 */
bool Http2SocketStream::handleShakeClient()
{
    SYLAR_LOG_INFO(g_logger) << "handleShakeClient " << getRemoteAddressString();
    if (!isConnected()) {
        return false;
    }

    // 发送HTTP/2客户端前导字符串
    int rt = writeFixSize(CLIENT_PREFACE.c_str(), CLIENT_PREFACE.size());
    if (rt <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeClient CLIENT_PREFACE fail, rt=" << rt
                                  << " errno=" << errno << " - " << strerror(errno) << " - "
                                  << getRemoteAddressString();
        return false;
    }

    // 创建并设置初始SETTINGS帧
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::SETTINGS;
    auto sf = std::make_shared<SettingsFrame>();
    sf->items.emplace_back((uint8_t)SettingsFrame::Settings::ENABLE_PUSH, 0); // 禁用服务器推送
    sf->items.emplace_back((uint8_t)SettingsFrame::Settings::INITIAL_WINDOW_SIZE,
                           4194304); // 设置初始窗口大小
    sf->items.emplace_back((uint8_t)SettingsFrame::Settings::MAX_HEADER_LIST_SIZE,
                           10485760); // 设置最大头部列表大小
    frame->data = sf;

    // 处理发送设置并发送帧
    handleSendSetting(frame);
    rt = sendFrame(frame, false);
    if (rt <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeClient Settings fail, rt=" << rt
                                  << " errno=" << errno << " - " << strerror(errno) << " - "
                                  << getRemoteAddressString();
        return false;
    }
    // 更新窗口大小
    sendWindowUpdate(0, MAX_INITIAL_WINDOW_SIZE - m_recvWindow);
    return true;
}

/**
 * @brief 服务器模式下的HTTP/2握手过程
 * 接收并验证客户端前导字符串，处理客户端SETTINGS帧
 * @return 是否握手成功
 */
bool Http2SocketStream::handleShakeServer()
{
    ByteArray::ptr ba = std::make_shared<ByteArray>();
    // 接收客户端前导字符串
    int rt = readFixSize(ba, CLIENT_PREFACE.size());
    if (rt <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeServer recv CLIENT_PREFACE fail, rt=" << rt
                                  << " errno=" << errno << " - " << strerror(errno) << " - "
                                  << getRemoteAddressString();
        return false;
    }
    ba->setPosition(0);
    // 验证前导字符串是否正确
    if (ba->toString() != CLIENT_PREFACE) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeServer recv CLIENT_PREFACE fail, rt=" << rt
                                  << " errno=" << errno << " - " << strerror(errno)
                                  << " hex: " << ba->toHexString() << " - "
                                  << getRemoteAddressString();
        return false;
    }
    // 解析客户端SETTINGS帧
    auto frame = m_codec->parseFrom(shared_from_this());
    if (!frame) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeServer recv SettingsFrame fail,"
                                  << " errno=" << errno << " - " << strerror(errno) << " - "
                                  << getRemoteAddressString();
        return false;
    }
    // 验证是否为SETTINGS帧
    if (frame->header.type != (uint8_t)FrameType::SETTINGS) {
        SYLAR_LOG_ERROR(g_logger) << "handleShakeServer recv Frame not SettingsFrame, type="
                                  << FrameTypeToString((FrameType)frame->header.type) << " - "
                                  << getRemoteAddressString();
        return false;
    }
    // 处理接收到的设置，发送确认和服务器设置
    handleRecvSetting(frame);
    sendSettingsAck();
    sendSettings({});
    return true;
}

/**
 * @brief 发送HTTP/2帧
 * @param[in] frame 要发送的帧
 * @param[in] async 是否异步发送
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendFrame(Frame::ptr frame, bool async)
{
    if (isConnected()) {
        if (async) {
            // 异步发送模式，将帧加入队列
            FrameSendCtx::ptr ctx = std::make_shared<FrameSendCtx>();
            ctx->frame = frame;
            enqueue(ctx);
            return 1;
        } else {
            // 同步发送模式，直接序列化发送
            return m_codec->serializeTo(shared_from_this(), frame);
        }
    } else {
        return -1;
    }
}

/**
 * @brief 处理接收到的WINDOW_UPDATE帧
 * 更新连接或流的窗口大小
 * @param[in] frame 接收到的WINDOW_UPDATE帧
 */
void Http2SocketStream::handleWindowUpdate(Frame::ptr frame)
{
    auto wuf = std::dynamic_pointer_cast<WindowUpdateFrame>(frame->data);
    if (wuf) {
        if (frame->header.identifier) {
            // 流级别的窗口更新
            auto stream = getStream(frame->header.identifier);
            if (!stream) {
                SYLAR_LOG_ERROR(g_logger) << "WINDOW_UPDATE stream_id=" << frame->header.identifier
                                          << " not exists, " << getRemoteAddressString();
                sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                return;
            }
            // 检查窗口大小是否超过最大值
            if (((int64_t)stream->m_sendWindow + wuf->increment) > MAX_INITIAL_WINDOW_SIZE) {
                SYLAR_LOG_ERROR(g_logger)
                    << "WINDOW_UPDATE stream_id=" << stream->getId()
                    << " increment=" << wuf->increment << " send_window=" << stream->m_sendWindow
                    << " biger than " << MAX_INITIAL_WINDOW_SIZE << " " << getRemoteAddressString();
                sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                return;
            }
            // 更新流的发送窗口
            stream->updateSendWindowByDiff(wuf->increment);
        } else {
            // 连接级别的窗口更新
            if (((int64_t)m_sendWindow + wuf->increment) > MAX_INITIAL_WINDOW_SIZE) {
                SYLAR_LOG_ERROR(g_logger)
                    << "WINDOW_UPDATE stream_id=0"
                    << " increment=" << wuf->increment << " send_window=" << m_sendWindow
                    << " biger than " << MAX_INITIAL_WINDOW_SIZE << " " << getRemoteAddressString();
                sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                return;
            }
            // 更新连接的发送窗口
            m_sendWindow += wuf->increment;
        }
    } else {
        // 无效的WINDOW_UPDATE帧体
        SYLAR_LOG_ERROR(g_logger) << "WINDOW_UPDATE stream_id=" << frame->header.identifier
                                  << " invalid body " << getRemoteAddressString();
        innerClose();
    }
}

/**
 * @brief 处理接收到的DATA帧数据
 * 更新连接和流的接收窗口
 * @param[in] frame 接收到的DATA帧
 * @param[in] stream 对应的HTTP/2流
 */
void Http2SocketStream::handleRecvData(Frame::ptr frame, Http2Stream::ptr stream)
{
    if (frame->header.length) {
        // 更新连接的接收窗口
        m_recvWindow -= frame->header.length;
        // 当窗口小于阈值时，发送窗口更新帧
        if (m_recvWindow < (int32_t)MAX_INITIAL_WINDOW_SIZE / 4) {
            SYLAR_LOG_INFO(g_logger)
                << "recv_window=" << m_recvWindow << " length=" << frame->header.length << " "
                << (MAX_INITIAL_WINDOW_SIZE / 4);
            sendWindowUpdate(0, MAX_INITIAL_WINDOW_SIZE - m_recvWindow);
        }

        // 更新流的接收窗口
        stream->m_recvWindow -= frame->header.length;
        // 当窗口小于阈值时，发送窗口更新帧
        if (stream->m_recvWindow < (int32_t)MAX_INITIAL_WINDOW_SIZE / 4) {
            SYLAR_LOG_INFO(g_logger)
                << "recv_window=" << stream->m_recvWindow << " length=" << frame->header.length
                << " " << (MAX_INITIAL_WINDOW_SIZE / 4)
                << " diff=" << (MAX_INITIAL_WINDOW_SIZE - stream->m_recvWindow);
            sendWindowUpdate(stream->getId(), MAX_INITIAL_WINDOW_SIZE - stream->m_recvWindow);
        }
    }
}

/**
 * @brief 作用域测试结构体（用于调试）
 */
struct ScopeTest {
    ScopeTest() { SYLAR_LOG_INFO(g_logger) << "=========== DoRecv ==========="; }
    ~ScopeTest() { SYLAR_LOG_INFO(g_logger) << "=========== DoRecv Out ==========="; }
};

/**
 * @brief 接收并处理HTTP/2帧的核心方法
 * 解析接收到的帧并根据帧类型进行相应处理
 * @return 处理结果上下文，如果需要后续处理则返回相应的上下文，否则返回nullptr
 */
AsyncSocketStream::Ctx::ptr Http2SocketStream::doRecv()
{
    // ScopeTest xxx;
    // SYLAR_LOG_INFO(g_logger) << "=========== DoRecv ===========";

    // 解析接收到的HTTP/2帧
    auto frame = m_codec->parseFrom(shared_from_this());
    if (!frame) {
        // 解析失败，关闭连接
        innerClose();
        return nullptr;
    }
    SYLAR_LOG_DEBUG(g_logger) << getRemoteAddressString() << " recv: " << frame->toString();
    // TODO handle RST_STREAM

    // 根据帧类型进行不同处理
    if (frame->header.type == (uint8_t)FrameType::WINDOW_UPDATE) {
        // 处理窗口更新帧
        handleWindowUpdate(frame);
    } else if (frame->header.identifier) {
        // 处理流相关的帧
        auto stream = getStream(frame->header.identifier);
        if (!stream) {
            // 流不存在
            if (m_isClient) {
                SYLAR_LOG_ERROR(g_logger) << "doRecv stream id=" << frame->header.identifier
                                          << " not exists " << frame->toString();
                return nullptr;
            } else {
                // 服务器端尝试创建新流
                stream = newStream(frame->header.identifier);
                if (!stream) {
                    if (frame->header.type != (uint8_t)FrameType::RST_STREAM) {
                        // sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                        sendRstStream(m_sn, (uint32_t)Http2Error::STREAM_CLOSED_ERROR);
                    }
                    return nullptr;
                }
            }
        }

        // 处理DATA帧
        if (frame->header.type == (uint8_t)FrameType::DATA) {
            handleRecvData(frame, stream);
        }
        // 让流处理自己的帧
        stream->handleFrame(frame, m_isClient);

        // 检查流状态
        if (stream->getState() == Http2Stream::State::CLOSED) {
            // 流关闭，处理关闭事件
            return onStreamClose(stream);
        } else if (frame->header.type == (uint8_t)FrameType::HEADERS
                   && frame->header.flags & (uint8_t)FrameFlagHeaders::END_HEADERS) {
            // HEADERS帧并且标记了END_HEADERS，处理头部结束事件
            return onHeaderEnd(stream);
        }
    } else {
        // 处理连接级别的帧
        if (frame->header.type == (uint8_t)FrameType::SETTINGS) {
            // 处理SETTINGS帧
            if (!(frame->header.flags & (uint8_t)FrameFlagSettings::ACK)) {
                handleRecvSetting(frame);
                sendSettingsAck();
            }
        } else if (frame->header.type == (uint8_t)FrameType::PING) {
            // 处理PING帧
            if (!(frame->header.flags & (uint8_t)FrameFlagPing::ACK)) {
                auto data = std::dynamic_pointer_cast<PingFrame>(frame->data);
                sendPing(true, data->uint64);
            }
        }
    }
    return nullptr;
}

// void Http2SocketStream::handleRequest(http::HttpRequest::ptr req, Http2Stream::ptr stream) {
//     if(stream->getHandleCount() > 0) {
//         return;
//     }
//     stream->addHandleCount();
//     http::HttpResponse::ptr rsp = std::make_shared<http::HttpResponse>(req->getVersion(), false);
//     req->setStreamId(stream->getId());
//     SYLAR_LOG_DEBUG(g_logger) << *req;
//     rsp->setHeader("server", m_server->getName());
//     int rt = m_server->getServletDispatch()->handle(req, rsp, shared_from_this());
//     if(rt != 0 || m_server->needSendResponse(req->getPath())) {
//         SYLAR_LOG_INFO(g_logger) << "send response ======";
//         stream->sendResponse(rsp);
//     }
//     delStream(stream->getId());
// }

/**
 * @brief FrameSendCtx的doSend方法，用于异步发送帧
 * @param[in] stream 异步Socket流
 * @return 是否发送成功
 */
bool Http2SocketStream::FrameSendCtx::doSend(AsyncSocketStream::ptr stream)
{
    return std::dynamic_pointer_cast<Http2SocketStream>(stream)->sendFrame(frame, false) > 0;
}

/**
 * @brief 发送数据到指定的HTTP/2流
 * 自动分片数据以适应最大帧大小
 * @param[in] stream 目标HTTP/2流
 * @param[in] data 要发送的数据
 * @param[in] async 是否异步发送
 * @param[in] end_stream 是否结束流
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendData(Http2Stream::ptr stream, const std::string &data, bool async,
                                    bool end_stream)
{
    int pos = 0;
    int length = data.size();

    // m_peer.max_frame_size = 1024;
    // 计算最大帧大小（减去帧头部9字节）
    auto max_frame_size = m_peer.max_frame_size - 9;

    // 分片发送数据
    do {
        int len = length;
        if (len > (int)max_frame_size) {
            len = max_frame_size;
        }
        // 创建DATA帧
        Frame::ptr body = std::make_shared<Frame>();
        body->header.type = (uint8_t)FrameType::DATA;
        // 设置END_STREAM标志（仅在最后一片数据时设置）
        if (end_stream) {
            body->header.flags = (length == len ? (uint8_t)FrameFlagData::END_STREAM : 0);
        } else {
            body->header.flags = 0;
        }
        body->header.identifier = stream->getId();
        auto df = std::make_shared<DataFrame>();
        df->data = data.substr(pos, len);
        body->data = df;

        // 发送帧
        int rt = sendFrame(body, async);
        if (rt <= 0) {
            SYLAR_LOG_DEBUG(g_logger) << "sendData error rt=" << rt << " errno=" << errno << " - "
                                      << getRemoteAddressString();
            return rt;
        }
        // 更新发送位置和剩余长度
        length -= len;
        pos += len;

        // 更新窗口大小
        stream->updateSendWindowByDiff(-len);
        sylar::Atomic::addFetch(m_sendWindow, -len);
        // send_window -= len;
    } while (length > 0);
    return 1;
}

/**
 * @brief RequestCtx的doSend方法，用于发送请求
 * @param[in] stream 异步Socket流
 * @return 是否发送成功
 */
bool Http2SocketStream::RequestCtx::doSend(AsyncSocketStream::ptr stream)
{
    auto h2stream = std::dynamic_pointer_cast<Http2SocketStream>(stream);
    auto stm = h2stream->getStream(sn);
    if (!stm) {
        SYLAR_LOG_ERROR(g_logger) << "RequestCtx doSend Fail, sn=" << sn << " not exists - "
                                  << stream->getRemoteAddressString();
        return false;
    }
    // 发送请求并等待响应
    return stm->sendRequest(request, true, false);
}

/**
 * @brief StreamCtx的doSend方法，用于发送请求但不等待响应
 * @param[in] stream 异步Socket流
 * @return 是否发送成功
 */
bool Http2SocketStream::StreamCtx::doSend(AsyncSocketStream::ptr stream)
{
    auto h2stream = std::dynamic_pointer_cast<Http2SocketStream>(stream);
    auto stm = h2stream->getStream(sn);
    if (!stm) {
        SYLAR_LOG_ERROR(g_logger) << "StreamCtx doSend Fail, sn=" << sn << " not exists - "
                                  << stream->getRemoteAddressString();
        return false;
    }
    // 发送请求但不等待响应
    return stm->sendRequest(request, false, false);
}

/**
 * @brief 上下文超时回调处理
 * @param[in] ctx 超时的上下文
 */
void Http2SocketStream::onTimeOut(AsyncSocketStream::Ctx::ptr ctx)
{
    AsyncSocketStream::onTimeOut(ctx);
    // 删除对应的流
    delStream(ctx->sn);
}

// StreamClient::ptr Http2SocketStream::openStreamClient(sylar::http::HttpRequest::ptr request) {
//     if(isConnected()) {
//         //Http2InitRequestForWrite(req, m_ssl);
//         auto stream = newStream();
//         StreamCtx::ptr ctx = std::make_shared<StreamCtx>();
//         ctx->request = request;
//         ctx->sn = stream->getId();
//         enqueue(ctx);
//         return StreamClient::Create(stream);
//     }
//     return nullptr;
// }

/**
 * @brief 创建新的HTTP/2流并发送请求
 * @param[in] request HTTP请求对象
 * @return 创建的HTTP/2流，失败返回nullptr
 */
Http2Stream::ptr Http2SocketStream::openStream(sylar::http::HttpRequest::ptr request)
{
    if (isConnected()) {
        // Http2InitRequestForWrite(req, m_ssl);
        // 创建新流
        auto stream = newStream();
        stream->m_isStream = true;
        // 创建流上下文并设置请求
        StreamCtx::ptr ctx = std::make_shared<StreamCtx>();
        ctx->request = request;
        ctx->sn = stream->getId();
        // 将上下文加入发送队列
        enqueue(ctx);
        return stream;
    }
    return nullptr;
}

/**
 * @brief 发送HTTP请求并等待响应
 * @param[in] req HTTP请求对象
 * @param[in] timeout_ms 超时时间（毫秒）
 * @return HTTP请求结果
 */
http::HttpResult::ptr Http2SocketStream::request(http::HttpRequest::ptr req, uint64_t timeout_ms)
{
    if (isConnected()) {
        // Http2InitRequestForWrite(req, m_ssl);
        // 创建新流
        auto stream = newStream();
        // 创建请求上下文
        RequestCtx::ptr ctx = std::make_shared<RequestCtx>();
        ctx->request = req;
        ctx->sn = stream->getId();
        ctx->timeout = timeout_ms;
        ctx->scheduler = sylar::Scheduler::GetThis();
        ctx->fiber = sylar::Fiber::GetThis();
        // 添加上下文并设置超时定时器
        addCtx(ctx);
        ctx->timer = sylar::IOManager::GetThis()->addTimer(
            timeout_ms,
            std::bind(&Http2SocketStream::onTimeOut,
                      std::dynamic_pointer_cast<Http2SocketStream>(shared_from_this()), ctx));
        // 将上下文加入发送队列
        enqueue(ctx);
        // 让出协程执行权，等待响应
        sylar::Fiber::GetThis()->yield();
        // 构建请求结果
        auto rt = std::make_shared<http::HttpResult>(ctx->result, ctx->response, ctx->resultStr);
        // 处理流重置的情况
        if (rt->result == 0 && !ctx->response) {
            rt->result = -401;
            rt->error = "rst_stream";
        }
        return rt;
    } else {
        // 连接未建立
        return std::make_shared<http::HttpResult>(AsyncSocketStream::NOT_CONNECT, nullptr,
                                                  "not_connect " + getRemoteAddressString());
    }
}

/**
 * @brief 处理接收到的SETTINGS帧
 * @param[in] frame 接收到的SETTINGS帧
 */
void Http2SocketStream::handleRecvSetting(Frame::ptr frame)
{
    auto s = std::dynamic_pointer_cast<SettingsFrame>(frame->data);
    SYLAR_LOG_DEBUG(g_logger) << "handleRecvSetting: " << s->toString();
    // 更新本地设置
    updateSettings(m_owner, s);
}

/**
 * @brief 处理要发送的SETTINGS帧
 * @param[in] frame 要发送的SETTINGS帧
 */
void Http2SocketStream::handleSendSetting(Frame::ptr frame)
{
    auto s = std::dynamic_pointer_cast<SettingsFrame>(frame->data);
    // 更新对端设置
    updateSettings(m_peer, s);
}

/**
 * @brief 发送GOAWAY帧，表示连接即将关闭
 * @param[in] last_stream_id 最后处理的流ID
 * @param[in] error 错误码
 * @param[in] debug 调试信息
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendGoAway(uint32_t last_stream_id, uint32_t error,
                                      const std::string &debug)
{
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::GOAWAY;
    GoAwayFrame::ptr data = std::make_shared<GoAwayFrame>();
    frame->data = data;
    data->last_stream_id = last_stream_id;
    data->error_code = error;
    data->data = debug;
    return sendFrame(frame, true);
}

/**
 * @brief 发送SETTINGS帧的确认
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendSettingsAck()
{
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::SETTINGS;
    frame->header.flags = (uint8_t)FrameFlagSettings::ACK;
    return sendFrame(frame, true);
}

/**
 * @brief 发送SETTINGS帧
 * @param[in] items 设置项列表
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendSettings(const std::vector<SettingsItem> &items)
{
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::SETTINGS;
    SettingsFrame::ptr data = std::make_shared<SettingsFrame>();
    frame->data = data;
    data->items = items;
    int rt = sendFrame(frame, true);
    if (rt > 0) {
        handleSendSetting(frame);
    }
    return rt;
}

/**
 * @brief 发送RST_STREAM帧，重置一个流
 * @param[in] stream_id 要重置的流ID
 * @param[in] error_code 错误码
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendRstStream(uint32_t stream_id, uint32_t error_code)
{
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::RST_STREAM;
    frame->header.identifier = stream_id;
    RstStreamFrame::ptr data = std::make_shared<RstStreamFrame>();
    frame->data = data;
    data->error_code = error_code;
    return sendFrame(frame, true);
}

/**
 * @brief 发送PING帧，用于连接保活和测量RTT
 * @param[in] ack 是否为确认
 * @param[in] v PING数据
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendPing(bool ack, uint64_t v)
{
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::PING;
    if (ack) {
        frame->header.flags = (uint8_t)FrameFlagPing::ACK;
    }
    PingFrame::ptr data = std::make_shared<PingFrame>();
    frame->data = data;
    data->uint64 = v;
    return sendFrame(frame, true);
}

/**
 * @brief 发送WINDOW_UPDATE帧，更新窗口大小
 * @param[in] stream_id 流ID，0表示连接级别
 * @param[in] n 窗口增量
 * @return 发送结果，>0成功，<=0失败
 */
int32_t Http2SocketStream::sendWindowUpdate(uint32_t stream_id, uint32_t n)
{
    // SYLAR_LOG_INFO(g_logger) << "----sendWindowUpdate id=" << stream_id << " n=" << n;
    Frame::ptr frame = std::make_shared<Frame>();
    frame->header.type = (uint8_t)FrameType::WINDOW_UPDATE;
    frame->header.identifier = stream_id;
    WindowUpdateFrame::ptr data = std::make_shared<WindowUpdateFrame>();
    frame->data = data;
    data->increment = n;
    // 提前更新本地窗口大小
    if (stream_id == 0) {
        // 连接级别的窗口更新
        m_recvWindow += n;
    } else {
        // 流级别的窗口更新
        auto stm = getStream(stream_id);
        if (stm) {
            stm->updateRecvWindowByDiff(n);
        } else {
            SYLAR_LOG_ERROR(g_logger) << "sendWindowUpdate stream=" << stream_id << " not exists";
        }
    }
    return sendFrame(frame, true);
}

/**
 * @brief 更新HTTP/2连接的设置参数
 * @param[in] sts 要更新的设置对象
 * @param[in] frame 包含设置信息的SETTINGS帧
 */
void Http2SocketStream::updateSettings(Http2Settings &sts, SettingsFrame::ptr frame)
{
    // 根据设置归属选择对应的动态表（发送或接收）
    DynamicTable &table = &sts == &m_owner ? m_sendTable : m_recvTable;

    // 遍历所有设置项并应用
    for (auto &i : frame->items) {
        switch ((SettingsFrame::Settings)i.identifier) {
            case SettingsFrame::Settings::HEADER_TABLE_SIZE:
                // 更新头部压缩表大小
                sts.header_table_size = i.value;
                table.setMaxDataSize(sts.header_table_size);
                break;
            case SettingsFrame::Settings::ENABLE_PUSH:
                // 设置服务器推送功能状态（0禁用，1启用）
                if (i.value != 0 && i.value != 1) {
                    SYLAR_LOG_ERROR(g_logger) << "invalid enable_push=" << i.value;
                    sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                    // TODO close socket
                }
                sts.enable_push = i.value;
                break;
            case SettingsFrame::Settings::MAX_CONCURRENT_STREAMS:
                // 设置最大并发流数量
                sts.max_concurrent_streams = i.value;
                break;
            case SettingsFrame::Settings::INITIAL_WINDOW_SIZE:
                // 更新初始窗口大小，并调整所有流的窗口
                if (i.value > MAX_INITIAL_WINDOW_SIZE) {
                    SYLAR_LOG_ERROR(g_logger) << "INITIAL_WINDOW_SIZE invalid value=" << i.value;
                    sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                } else {
                    int32_t diff = i.value - sts.initial_window_size;
                    sts.initial_window_size = i.value;
                    if (&sts == &m_peer) {
                        updateRecvWindowByDiff(diff);
                    } else {
                        updateSendWindowByDiff(diff);
                    }
                }
                break;
            case SettingsFrame::Settings::MAX_FRAME_SIZE:
                // 设置最大帧大小（必须在允许范围内）
                sts.max_frame_size = i.value;
                if (sts.max_frame_size < DEFAULT_MAX_FRAME_SIZE
                    || sts.max_frame_size > MAX_MAX_FRAME_SIZE) {
                    SYLAR_LOG_ERROR(g_logger) << "invalid max_frame_size=" << sts.max_frame_size;
                    sendGoAway(m_sn, (uint32_t)Http2Error::PROTOCOL_ERROR, "");
                    // TODO close socket
                }
                break;
            case SettingsFrame::Settings::MAX_HEADER_LIST_SIZE:
                // 设置最大头部列表大小
                sts.max_header_list_size = i.value;
                break;
            default:
                // sendGoAway(m_sn, Http2Error::PROTOCOL_ERROR, "");
                // TODO close socket
                break;
        }
    }
}

/**
 * @brief 使用指定ID创建新的HTTP/2流
 * @param[in] id 流ID
 * @return 新创建的流对象，如果ID无效则返回nullptr
 */
Http2Stream::ptr Http2SocketStream::newStream(uint32_t id)
{
    // 验证流ID是否有效（必须大于上一个使用的ID）
    if (id <= m_sn) {
        return nullptr;
    }
    m_sn = id;

    // 创建新流并添加到流管理器
    Http2Stream::ptr stream = std::make_shared<Http2Stream>(
        std::dynamic_pointer_cast<Http2SocketStream>(shared_from_this()), id);
    m_streamMgr.add(stream);
    return stream;
}

/**
 * @brief 创建新的HTTP/2流（自动分配ID）
 * @return 新创建的流对象
 * @note 根据HTTP/2规范，客户端发起的流ID为奇数，服务器为偶数，每次递增2
 */
Http2Stream::ptr Http2SocketStream::newStream()
{
    // 原子操作递增流ID（每次+2）并创建新流
    Http2Stream::ptr stream = std::make_shared<Http2Stream>(
        std::dynamic_pointer_cast<Http2SocketStream>(shared_from_this()),
        sylar::Atomic::addFetch(m_sn, 2));
    m_streamMgr.add(stream);
    return stream;
}

/**
 * @brief 获取指定ID的HTTP/2流
 * @param[in] id 流ID
 * @return 对应的流对象，如果不存在则返回nullptr
 */
Http2Stream::ptr Http2SocketStream::getStream(uint32_t id)
{
    return m_streamMgr.get(id);
}

/**
 * @brief 删除指定ID的HTTP/2流
 * @param[in] id 要删除的流ID
 * @note 删除前会先关闭流以释放相关资源
 */
void Http2SocketStream::delStream(uint32_t id)
{
    auto stream = m_streamMgr.get(id);
    if (stream) {
        stream->close();
    }
    return m_streamMgr.del(id);
}

/**
 * @brief 为所有流的发送窗口增加/减少指定差值
 * @param[in] diff 窗口大小的差值
 * @note 如果更新导致窗口大小无效，会发送RST_STREAM帧重置流
 */
void Http2SocketStream::updateSendWindowByDiff(int32_t diff)
{
    // 遍历所有流，更新它们的发送窗口
    m_streamMgr.foreach ([diff, this](Http2Stream::ptr stream) {
        // 如果更新导致窗口无效（返回true），发送RST_STREAM帧重置该流
        if (stream->updateSendWindowByDiff(diff)) {
            sendRstStream(stream->getId(), (uint32_t)Http2Error::FLOW_CONTROL_ERROR);
        }
    });
}

/**
 * @brief 为所有流的接收窗口增加/减少指定差值
 * @param[in] diff 窗口大小的差值
 * @note 如果更新导致窗口大小无效，会发送RST_STREAM帧重置流
 */
void Http2SocketStream::updateRecvWindowByDiff(int32_t diff)
{
    // 遍历所有流，更新它们的接收窗口
    m_streamMgr.foreach ([this, diff](Http2Stream::ptr stream) {
        // 如果更新导致窗口无效（返回true），发送RST_STREAM帧重置该流
        if (stream->updateRecvWindowByDiff(diff)) {
            sendRstStream(stream->getId(), (uint32_t)Http2Error::FLOW_CONTROL_ERROR);
        }
    });
}

} // namespace sylar::http2