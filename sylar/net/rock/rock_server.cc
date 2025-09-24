// Rock 服务端连接处理实现
#include "rock_server.h"
#include "sylar/core/log/log.h"
#include "sylar/core/module.h"

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

RockServer::RockServer(const std::string &type, sylar::IOManager *worker,
                       sylar::IOManager *io_worker, sylar::IOManager *accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
{
    m_type = type;
}

// 新连接回调：
// - 创建 RockSession，并将业务事件分发到 ModuleMgr::ROCK 模块
// - 安装 request/notify 处理器
// - 启动 session（进入异步读写循环）
void RockServer::handleClient(Socket::ptr client)
{
    SYLAR_LOG_DEBUG(g_logger) << "handleClient " << *client;
    sylar::RockSession::ptr session = std::make_shared<sylar::RockSession>(client);
    session->setWorker(m_worker);
    ModuleMgr::GetInstance()->foreach (Module::ROCK,
                                       [session](Module::ptr m) { m->onConnect(session); });
    session->setDisconnectCb([](AsyncSocketStream::ptr stream) {
        ModuleMgr::GetInstance()->foreach (Module::ROCK,
                                           [stream](Module::ptr m) { m->onDisconnect(stream); });
    });
    session->setRequestHandler([](sylar::RockRequest::ptr req, sylar::RockResponse::ptr rsp,
                                  sylar::RockStream::ptr conn) -> bool {
        // SYLAR_LOG_INFO(g_logger) << "handleReq " << req->toString()
        //                          << " body=" << req->getBody();
        bool rt = false;
        ModuleMgr::GetInstance()->foreach (Module::ROCK, [&rt, req, rsp, conn](Module::ptr m) {
            if (rt) {
                return;
            }
            rt = m->handleRequest(req, rsp, conn);
        });
        return rt;
    });
    session->setNotifyHandler([](sylar::RockNotify::ptr nty, sylar::RockStream::ptr conn) -> bool {
        // SYLAR_LOG_INFO(g_logger) << "handleNty " << nty->toString()
        //                          << " body=" << nty->getBody();
        bool rt = false;
        ModuleMgr::GetInstance()->foreach (Module::ROCK, [&rt, nty, conn](Module::ptr m) {
            if (rt) {
                return;
            }
            rt = m->handleNotify(nty, conn);
        });
        return rt;
    });
    session->start();
}

} // namespace sylar
