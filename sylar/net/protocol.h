/**
 * @file protocol.h
 * @brief 自定义协议
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-07-03
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_PROTOCOL_H__
#define __SYLAR_PROTOCOL_H__

#include <memory>
#include "sylar/net/stream.h"
#include "sylar/net/bytearray.h"

namespace sylar
{

class Message
{
public:
    typedef std::shared_ptr<Message> ptr;
    enum MessageType { REQUEST = 1, RESPONSE = 2, NOTIFY = 3 };
    virtual ~Message() {}

    virtual ByteArray::ptr toByteArray();
    virtual bool serializeToByteArray(ByteArray::ptr bytearray) = 0;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) = 0;

    virtual std::string toString() const = 0;
    virtual const std::string &getName() const = 0;
    virtual int32_t getType() const = 0;
};

class MessageDecoder
{
public:
    typedef std::shared_ptr<MessageDecoder> ptr;

    virtual ~MessageDecoder() {}
    virtual Message::ptr parseFrom(Stream::ptr stream) = 0;
    virtual int32_t serializeTo(Stream::ptr stream, Message::ptr msg) = 0;
};

class Request : public Message
{
public:
    typedef std::shared_ptr<Request> ptr;

    Request();

    uint32_t getSn() const { return m_sn; }
    uint32_t getCmd() const { return m_cmd; }

    void setSn(uint32_t v) { m_sn = v; }
    void setCmd(uint32_t v) { m_cmd = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    // m_sn 表示请求的序列号（Sequence Number），用于唯一标识一次请求，便于请求-响应的匹配
    uint32_t m_sn;
    // m_cmd 表示请求的命令字（Command），用于区分不同的业务操作类型
    uint32_t m_cmd;
};

class Response : public Message
{
public:
    typedef std::shared_ptr<Response> ptr;

    Response();

    uint32_t getSn() const { return m_sn; }
    uint32_t getCmd() const { return m_cmd; }
    uint32_t getResult() const { return m_result; }
    const std::string &getResultStr() const { return m_resultStr; }

    void setSn(uint32_t v) { m_sn = v; }
    void setCmd(uint32_t v) { m_cmd = v; }
    void setResult(uint32_t v) { m_result = v; }
    void setResultStr(const std::string &v) { m_resultStr = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    uint32_t m_sn; // 响应对应的请求序列号（Sequence Number），用于关联请求和响应
    uint32_t m_cmd;          // 响应对应的命令字（Command），标识业务操作类型
    uint32_t m_result;       // 业务处理结果码，0 通常表示成功，非0为错误码
    std::string m_resultStr; // 结果描述字符串，通常用于错误信息或结果说明
};

class Notify : public Message
{
public:
    typedef std::shared_ptr<Notify> ptr;
    Notify();

    uint32_t getNotify() const { return m_notify; }
    void setNotify(uint32_t v) { m_notify = v; }

    virtual bool serializeToByteArray(ByteArray::ptr bytearray) override;
    virtual bool parseFromByteArray(ByteArray::ptr bytearray) override;

protected:
    // m_notify 表示通知类型或通知码，用于标识不同的通知消息
    uint32_t m_notify;
};

} // namespace sylar

#endif
