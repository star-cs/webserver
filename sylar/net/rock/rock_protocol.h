#ifndef __SYLAR_ROCK_ROCK_PROTOCOL_H__
#define __SYLAR_ROCK_ROCK_PROTOCOL_H__

// Rock 协议定义与编解码器
//
// 设计要点：
// - 统一的消息体 RockBody，承载原始字符串或 Protobuf 序列化字节
// - 三种消息类型：Request/Response/Notify，均继承自通用 Message 体系
// - 传输层使用固定头 RockMsgHeader + 可选 gzip 压缩的 payload
// - 编解码由 RockMessageDecoder 实现，负责 header 校验、gzip 解压/压缩、消息分发

#include "sylar/net/protocol.h"
#include "google/protobuf/message.h"

namespace sylar
{

class RockBody
{
public:
    typedef std::shared_ptr<RockBody> ptr;
    virtual ~RockBody() {}

    // 设置/获取消息体字节（通常为 Protobuf 序列化结果或 JSON 文本）
    void setBody(const std::string &v) { m_body = v; }
    const std::string &getBody() const { return m_body; }

    // 将 body 写入 ByteArray（F32 前缀长度）
    virtual bool serializeToByteArray(ByteArray::ptr bytearray);
    // 从 ByteArray 读取 body（F32 前缀长度）
    virtual bool parseFromByteArray(ByteArray::ptr bytearray);

    template <class T>
    std::shared_ptr<T> getAsPB() const
    {
        try {
            std::shared_ptr<T> data = std::make_shared<T>();
            if (data->ParseFromString(m_body)) {
                return data;
            }
        } catch (...) {
        }
        return nullptr;
    }

    template <class T>
    bool setAsPB(const T &v)
    {
        try {
            return v.SerializeToString(&m_body);
        } catch (...) {
        }
        return false;
    }

protected:
    std::string m_body;
};

class RockResponse;
// Rock 协议请求，携带 sn/cmd 等通用头部字段与 RockBody
class RockRequest : public Request, public RockBody
{
public:
    typedef std::shared_ptr<RockRequest> ptr;

    // 基于当前请求构造一个对应的响应（复制 sn/cmd）
    std::shared_ptr<RockResponse> createResponse();

    virtual std::string toString() const override;
    virtual const std::string &getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

// Rock 协议响应，包含 result/resultStr 与 RockBody
class RockResponse : public Response, public RockBody
{
public:
    typedef std::shared_ptr<RockResponse> ptr;

    virtual std::string toString() const override;
    virtual const std::string &getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

// Rock 协议通知（单向，无应答）
class RockNotify : public Notify, public RockBody
{
public:
    typedef std::shared_ptr<RockNotify> ptr;

    virtual std::string toString() const override;
    virtual const std::string &getName() const override;
    virtual int32_t getType() const override;

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;
};

// 传输层固定头
// magic: 固定 0x12 0x21 用于快速校验
// version: 版本，目前为 0x1
// flag: bit0=1 表示 payload 使用 gzip 压缩
// length: payload 字节长度，网络字节序
struct RockMsgHeader {
    RockMsgHeader();
    uint8_t magic[2];
    uint8_t version;
    uint8_t flag;
    int32_t length;
};

// Rock 编解码器：负责从 Stream 读取并还原 Message，或将 Message 写入 Stream
class RockMessageDecoder : public MessageDecoder
{
public:
    typedef std::shared_ptr<RockMessageDecoder> ptr;

    virtual Message::ptr parseFrom(Stream::ptr stream) override;
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) override;
};

} // namespace sylar

#endif
