#include "http2_stream.h"
#include "http2_socket_stream.h"
#include "sylar/core/log/log.h"
#include "sylar/core/common/macro.h"

namespace sylar::http2
{

// HTTP/2流管理相关日志记录器
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// HTTP/2流状态字符串映射
static const std::vector<std::string> s_state_strings = {
    "IDLE",              // 空闲状态
    "OPEN",              // 打开状态
    "CLOSED",            // 关闭状态
    "RESERVED_LOCAL",    // 本地保留状态
    "RESERVED_REMOTE",   // 远程保留状态
    "HALF_CLOSE_LOCAL",  // 本地半关闭状态
    "HALF_CLOSE_REMOTE", // 远程半关闭状态
};

/**
 * @brief 将流状态转换为字符串表示
 * @param[in] state HTTP/2流状态
 * @return 状态对应的字符串
 */
std::string Http2Stream::StateToString(State state)
{
    uint8_t v = (uint8_t)state;
    if (v < 7) {
        return s_state_strings[v];
    }
    return "UNKNOW(" + std::to_string((uint32_t)v) + ")";
}

/**
 * @brief 构造函数，创建HTTP/2流对象
 * @param[in] stm 所属的HTTP/2套接字流
 * @param[in] id 流ID
 */
Http2Stream::Http2Stream(std::shared_ptr<Http2SocketStream> stm, uint32_t id)
    : m_stream(stm), m_state(State::IDLE), m_handleCount(0), m_isStream(false), m_id(id)
{
    // 初始化发送窗口和接收窗口大小
    m_sendWindow = stm->getPeerSettings().initial_window_size;
    m_recvWindow = stm->getOwnerSettings().initial_window_size;
}

/**
 * @brief 增加流处理计数
 */
void Http2Stream::addHandleCount()
{
    ++m_handleCount;
}

/**
 * @brief 获取指定名称的头部字段值
 * @param[in] name 头部字段名称
 * @return 头部字段值，如果不存在返回空字符串
 */
std::string Http2Stream::getHeader(const std::string &name) const
{
    if (!m_recvHPack) {
        return "";
    }
    auto &m = m_recvHPack->getHeaders();
    for (auto &i : m) {
        if (i.name == name) {
            return i.value;
        }
    }
    return "";
}

/**
 * @brief 析构函数，清理流资源
 */
Http2Stream::~Http2Stream()
{
    close();
    SYLAR_LOG_INFO(g_logger) << "Http2Stream::~Http2Stream id=" << m_id << " - "
                             << StateToString(m_state);
}

/**
 * @brief 关闭流，清理相关资源
 */
void Http2Stream::close()
{
    if (m_handler) {
        // 通知处理函数流已关闭
        m_handler(nullptr);
    }
    m_data.push(nullptr); // 推送空数据表示流结束
    // m_data.notifyAll();
}

/**
 * @brief 结束流，发送空数据并设置END_STREAM标志
 */
void Http2Stream::endStream()
{
    sendData("", true, true);
}

/**
 * @brief 获取所属的HTTP/2套接字流
 * @return HTTP/2套接字流对象的弱引用
 */
std::shared_ptr<Http2SocketStream> Http2Stream::getSockStream() const
{
    return m_stream.lock();
}

/**
 * @brief 处理RST_STREAM帧，用于重置流
 * @param[in] frame RST_STREAM帧
 * @param[in] is_client 是否为客户端
 * @return 处理结果，0表示成功
 */
int32_t Http2Stream::handleRstStreamFrame(Frame::ptr frame, bool is_client)
{
    // 收到RST_STREAM帧后，将流状态设置为CLOSED
    m_state = State::CLOSED;
    return 0;
}

/**
 * @brief 处理HTTP/2帧的主方法
 * @param[in] frame 要处理的帧
 * @param[in] is_client 是否为客户端
 * @return 处理结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::handleFrame(Frame::ptr frame, bool is_client)
{
    int rt = 0;
    // 根据帧类型调用相应的处理方法
    if (frame->header.type == (uint8_t)FrameType::HEADERS) {
        rt = handleHeadersFrame(frame, is_client);
        SYLAR_ASSERT(rt != -1);
    } else if (frame->header.type == (uint8_t)FrameType::DATA) {
        rt = handleDataFrame(frame, is_client);
    } else if (frame->header.type == (uint8_t)FrameType::RST_STREAM) {
        rt = handleRstStreamFrame(frame, is_client);
    }

    // 调用自定义处理函数（如果设置了）
    if (m_handler) {
        m_handler(frame);
    }

    // 检查是否有END_STREAM标志，表示流结束
    if (frame->header.flags & (uint8_t)FrameFlagHeaders::END_STREAM) {
        m_state = State::CLOSED;
        if (m_isStream) {
            // 对于流式传输，推送空数据表示流结束
            m_data.push(nullptr);
        }
        if (is_client) {
            // 客户端模式下，构建响应对象
            if (!m_response) {
                m_response = std::make_shared<http::HttpResponse>(0x20);
            }
            if (!m_isStream) {
                m_response->setBody(getDataBody());
            }
            if (m_recvHPack) {
                // 从HPack解压缩的头部中提取响应头
                auto &m = m_recvHPack->getHeaders();
                for (auto &i : m) {
                    m_response->setHeader(i.name, i.value);
                }
            }
            Http2InitResponseForRead(m_response);
        } else {
            // 服务器模式下，构建请求对象
            initRequest();
        }
        SYLAR_LOG_DEBUG(g_logger) << "id=" << m_id << " is_client=" << is_client
                                  << " req=" << m_request << " rsp=" << m_response;
    }
    return rt;
}

/**
 * @brief 获取所有接收到的数据作为请求体
 * @return 完整的请求体字符串
 */
std::string Http2Stream::getDataBody()
{
    std::stringstream ss;
    // 将接收到的所有数据帧内容拼接成完整的请求体
    while (!m_data.empty()) {
        auto data = m_data.pop();
        ss << data->data;
    }
    return ss.str();
}

/**
 * @brief 初始化HTTP请求对象
 */
void Http2Stream::initRequest()
{
    if (!m_request) {
        m_request = std::make_shared<http::HttpRequest>(0x20);
    }
    if (!m_isStream) {
        m_request->setBody(getDataBody());
    }
    if (m_recvHPack) {
        // 从HPack解压缩的头部中提取请求头
        auto &m = m_recvHPack->getHeaders();
        for (auto &i : m) {
            m_request->setHeader(i.name, i.value);
        }
    }
    Http2InitRequestForRead(m_request);
}

/**
 * @brief 处理HEADERS帧，解析HTTP头部
 * @param[in] frame HEADERS帧
 * @param[in] is_client 是否为客户端
 * @return 解析结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::handleHeadersFrame(Frame::ptr frame, bool is_client)
{
    // 将帧数据转换为HeadersFrame类型
    auto headers = std::dynamic_pointer_cast<HeadersFrame>(frame->data);
    if (!headers) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id
                                  << " handleHeadersFrame data not HeadersFrame "
                                  << frame->toString();
        return -1;
    }
    // 获取所属的HTTP/2套接字流
    auto stream = getSockStream();
    if (!stream) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " handleHeadersFrame stream is closed "
                                  << frame->toString();
        return -1;
    }

    // 如果还没有创建HPack解析器，创建一个
    if (!m_recvHPack) {
        m_recvHPack = std::make_shared<HPack>(stream->getRecvTable());
    }
    // 使用HPack解析器解析头部数据
    return m_recvHPack->parse(headers->data);
}

/**
 * @brief 处理DATA帧，处理实际的数据内容
 * @param[in] frame DATA帧
 * @param[in] is_client 是否为客户端
 * @return 处理结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::handleDataFrame(Frame::ptr frame, bool is_client)
{
    // sleep(1);
    // if(m_handleCount > 0) {
    //     return 0;
    // }
    // 将帧数据转换为DataFrame类型
    auto data = std::dynamic_pointer_cast<DataFrame>(frame->data);
    if (!data) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " handleDataFrame data not DataFrame "
                                  << frame->toString();
        return -1;
    }
    // 获取所属的HTTP/2套接字流
    auto stream = getSockStream();
    if (!stream) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " handleDataFrame stream is closed "
                                  << frame->toString();
        return -1;
    }
    // 将数据帧加入队列等待处理
    m_data.push(data);
    // m_body += data->data;
    // SYLAR_LOG_DEBUG(g_logger) << "stream_id=" << m_id << " cur_body_size=" << m_body.size();
    // if(is_client) {
    //     m_response = std::make_shared<http::HttpResponse>(0x20);
    //     m_response->setBody(data->data);
    // } else {
    //     m_request = std::make_shared<http::HttpRequest>(0x20);
    //     m_request->setBody(data->data);
    // }
    return 0;
}

/**
 * @brief 接收数据帧
 * @return 接收到的数据帧，如果队列为空则返回nullptr
 */
DataFrame::ptr Http2Stream::recvData()
{
    return m_data.pop();
}

/**
 * @brief 发送HTTP请求
 * @param[in] req HTTP请求对象
 * @param[in] end_stream 是否结束流
 * @param[in] async 是否异步发送
 * @return 发送结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::sendRequest(sylar::http::HttpRequest::ptr req, bool end_stream, bool async)
{
    auto stream = getSockStream();
    if (!stream) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendResponse stream is closed";
        return -1;
    }

    // 初始化HTTP/2请求的特殊头部
    Http2InitRequestForWrite(req, stream->isSsl());

    // 创建HEADERS帧
    Frame::ptr headers = std::make_shared<Frame>();
    headers->header.type = (uint8_t)FrameType::HEADERS;
    headers->header.flags = (uint8_t)FrameFlagHeaders::END_HEADERS;
    headers->header.identifier = m_id;
    HeadersFrame::ptr data;
    data = std::make_shared<HeadersFrame>();
    auto m = req->getHeaders();
    // 如果结束流且请求体为空，设置END_STREAM标志
    if (end_stream && req->getBody().empty()) {
        headers->header.flags |= (uint8_t)FrameFlagHeaders::END_STREAM;
    }

    // 创建HPack编码器并添加所有头部
    data->hpack = std::make_shared<HPack>(stream->m_sendTable);
    for (auto &i : m) {
        // 头部名称转换为小写（HTTP/2规范要求）
        data->kvs.emplace_back(sylar::ToLower(i.first), i.second);
    }
    // debug stream_id
    data->kvs.push_back(std::make_pair("stream_id", std::to_string(m_id)));
    headers->data = data;
    // 发送HEADERS帧
    int32_t ok = stream->sendFrame(headers, async);
    if (ok < 0) {
        SYLAR_LOG_INFO(g_logger) << "sendHeaders fail";
        return ok;
    }
    // 如果请求体不为空，发送数据
    if (!req->getBody().empty()) {
        ok = stream->sendData(shared_from_this(), req->getBody(), async, true);
        if (ok <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendData fail, rt=" << ok
                                      << " size=" << req->getBody().size();
            return ok;
        }
    }
    return ok;
}

/**
 * @brief 发送HTTP头部
 * @param[in] m 头部字段映射
 * @param[in] end_stream 是否结束流
 * @param[in] async 是否异步发送
 * @return 发送结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::sendHeaders(const std::map<std::string, std::string> &m, bool end_stream,
                                 bool async)
{
    auto stream = getSockStream();
    if (!stream) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendHeaders stream is closed";
        return -1;
    }

    // 创建HEADERS帧
    Frame::ptr headers = std::make_shared<Frame>();
    headers->header.type = (uint8_t)FrameType::HEADERS;
    headers->header.flags = (uint8_t)FrameFlagHeaders::END_HEADERS;
    if (end_stream) {
        headers->header.flags |= (uint8_t)FrameFlagHeaders::END_STREAM;
    }
    headers->header.identifier = m_id;
    HeadersFrame::ptr data;
    data = std::make_shared<HeadersFrame>();

    // 创建HPack编码器并添加所有头部
    data->hpack = std::make_shared<HPack>(stream->getSendTable());
    for (auto &i : m) {
        // 头部名称转换为小写（HTTP/2规范要求）
        data->kvs.emplace_back(sylar::ToLower(i.first), i.second);
    }
    headers->data = data;
    // 发送HEADERS帧
    int ok = stream->sendFrame(headers, async);
    if (ok < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendHeaders fail " << ok;
        return ok;
    }
    return ok;
}

/**
 * @brief 发送HTTP响应
 * @param[in] rsp HTTP响应对象
 * @param[in] end_stream 是否结束流
 * @param[in] async 是否异步发送
 * @return 发送结果，>=0表示成功，<0表示失败
 
 * Trailer 头部，允许再响应体数据发送完成后，再发送额外的头部字段
 * 当某些头部值需要在处理完整个响应体后才能确定（如内容校验和）
 * 流式传输场景中，需要在传输结束时提供额外元数据
 */
int32_t Http2Stream::sendResponse(http::HttpResponse::ptr rsp, bool end_stream, bool async)
{
    auto stream = getSockStream();
    if (!stream) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendResponse stream is closed";
        return -1;
    }

    // 初始化HTTP/2响应的特殊头部
    Http2InitResponseForWrite(rsp);

    // 创建HEADERS帧
    Frame::ptr headers = std::make_shared<Frame>();
    headers->header.type = (uint8_t)FrameType::HEADERS;
    headers->header.flags = (uint8_t)FrameFlagHeaders::END_HEADERS;
    headers->header.identifier = m_id;
    HeadersFrame::ptr data;
    auto m = rsp->getHeaders();
    data = std::make_shared<HeadersFrame>();

    // 处理trailer头部（尾部头部）
    auto trailer = rsp->getHeader("trailer");
    std::set<std::string> trailers;
    if (!trailer.empty()) {
        auto vec = sylar::split(trailer, ',');
        for (auto &i : vec) {
            trailers.insert(sylar::StringUtil::Trim(i));
        }
    }

    // 如果结束流且响应体为空且没有trailer，设置END_STREAM标志
    if (end_stream && rsp->getBody().empty() && trailers.empty()) {
        headers->header.flags |= (uint8_t)FrameFlagHeaders::END_STREAM;
    }

    // 创建HPack编码器并添加所有非trailer头部
    data->hpack = std::make_shared<HPack>(stream->getSendTable());
    for (auto &i : m) {
        if (trailers.count(i.first)) {
            continue; // 跳过trailer头部，后面单独处理
        }
        data->kvs.emplace_back(sylar::ToLower(i.first), i.second);
    }
    headers->data = data;
    // 发送HEADERS帧
    int ok = stream->sendFrame(headers, async);
    if (ok < 0) {
        SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendResponse send Headers fail";
        return ok;
    }
    // 如果响应体不为空，发送数据
    if (!rsp->getBody().empty()) {
        ok = stream->sendData(shared_from_this(), rsp->getBody(), async, trailers.empty());
        if (ok < 0) {
            SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_id << " sendData fail, rt=" << ok
                                      << " size=" << rsp->getBody().size();
        }
    }
    // 如果有trailer头部，单独发送
    if (end_stream && !trailers.empty()) {
        Frame::ptr headers = std::make_shared<Frame>();
        headers->header.type = (uint8_t)FrameType::HEADERS;
        headers->header.flags =
            (uint8_t)FrameFlagHeaders::END_HEADERS | (uint8_t)FrameFlagHeaders::END_STREAM;
        headers->header.identifier = m_id;

        HeadersFrame::ptr data = std::make_shared<HeadersFrame>();
        data->hpack = std::make_shared<HPack>(stream->getSendTable());
        for (auto &i : trailers) {
            auto v = rsp->getHeader(i);
            data->kvs.emplace_back(sylar::ToLower(i), v);
        }
        headers->data = data;
        bool ok = stream->sendFrame(headers, async) > 0;
        if (!ok) {
            SYLAR_LOG_INFO(g_logger) << "sendHeaders trailer fail";
            return ok;
        }
    }

    return ok;
}

/**
 * @brief 发送HTTP/2帧
 * @param[in] frame 要发送的帧
 * @param[in] async 是否异步发送
 * @return 发送结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::sendFrame(Frame::ptr frame, bool async)
{
    auto stream = getSockStream();
    if (stream) {
        return stream->sendFrame(frame, async);
    }
    return 0;
}

/**
 * @brief 更新发送窗口大小
 * @param[in] diff 窗口大小的差值
 * @return 处理结果，0表示成功
 */
int32_t Http2Stream::updateSendWindowByDiff(int32_t diff)
{
    return updateWindowSizeByDiff(&m_sendWindow, diff);
}

/**
 * @brief 更新接收窗口大小
 * @param[in] diff 窗口大小的差值
 * @return 处理结果，0表示成功
 */
int32_t Http2Stream::updateRecvWindowByDiff(int32_t diff)
{
    return updateWindowSizeByDiff(&m_recvWindow, diff);
}

/**
 * @brief 通用的窗口大小更新方法
 * @param[in] window_size 指向窗口大小变量的指针
 * @param[in] diff 窗口大小的差值
 * @return 处理结果，0表示成功
 */
int32_t Http2Stream::updateWindowSizeByDiff(int32_t *window_size, int32_t diff)
{
    int64_t new_value = *window_size + diff;
    // 检查窗口大小是否有效（HTTP/2规范要求窗口大小不能小于0或大于2^31-1）
    if (new_value < 0 || new_value > MAX_INITIAL_WINDOW_SIZE) {
        SYLAR_LOG_DEBUG(g_logger) << (window_size == &m_recvWindow ? "recv_window" : "send_window")
                                  << " update to " << new_value << ", from=" << *window_size
                                  << " diff=" << diff << ", invalid"
                                  << " stream_id=" << m_id << " " << this;
        // return -1;
    }
    // 原子操作更新窗口大小
    sylar::Atomic::addFetch(*window_size, diff);
    //*window_size += diff;
    return 0;
}

/**
 * @brief 发送数据
 * @param[in] data 要发送的数据
 * @param[in] end_stream 是否结束流
 * @param[in] async 是否异步发送
 * @return 发送结果，>=0表示成功，<0表示失败
 */
int32_t Http2Stream::sendData(const std::string &data, bool end_stream, bool async)
{
    auto stm = getSockStream();
    if (stm) {
        return stm->sendData(shared_from_this(), data, async, end_stream);
    }
    return -1;
}

// StreamClient::ptr StreamClient::Create(Http2Stream::ptr stream) {
//     auto rt = std::make_shared<StreamClient>();
//     rt->m_stream = stream;
//     stream->setFrameHandler(std::bind(&StreamClient::onFrame, rt, std::placeholders::_1));
//     return rt;
// }
//
// int32_t StreamClient::close() {
//     return sendData("", true);
// }
//
// int32_t StreamClient::sendData(const std::string& data, bool end_stream) {
//     auto stm = m_stream->getSockStream();
//     if(stm) {
//         return stm->sendData(m_stream, data, true, end_stream);
//     }
//     return -1;
// }
//
// DataFrame::ptr StreamClient::recvData() {
//     auto pd = m_data.pop();
//     return pd;
// }
//
// int32_t StreamClient::onFrame(Frame::ptr frame) {
//     if(!frame) {
//         m_data.push(nullptr);
//         return 0;
//     }
//     if(frame->header.type == (uint8_t)FrameType::DATA) {
//         auto data = std::dynamic_pointer_cast<DataFrame>(frame->data);
//         if(!data) {
//             SYLAR_LOG_ERROR(g_logger) << "Stream id=" << m_stream->getId()
//                 << " onFrame data not DataFrame "
//                 << frame->toString();
//             return -1;
//         }
//         m_data.push(data);
//     }
//     if(frame->header.flags & (uint8_t)FrameFlagHeaders::END_STREAM) {
//         m_data.push(nullptr);
//     }
//     return 0;
// }

/**
 * @brief 根据流ID获取流对象
 * @param[in] id 流ID
 * @return 流对象智能指针，如果不存在返回nullptr
 */
Http2Stream::ptr Http2StreamManager::get(uint32_t id)
{
    RWMutexType::ReadLock lock(m_mutex); // 读锁保护
    auto it = m_streams.find(id);
    return it == m_streams.end() ? nullptr : it->second;
}

/**
 * @brief 添加流对象到管理器中
 * @param[in] stream 流对象智能指针
 */
void Http2StreamManager::add(Http2Stream::ptr stream)
{
    RWMutexType::WriteLock lock(m_mutex); // 写锁保护
    m_streams[stream->getId()] = stream;
}

/**
 * @brief 从管理器中删除指定ID的流对象
 * @param[in] id 要删除的流ID
 */
void Http2StreamManager::del(uint32_t id)
{
    RWMutexType::WriteLock lock(m_mutex); // 写锁保护
    m_streams.erase(id);
}

/**
 * @brief 清空所有流对象并关闭它们
 */
void Http2StreamManager::clear()
{
    RWMutexType::WriteLock lock(m_mutex); // 写锁保护
    auto streams = m_streams;
    lock.unlock(); // 提前解锁，避免在关闭流时持有锁
    for (auto &i : streams) {
        i.second->close(); // 关闭每个流
    }
}

/**
 * @brief 对所有流对象执行回调函数
 * @param[in] cb 回调函数，参数为流对象智能指针
 */
void Http2StreamManager::foreach (std::function<void(Http2Stream::ptr)> cb)
{
    RWMutexType::ReadLock lock(m_mutex); // 读锁保护
    auto m = m_streams; // 复制流映射，避免在执行回调时持有锁
    lock.unlock(); // 提前解锁
    for (auto &i : m) {
        cb(i.second); // 对每个流执行回调
    }
}
} // namespace sylar::http2