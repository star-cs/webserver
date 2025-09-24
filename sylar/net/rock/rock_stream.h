#ifndef __SYLAR_ROCK_ROCK_STREAM_H__
#define __SYLAR_ROCK_ROCK_STREAM_H__

// Rock 流式会话与连接封装
//
// 设计要点：
// - 基于 AsyncSocketStream 提供异步收发、请求/应答上下文（Ctx）管理
// - 使用 RockMessageDecoder 完成协议编解码
// - 提供 request/notify 处理回调，业务侧只需关注消息对象
// - RockSession 代表服务端会话；RockConnection 代表客户端连接
// - RockSDLoadBalance 集成服务发现并封装请求转发
#include "sylar/net/streams/async_socket_stream.h"
#include "rock_protocol.h"
#include "load_balance.h"
#include "sylar/core/common/singleton.h"
#include <boost/any.hpp>

namespace sylar
{

// 一次 Rock 请求的结果聚合
struct RockResult {
    typedef std::shared_ptr<RockResult> ptr;
    RockResult(int32_t _result, const std::string &_resultStr, int32_t _used, RockResponse::ptr rsp,
               RockRequest::ptr req)
        : result(_result), used(_used), resultStr(_resultStr), response(rsp), request(req)
    {
    }
    int32_t result;        // 业务/网络返回码，0 成功，<0 表示连接/超时等错误
    int32_t used;          // 耗时（ms）
    std::string resultStr; // 返回码描述
    RockResponse::ptr response; // 响应对象（成功时有效）
    RockRequest::ptr request;   // 请求对象

    std::string server;

    std::string toString() const;
};

class RockStream : public sylar::AsyncSocketStream
{
public:
    typedef std::shared_ptr<RockStream> ptr;
    typedef std::function<bool(sylar::RockRequest::ptr, sylar::RockResponse::ptr,
                               sylar::RockStream::ptr)>
        request_handler;
    typedef std::function<bool(sylar::RockNotify::ptr, sylar::RockStream::ptr)> notify_handler;

    RockStream(Socket::ptr sock);
    ~RockStream();

    // 发送任意 Rock Message（Request/Response/Notify）
    int32_t sendMessage(Message::ptr msg);
    // 发送请求并等待应答，超时返回 TIMEOUT
    RockResult::ptr request(RockRequest::ptr req, uint32_t timeout_ms);

    request_handler getRequestHandler() const { return m_requestHandler; }
    notify_handler getNotifyHandler() const { return m_notifyHandler; }

    void setRequestHandler(request_handler v) { m_requestHandler = v; }
    void setNotifyHandler(notify_handler v) { m_notifyHandler = v; }

    // 为连接附加任意类型的业务数据
    template <class T>
    void setData(const T &v)
    {
        m_data = v;
    }

    template <class T>
    T getData()
    {
        try {
            return boost::any_cast<T>(m_data);
        } catch (...) {
        }
        return T();
    }

protected:
    // 发送上下文：发送一个通用 Message
    struct RockSendCtx : public SendCtx {
        typedef std::shared_ptr<RockSendCtx> ptr;
        Message::ptr msg;

        virtual bool doSend(AsyncSocketStream::ptr stream) override;
    };

    // 请求/应答上下文：用于 request 等待响应
    struct RockCtx : public Ctx {
        typedef std::shared_ptr<RockCtx> ptr;
        RockRequest::ptr request;
        RockResponse::ptr response;

        virtual bool doSend(AsyncSocketStream::ptr stream) override;
    };

    virtual Ctx::ptr doRecv() override;

    // 内部分发到业务层回调
    void handleRequest(sylar::RockRequest::ptr req);
    void handleNotify(sylar::RockNotify::ptr nty);

private:
    RockMessageDecoder::ptr m_decoder; // 协议编解码器
    request_handler m_requestHandler;
    notify_handler m_notifyHandler;
    boost::any m_data; // 业务自定义数据
    uint32_t m_sn = 0; // 本地请求序列号自增
};

// 服务端会话：包装已接受的 Socket
class RockSession : public RockStream
{
public:
    typedef std::shared_ptr<RockSession> ptr;
    RockSession(Socket::ptr sock);
};

// 客户端连接：主动 connect 到目标地址
class RockConnection : public RockStream
{
public:
    typedef std::shared_ptr<RockConnection> ptr;
    RockConnection();
    bool connect(sylar::Address::ptr addr);
};

// Rock + 服务发现的负载均衡封装
class RockSDLoadBalance : public SDLoadBalance
{
public:
    typedef std::shared_ptr<RockSDLoadBalance> ptr;
    RockSDLoadBalance(IServiceDiscovery::ptr sd);

    virtual void start();
    virtual void stop();
    void start(const std::unordered_map<std::string, std::unordered_map<std::string, std::string> >
                   &confs);

    // 通过 domain/service 选择连接并发起请求
    RockResult::ptr request(const std::string &domain, const std::string &service,
                            RockRequest::ptr req, uint32_t timeout_ms, uint64_t idx = -1);
};

} // namespace sylar

#endif
