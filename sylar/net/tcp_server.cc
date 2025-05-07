#include "tcp_server.h"
#include "sylar/core/config.h"

namespace sylar{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
    
static sylar::ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout = 
    sylar::Config::Lookup<uint64_t>("tcp_server.read_timeout", 60 * 1000 * 2, "tcp read timeout");


TcpServer::TcpServer(IOManager* io_worker, IOManager* accept_work)
    : m_ioWorker(io_worker)
    , m_acceptWorker(accept_work)
    , m_recvTimeout(g_tcp_server_read_timeout->getValue())
    , m_name("sylar/1.0.0")
    , m_type("tcp")
    , m_isStop(true){

}

TcpServer::~TcpServer(){
    for(auto& i : m_socks){
        i->close();
    }
    m_socks.clear();
}

bool TcpServer::bind(sylar::Address::ptr addr){
    std::vector<Address::ptr> addrs;    
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs
                    ,std::vector<Address::ptr>& fails){

    for(auto& addr : addrs){
        Socket::ptr sock = Socket::CreateTCP(addr);
        if(!sock->bind(addr)){
            SYLAR_LOG_ERROR(g_logger) << "TcpServer::bind bind errno = " 
                    << errno << " strerrno = " << strerror(errno)
                    << "addr [ " << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }

        if(!sock->listen()){
            SYLAR_LOG_ERROR(g_logger) << "TcpServer::bind listen errno = " 
                    << errno << " strerrno = " << strerror(errno)
                    << "addr [ " << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        
        m_socks.push_back(sock);
    }

    if(!fails.empty()){
        m_socks.clear();
        return false;
    }

    for(auto& i : m_socks) {
        SYLAR_LOG_INFO(g_logger) << "type=" << m_type
            << " name=" << m_name
            << " server bind success: " << *i;
    }
    return true;

}

bool TcpServer::start(){
    if(!m_isStop){
        return true;
    }

    m_isStop = false;

    for(auto& i : m_socks){
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept, 
                            shared_from_this() , i));
    }
    return true;
}

void TcpServer::stop(){
    m_isStop = true;
    auto self = shared_from_this();
    m_acceptWorker->schedule([this, self](){
        for(auto& i : m_socks){
            i->cancelAll();
        }
        m_socks.clear();
    });
}

std::string TcpServer::toString(const std::string& prefix){
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name
       << " io_worker=" << (m_ioWorker ? m_ioWorker->getName() : "")
       << " accept=" << (m_acceptWorker ? m_acceptWorker->getName() : "")
       << " recv_timeout=" << m_recvTimeout << "]" << std::endl;
    std::string pfx = prefix.empty() ? "    " : prefix;
    for(auto& i : m_socks) {
        ss << pfx << pfx << *i << std::endl;
    }
    return ss.str();
}

void TcpServer::handleClient(Socket::ptr client){
    SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
}

void TcpServer::startAccept(Socket::ptr sock){
    while(!m_isStop){
        Socket::ptr client = sock->accept();

        if(client){
            client->setRecvTimeout(m_recvTimeout);
            m_ioWorker->schedule(std::bind(&TcpServer::handleClient, shared_from_this(), client));
        } else {
            SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno
                << " errstr=" << strerror(errno);
        }
    }
}

}