// Rock 协议编解码实现
// 负责：
// - 消息体与基础类型序列化
// - 头部校验（magic/version/length）
// - 可选 gzip 压缩/解压
// - 根据 type 分发到 Request/Response/Notify
#include "rock_protocol.h"
#include "sylar/core/log/log.h"
#include "sylar/core/config/config.h"
#include "sylar/net/endian.h"
#include "sylar/net/streams/zlib_stream.h"
#include "sylar/core/common/macro.h"

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 最大消息长度（防止异常/攻击），默认 64MB
static sylar::ConfigVar<uint32_t>::ptr g_rock_protocol_max_length = sylar::Config::Lookup(
    "rock.protocol.max_length", (uint32_t)(1024 * 1024 * 64), "rock protocol max length");

// 触发 gzip 的最小原始 payload 大小，默认 64MB（即通常不压缩，留接口可配置）
static sylar::ConfigVar<uint32_t>::ptr g_rock_protocol_gzip_min_length = sylar::Config::Lookup(
    "rock.protocol.gzip_min_length", (uint32_t)(1024 * 1024 * 64), "rock protocol gizp min length");

// 写出 body: [F32 length][bytes]
bool RockBody::serializeToByteArray(ByteArray::ptr bytearray)
{
    bytearray->writeStringF32(m_body);
    return true;
}

// 读取 body: [F32 length][bytes]
bool RockBody::parseFromByteArray(ByteArray::ptr bytearray)
{
    m_body = bytearray->readStringF32();
    return true;
}

// 构造与请求对应的响应对象，复制 sn/cmd
std::shared_ptr<RockResponse> RockRequest::createResponse()
{
    RockResponse::ptr rt = std::make_shared<RockResponse>();
    rt->setSn(m_sn);
    rt->setCmd(m_cmd);
    return rt;
}

std::string RockRequest::toString() const
{
    std::stringstream ss;
    ss << "[RockRequest sn=" << m_sn << " cmd=" << m_cmd << " body.length=" << m_body.size() << "]";
    return ss.str();
}

const std::string &RockRequest::getName() const
{
    static const std::string &s_name = "RockRequest";
    return s_name;
}

int32_t RockRequest::getType() const
{
    return Message::REQUEST;
}

bool RockRequest::serializeToByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Request::serializeToByteArray(bytearray);
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockRequest serializeToByteArray error";
    }
    return false;
}

bool RockRequest::parseFromByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Request::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockRequest parseFromByteArray error "
                                  << bytearray->toHexString();
    }
    return false;
}

std::string RockResponse::toString() const
{
    std::stringstream ss;
    ss << "[RockResponse sn=" << m_sn << " cmd=" << m_cmd << " result=" << m_result
       << " result_msg=" << m_resultStr << " body.length=" << m_body.size() << "]";
    return ss.str();
}

const std::string &RockResponse::getName() const
{
    static const std::string &s_name = "RockResponse";
    return s_name;
}

int32_t RockResponse::getType() const
{
    return Message::RESPONSE;
}

bool RockResponse::serializeToByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Response::serializeToByteArray(bytearray);
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockResponse serializeToByteArray error";
    }
    return false;
}

bool RockResponse::parseFromByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Response::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockResponse parseFromByteArray error";
    }
    return false;
}

std::string RockNotify::toString() const
{
    std::stringstream ss;
    ss << "[RockNotify notify=" << m_notify << " body.length=" << m_body.size() << "]";
    return ss.str();
}

const std::string &RockNotify::getName() const
{
    static const std::string &s_name = "RockNotify";
    return s_name;
}

int32_t RockNotify::getType() const
{
    return Message::NOTIFY;
}

bool RockNotify::serializeToByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Notify::serializeToByteArray(bytearray);
        v &= RockBody::serializeToByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockNotify serializeToByteArray error";
    }
    return false;
}

bool RockNotify::parseFromByteArray(ByteArray::ptr bytearray)
{
    try {
        bool v = true;
        v &= Notify::parseFromByteArray(bytearray);
        v &= RockBody::parseFromByteArray(bytearray);
        return v;
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockNotify parseFromByteArray error";
    }
    return false;
}

static const uint8_t s_rock_magic[2] = {0x12, 0x21};

RockMsgHeader::RockMsgHeader()
    : magic{s_rock_magic[0], s_rock_magic[1]}, version(1), flag(0), length(0)
{
}

// 从流中读取并解码一条 Rock 消息
Message::ptr RockMessageDecoder::parseFrom(Stream::ptr stream)
{
    try {
        RockMsgHeader header;
        int rt = stream->readFixSize(&header, sizeof(header));
        if (rt <= 0) {
            // 区分不同类型的错误
            if (rt == 0) {
                SYLAR_LOG_DEBUG(g_logger) << "RockMessageDecoder connection closed by peer";
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                SYLAR_LOG_DEBUG(g_logger) << "RockMessageDecoder would block, retry later";
            } else if (errno == EBADF) {
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder bad file descriptor rt=" << rt
                                          << " errno=" << errno << " " << strerror(errno);
            } else {
                SYLAR_LOG_DEBUG(g_logger) << "RockMessageDecoder decode head error rt=" << rt
                                          << " errno=" << errno << " " << strerror(errno);
            }
            return nullptr;
        }

        if (memcmp(header.magic, s_rock_magic, sizeof(s_rock_magic))) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.magic error";
            return nullptr;
        }

        if (header.version != 0x1) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.version != 0x1";
            return nullptr;
        }

        header.length = sylar::byteswapOnLittleEndian(header.length);
        if ((uint32_t)header.length >= g_rock_protocol_max_length->getValue()) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder head.length(" << header.length
                                      << ") >=" << g_rock_protocol_max_length->getValue();
            return nullptr;
        }
        sylar::ByteArray::ptr ba = std::make_shared<sylar::ByteArray>();
        rt = stream->readFixSize(ba, header.length);
        if (rt <= 0) {
            SYLAR_LOG_ERROR(g_logger)
                << "RockMessageDecoder read body fail length=" << header.length << " rt=" << rt
                << " errno=" << errno << " - " << strerror(errno);
            return nullptr;
        }

        ba->setPosition(0);
        // SYLAR_LOG_INFO(g_logger) << ba->toHexString();
        if (header.flag & 0x1) { // gzip 压缩标志
            auto zstream = sylar::ZlibStream::CreateGzip(false);
            if (zstream->write(ba, -1) != Z_OK) {
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder ungzip error";
                return nullptr;
            }
            if (zstream->flush() != Z_OK) {
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder ungzip flush error";
                return nullptr;
            }
            ba = zstream->getByteArray();
        }
        uint8_t type = ba->readFuint8();
        Message::ptr msg;
        switch (type) { // 根据消息类型实例化
            case Message::REQUEST:
                msg = std::make_shared<RockRequest>();
                break;
            case Message::RESPONSE:
                msg = std::make_shared<RockResponse>();
                break;
            case Message::NOTIFY:
                msg = std::make_shared<RockNotify>();
                break;
            default:
                SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder invalid type=" << (int)type;
                return nullptr;
        }

        if (!msg->parseFromByteArray(ba)) {
            SYLAR_LOG_ERROR(g_logger)
                << "RockMessageDecoder parseFromByteArray fail type=" << (int)type;
            return nullptr;
        }
        return msg;
    } catch (std::exception &e) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder except:" << e.what();
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder except";
    }
    return nullptr;
}

// 将消息编码后写入流。若 payload 超过阈值，可启用 gzip 压缩
int32_t RockMessageDecoder::serializeTo(Stream::ptr stream, Message::ptr msg)
{
    RockMsgHeader header;
    auto ba = msg->toByteArray();
    ba->setPosition(0);
    header.length = ba->getSize();
    if ((uint32_t)header.length >= g_rock_protocol_gzip_min_length->getValue()) {
        auto zstream = sylar::ZlibStream::CreateGzip(true);
        if (zstream->write(ba, -1) != Z_OK) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo gizp error";
            return -1;
        }
        if (zstream->flush() != Z_OK) {
            SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo gizp flush error";
            return -2;
        }
 
        ba = zstream->getByteArray();
        header.flag |= 0x1;
        header.length = ba->getSize();
    }
    header.length = sylar::byteswapOnLittleEndian(header.length);
    int rt = stream->writeFixSize(&header, sizeof(header));
    if (rt <= 0) {
        // 区分不同类型的错误
        if (rt == 0) {
            SYLAR_LOG_DEBUG(g_logger)
                << "RockMessageDecoder connection closed by peer during header write";
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            SYLAR_LOG_DEBUG(g_logger)
                << "RockMessageDecoder would block during header write, retry later";
        } else if (errno == EBADF) {
            SYLAR_LOG_ERROR(g_logger)
                << "RockMessageDecoder bad file descriptor during header write rt=" << rt
                << " errno=" << errno << " - " << strerror(errno);
        } else if (errno == EPIPE) {
            SYLAR_LOG_DEBUG(g_logger)
                << "RockMessageDecoder broken pipe during header write rt=" << rt;
        } else {
            SYLAR_LOG_ERROR(g_logger)
                << "RockMessageDecoder serializeTo write header fail rt=" << rt
                << " errno=" << errno << " - " << strerror(errno);
        }
        return -3;
    }
    rt = stream->writeFixSize(ba, ba->getReadSize());
    if (rt <= 0) {
        SYLAR_LOG_ERROR(g_logger) << "RockMessageDecoder serializeTo write body fail rt=" << rt
                                  << " errno=" << errno << " - " << strerror(errno);
        return -4;
    }
    return sizeof(header) + ba->getSize();
}

} // namespace sylar
