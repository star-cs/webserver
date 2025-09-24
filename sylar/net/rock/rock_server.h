#ifndef __SYLAR_ROCK_SERVER_H__
#define __SYLAR_ROCK_SERVER_H__

// Rock 服务端封装：基于 TcpServer 接入，使用 RockSession 处理连接
#include "rock_stream.h"
#include "sylar/net/tcp_server.h"

namespace sylar
{

class RockServer : public TcpServer
{
public:
    typedef std::shared_ptr<RockServer> ptr;
    // type: 服务类型标识（用于服务发现/模块路由）
    RockServer(const std::string &type = "rock",
               sylar::IOManager *worker = sylar::IOManager::GetThis(),
               sylar::IOManager *io_worker = sylar::IOManager::GetThis(),
               sylar::IOManager *accept_worker = sylar::IOManager::GetThis());

protected:
    // 每当有新连接建立，创建 RockSession，绑定业务回调并启动
    virtual void handleClient(Socket::ptr client) override;
};

} // namespace sylar

#endif
