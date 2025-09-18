#pragma once
#include "sylar/core/iomanager.h"
#include "sylar/net/tcp_server.h"
#include "sylar/net/http/servlet.h"

namespace sylar::http2
{
class Http2Server : public TcpServer
{
public:
    typedef std::shared_ptr<Http2Server> ptr;
    Http2Server(sylar::IOManager *worker = sylar::IOManager::GetThis(),
                sylar::IOManager *io_worker = sylar::IOManager::GetThis(),
                sylar::IOManager *accept_worker = sylar::IOManager::GetThis());

    http::ServletDispatch::ptr getServletDispatch() const { return m_dispatch; }
    void setServletDispatch(http::ServletDispatch::ptr v) { m_dispatch = v; }

    virtual void setName(const std::string &v) override;

protected:
    virtual void handleClient(Socket::ptr client) override;

protected:
    http::ServletDispatch::ptr m_dispatch;
};

} // namespace sylar::http2