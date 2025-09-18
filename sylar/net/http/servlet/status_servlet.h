#pragma once

#include "sylar/net/http/servlet.h"

namespace sylar {
namespace http {

class StatusServlet : public Servlet {
public:
    StatusServlet();
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::SocketStream::ptr session) override;
};

}
}
