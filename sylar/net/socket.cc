#include "socket.h"
#include "sylar/core/fd_manager.h"
#include "sylar/core/log.h"
#include "sylar/core/hook.h"
#include "sylar/core/iomanager.h"

namespace sylar{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Socket::ptr Socket::CreateTCP(sylar::Address::ptr address){
    Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDP(sylar::Address::ptr address){
    Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

Socket::ptr Socket::CreateTCPSocket(){
    Socket::ptr sock(new Socket(IPv4, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket(){
    Socket::ptr sock(new Socket(IPv4, UDP, 0));
    return sock;
}

Socket::ptr Socket::CreateTCPSocket6(){
    Socket::ptr sock(new Socket(IPv6, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUDPSocket6(){
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    return sock;
}

Socket::ptr Socket::CreateUnixTCPSocket(){
    Socket::ptr sock(new Socket(UNIX, TCP, 0));
    return sock;
}

Socket::ptr Socket::CreateUnixUDPSocket(){
    Socket::ptr sock(new Socket(UNIX, UDP, 0));
    return sock;
}

Socket::Socket(int family, int type, int protocol)
    : m_sock(-1)
    , m_family(family)
    , m_type(type)
    , m_protocol(protocol)
    , m_isConnected(false) {
}

Socket::~Socket(){
    close();
}

int64_t Socket::getSendTimeout(){
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if(ctx){
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Socket::setSendTimeout(int64_t v){
    struct timeval tv{
        int(v / 1000),
        int(v % 1000 * 1000)
    };
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout(){
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if(ctx){
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}
void Socket::setRecvTimeout(int64_t v){
    struct timeval tv{
        int(v / 1000), 
        int(v % 1000 * 1000),
    };
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::getOption(int level, int option, void *result, socklen_t *len){
    int rt = getsockopt(m_sock, level, option, result, len);
    if (rt) {
        SYLAR_LOG_DEBUG(g_logger) << "getOption sock=" << m_sock
                                  << " level=" << level << " option=" << option
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void *result, socklen_t len){
    if(setsockopt(m_sock, level, option, result, len)){
        SYLAR_LOG_DEBUG(g_logger) << "setOption sock=" << m_sock
                                    << " level=" << level << " option=" << option
                                    << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

Socket::ptr Socket::accept(){
    // 新socket和原socket使用同样的m_family，m_type，m_protocol
    Socket::ptr sock(new Socket(m_family, m_type, m_protocol));
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if(newsock == -1){
        SYLAR_LOG_ERROR(g_logger) << "accept(" << m_sock << ") errno="
                                    << errno << " errstr=" << strerror(errno);
        return nullptr;
    }
    if(sock->init(newsock)){
        return sock;
    }
    return nullptr;
}
bool Socket::bind(const Address::ptr addr){
    m_localAddress = addr;
    if(!isValid()){
        newSock();
        if(SYLAR_UNLIKELY(!isValid())){
            return false;
        }
    }

    if (SYLAR_UNLIKELY(addr->getFamily() != m_family)) {
        SYLAR_LOG_ERROR(g_logger) << "bind sock.family("
                                  << m_family << ") addr.family(" << addr->getFamily()
                                  << ") not equal, addr=" << addr->toString();
        return false;
    }

    /**
     * 处理Unix域套接字地址的连接和清理操作
     * 该函数尝试将给定的地址转换为UnixAddress类型，并创建一个TCP套接字进行连接。
     * 如果连接成功，则返回false；如果连接失败，则删除Unix域套接字文件。
     */
    UnixAddress::ptr uaddr = std::dynamic_pointer_cast<UnixAddress>(addr);
    if(uaddr){
        // 创建一个TCP套接字
        Socket::ptr sock = Socket::CreateTCPSocket();
        
        // 尝试连接到Unix域套接字地址
        if(sock->connect(uaddr)){
            // 连接成功，返回false
            return false;
        }else{
            // 连接失败，删除Unix域套接字文件
            sylar::FSUtil::Unlink(uaddr->getPath(), true);
        }
    }
    if(::bind(m_sock, addr->getAddr(), addr->getAddrLen())){
        SYLAR_LOG_ERROR(g_logger) << "bind error errno=" << errno
                                  << " errstr=" << strerror(errno);
        return false;
    }

    getLocalAddress();
    return true;
}
bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms){
    m_remoteAddress = addr;
    if(!isValid()){
        newSock();
        if(SYLAR_UNLIKELY(!isValid())){
            return false;
        }
    }

    if(SYLAR_UNLIKELY(addr->getFamily() != m_family)){
        SYLAR_LOG_ERROR(g_logger) << "connect sock.family("
                                    << m_family << ") addr.family(" << addr->getFamily()
                                    << ") not equal, addr=" << addr->toString();
        return false;
    }

    if(timeout_ms == (uint64_t)-1){
        if(::connect(m_sock, addr->getAddr(), addr->getAddrLen())){
            SYLAR_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                                      << ") error errno=" << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    }else {
        if(::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(), timeout_ms)){
            SYLAR_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                                      << ") error errno=" << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    }

    m_isConnected = true;
    // 客户端 connect 连接成功后，m_sock绑定了本地地址和远端地址
    getRemoteAddress();
    getLocalAddress();

    return true;
}
bool Socket::reconnect(uint64_t timeout_ms){
    if(!m_remoteAddress){
        SYLAR_LOG_ERROR(g_logger) << "reconnect m_remoteAddress is null";
        return false;
    }
    m_localAddress.reset();
    return connect(m_remoteAddress, timeout_ms);
}
bool Socket::listen(int backlog){
    if (!isValid()) {
        SYLAR_LOG_ERROR(g_logger) << "listen error sock=-1";
        return false;
    }
    if (::listen(m_sock, backlog) == -1) {
        SYLAR_LOG_ERROR(g_logger) << "listen error errno=" << errno
                                  << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::close(){
    if(!m_isConnected && m_sock == -1){
        return true;
    }
    m_isConnected = false;
    if(m_sock != -1){
        ::close(m_sock);
        m_sock = -1;
    }
    return false;
}

int Socket::send(const void *buffer, size_t length, int flags){
    if (isConnected()) {
        return ::send(m_sock, buffer, length, flags);
    }
    return -1;
}
int Socket::send(const iovec *buffers, size_t length, int flags){
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov    = (iovec *)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}
int Socket::sendTo(const void *buffer, size_t length, const Address::ptr to, int flags){
    if (isConnected()) {
        return ::sendto(m_sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
    }
    return -1;
}
int Socket::sendTo(const iovec *buffers, size_t length, const Address::ptr to, int flags){
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov     = (iovec *)buffers;
        msg.msg_iovlen  = length;
        msg.msg_name    = to->getAddr();
        msg.msg_namelen = to->getAddrLen();
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recv(void *buffer, size_t length, int flags){
    if (isConnected()) {
        return ::recv(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::recv(iovec *buffers, size_t length, int flags){
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov    = (iovec *)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recvFrom(void *buffer, size_t length, Address::ptr from, int flags){
    if (isConnected()) {
        socklen_t len = from->getAddrLen();
        return ::recvfrom(m_sock, buffer, length, flags, from->getAddr(), &len);
    }
    return -1;
}

int Socket::recvFrom(iovec *buffers, size_t length, Address::ptr from, int flags){
    if (isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov     = (iovec *)buffers;
        msg.msg_iovlen  = length;
        msg.msg_name    = from->getAddr();
        msg.msg_namelen = from->getAddrLen();
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

Address::ptr Socket::getRemoteAddress(){
    if(m_remoteAddress){
        return m_remoteAddress;
    }

    Address::ptr result;
    switch (m_family) {
    case AF_INET:
        result.reset(new IPv4Address());
        break;
    case AF_INET6:
        result.reset(new IPv6Address());
        break;
    case AF_UNIX:
        result.reset(new UnixAddress());
        break;
    default:
        result.reset(new UnknownAddress(m_family));
        break;
    }

    socklen_t addrlen = result->getAddrLen();
    if(getpeername(m_sock, result->getAddr(), &addrlen)){
        SYLAR_LOG_ERROR(g_logger) << "getsockname error sock=" << m_sock
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }

    if(m_family == AF_UNIX){
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }

    m_remoteAddress = result;
    return result;
}

Address::ptr Socket::getLocalAddress() {
    // 如果已经缓存了本地地址，直接返回
    if(m_localAddress) {
        return m_localAddress;
    }

    // 根据地址族类型创建相应的地址对象
    Address::ptr result;
    switch(m_family) {
    case AF_INET:
        result.reset(new IPv4Address());
        break;
    case AF_INET6:
        result.reset(new IPv6Address());
        break;
    case AF_UNIX:
        result.reset(new UnixAddress());
        break;
    default:
        result.reset(new UnknownAddress(m_family)); 
        break;
    }

    // 获取地址长度
    socklen_t addrlen = result->getAddrLen();

    // 调用 getsockname 获取本地地址
    if(getsockname(m_sock, result->getAddr(), &addrlen)) {
        SYLAR_LOG_ERROR(g_logger) << "getsockname error sock=" << m_sock
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }

    // 如果是 AF_UNIX 地址，需要设置地址长度
    if(m_family == AF_UNIX) {
        // 将 result 转换为 UnixAddress::ptr 类型，以便调用 setAddrLen 方法
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }

    // 缓存本地地址并返回
    m_localAddress = result;
    return m_localAddress;
}

bool Socket::isValid() const{
    return m_sock != -1;
}

int Socket::getError(){
    int error = 0;
    socklen_t len = sizeof(error);
    if(!getOption(SOL_SOCKET, SO_ERROR, &error, &len)){
        error = errno;
    }
    return error;
}

std::ostream &Socket::dump(std::ostream &os) const{
    os  << "[Socket sock=" << m_sock
        << " is_connected=" << m_isConnected
        << " family=" << m_family
        << " type=" << m_type
        << " protocol=" << m_protocol;

    if (m_localAddress) {
        os << " local_address=" << m_localAddress->toString();
    }
    if (m_remoteAddress) {
        os << " remote_address=" << m_remoteAddress->toString();
    }
    os << "]";
    return os;
}

std::string Socket::toString() const{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

bool Socket::cancelRead(){
    return IOManager::GetThis()->cancelEvent(m_sock, IOManager::READ);
}

bool Socket::cancelWrite(){
    return IOManager::GetThis()->cancelEvent(m_sock, IOManager::WRITE);
}

bool Socket::cancelAccept(){
    return IOManager::GetThis()->cancelEvent(m_sock, IOManager::READ);
}

bool Socket::cancelAll(){
    return IOManager::GetThis()->cancelAll(m_sock);
}

void Socket::initSock(){
    int val = 1;
    // 设置套接字选项，允许地址重用（SO_REUSEADDR）
    setOption(SOL_SOCKET, SO_REUSEADDR, val);
    // 避免 TCP 粘包
    // 如果套接字类型为流式套接字（SOCK_STREAM），则禁用Nagle算法（TCP_NODELAY）
    if(m_type == SOCK_STREAM){
        setOption(SOL_SOCKET, TCP_NODELAY, val);
    }
}

void Socket::newSock(){
    m_sock = socket(m_family, m_type, m_protocol);
    if (SYLAR_LIKELY(m_sock != -1)) {
        initSock();
    } else {
        SYLAR_LOG_ERROR(g_logger) << "socket(" << m_family
                                  << ", " << m_type << ", " << m_protocol << ") errno="
                                  << errno << " errstr=" << strerror(errno);
    }
}

bool Socket::init(int sock){
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(sock);
    if(ctx && ctx->isSocket() && !ctx->isClose()){
        m_sock = sock;
        m_isConnected = true;

        initSock();
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

std::ostream &operator<<(std::ostream &os, const Socket &sock){
    return sock.dump(os);
}

}