#pragma once

#include "sylar/net/http/servlet.h"

namespace sylar
{
namespace http
{

    class ConfigServlet : public Servlet
    {
    public:
        ConfigServlet();
        virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                               sylar::http::HttpResponse::ptr response,
                               sylar::SocketStream::ptr session) override;
    };

} // namespace http
} // namespace sylar
