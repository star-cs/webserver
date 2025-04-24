#include "address.h"
#include "endian.h"
#include "log.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

namespace sylar
{
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// **************** Address ****************


// 得到类似于 0000.0000.0011.1111，末尾有bits个1
template <class T>
static T CreateMask(uint32_t bits){
    return (1 << (sizeof(T) * 8 - bits)) - 1;
}

// 计算一个整数的二进制表示中1的个数
template <class T>
static uint32_t CountBytes(T value){
    uint32_t result = 0;
    for(; value; ++result){
        // 逐次消除最右侧的1
        value &= (value-1);
    }
    return result;
}


Address::ptr Address::Create(const sockaddr *addr, socklen_t addrlen){
    if(addr == nullptr){
        return nullptr;
    }

    Address::ptr result;
    switch(addr->sa_family){
    case AF_INET:
        result.reset(new IPv4Address(*(const sockaddr_in*)addr));
        break;

    case AF_INET6:
        result.reset(new IPv6Address(*(const sockaddr_in6*)addr));
        break;

    default:
        result.reset(new UnknownAddress(*addr));
        break;
    }
    return result;
}


bool Address::Lookup(std::vector<Address::ptr> &result, const std::string &host, int family, int type, int protocol)
{
    addrinfo hints, *results, *next;
    hints.ai_flags = 0;
    hints.ai_family = family;
    hints.ai_socktype = type;
    hints.ai_protocol  = protocol;
    hints.ai_addrlen   = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr      = NULL;
    hints.ai_next      = NULL;

    std::string node;
    const char* service = NULL;     // 端口/方法

    // IPv6  [xx:xx:xx:xx]:service
    if(!host.empty() && host[0] == '['){
        const char* endipv6 = (const char*)memchr(host.c_str()+1, ']', host.size()-1);
        if(endipv6){
            if(*(endipv6 + 1) == ':'){
                service = endipv6 + 2;
            }
            node = host.substr(1, endipv6 - host.c_str() - 1);
        }
    }
    // IPv4  example.com:80  192.168.1.1:8080
    if(node.empty()){
        service = (const char*)memchr(host.c_str(), ':', host.size());
        if(service){
            if(!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)){
                node = host.substr(0, service - host.c_str());
                ++service;
            }
        }
    }
    // 纯主机名 或 IP地址 如 "localhost" 或 "192.168.1.1"
    if(node.empty()){
        node = host;
    }
    /**
     * getaddrinfo
     * 1. 接收 node（主机名/IP）、service（端口/协议名）及 hints（过滤条件）
     * 2. 数字格式 IP直接解析 IPv4/IPv6 地址，跳过 DNS 查询；反之 优先读取 /etc/hosts 中的静态映射
     * 3. DNS 域名解析，（先查 hosts 文件，再查 DNS），向 DNS 服务器发起递归查询，通过 UDP 53 端口发送 DNS 请求，接收 DNS 响应并解析 IP 地址列表
     * 4. 服务名解析，服务名（如 http）转换为端口号（如 80）
     * 5. 根据 hints 参数（如 ai_family=AF_INET）过滤 IP 类型，生成 addrinfo 结构链表
     */
    int error = getaddrinfo(node.c_str(), service, &hints, &results);
    if (error) {
        SYLAR_LOG_DEBUG(g_logger) << "Address::Lookup getaddress(" << host << ", "
                                  << family << ", " << type << ") err=" << error << " errstr="
                                  << gai_strerror(error);
        return false;
    }
    
    next = results;
    while(next){
        result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
        // ip/端口 可以对应多个套接字类型，比如SOCK_STREAM，SOCK_DGRAM，SOCK_RAW
        SYLAR_LOG_DEBUG(g_logger) << "family:" << next->ai_family << ", sock type:" << next->ai_socktype;
        next = next->ai_next;
    }

    freeaddrinfo(results);
    return !result.empty();
}

Address::ptr Address::LookupAny(const std::string &host, int family, int type, int protocol)
{
    std::vector<Address::ptr> result;
    if(Lookup(result, host, family, type, protocol)){
        return result[0];
    }
    return nullptr;
}

std::shared_ptr<IPAddress> Address::LookupAnyIPAddress(const std::string &host, int family, int type, int protocol)
{
    std::vector<Address::ptr> result;
    if(Lookup(result, host, family, type, protocol)){
        for(auto &i : result){
            IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
            if(v){
                return v;
            }
        }
    }
    return nullptr;
}

bool Address::GetInterfaceAddresses(std::multimap<std::string, std::pair<Address::ptr, uint32_t>> &result,
                                    int family) {
    struct ifaddrs *next, *results;
    if (getifaddrs(&results) != 0) {
        SYLAR_LOG_DEBUG(g_logger) << "Address::GetInterfaceAddresses getifaddrs "
                                     " err="
                                  << errno << " errstr=" << strerror(errno);
        return false;
    }

    try {
        for (next = results; next; next = next->ifa_next) {
            Address::ptr addr;
            uint32_t prefix_len = ~0u;
            if (family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
                continue;
            }
            switch (next->ifa_addr->sa_family) {
            case AF_INET: {
                addr             = Create(next->ifa_addr, sizeof(sockaddr_in));
                uint32_t netmask = ((sockaddr_in *)next->ifa_netmask)->sin_addr.s_addr;
                prefix_len       = CountBytes(netmask);
            } break;
            case AF_INET6: {
                addr              = Create(next->ifa_addr, sizeof(sockaddr_in6));
                in6_addr &netmask = ((sockaddr_in6 *)next->ifa_netmask)->sin6_addr;
                prefix_len        = 0;
                for (int i = 0; i < 16; ++i) {
                    prefix_len += CountBytes(netmask.s6_addr[i]);
                }
            } break;
            default:
                break;
            }

            if (addr) {
                result.insert(std::make_pair(next->ifa_name,
                                             std::make_pair(addr, prefix_len)));
            }
        }
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "Address::GetInterfaceAddresses exception";
        freeifaddrs(results);
        return false;
    }
    freeifaddrs(results);
    return !result.empty();
}

/**
 * @brief 获取指定网络接口的地址列表。
 * 
 * 该函数根据指定的网络接口名称和地址族类型，获取对应的网络地址信息，并将结果存储在 `result` 中。
 * 
 * @param result 存储获取到的地址列表，每个元素是一个 pair，包含一个 Address 指针和一个 uint32_t 类型的值（通常表示前缀长度）。
 * @param iface 网络接口名称。如果为空字符串或 "*"，则返回默认的 IPv4 和/或 IPv6 地址。
 * @param family 地址族类型，可以是 AF_INET（IPv4）、AF_INET6（IPv6）或 AF_UNSPEC（同时支持 IPv4 和 IPv6）。
 * @return bool 返回 true 表示成功获取地址列表，false 表示失败。
 */
bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>> &result, const std::string &iface, int family)
{
    // 如果接口名称为空或为 "*"，则根据地址族类型添加默认的 IPv4 和/或 IPv6 地址。
    if(iface.empty() || iface == "*"){
        if(family == AF_INET || family == AF_UNSPEC){
            result.push_back(std::make_pair(Address::ptr(new IPv4Address()), 0u));
        }
        if(family == AF_INET6 || family == AF_UNSPEC){
            result.push_back(std::make_pair(Address::ptr(new IPv6Address()), 0u));
        }
        return true;
    }

    // 使用一个多映射存储所有接口的地址信息。
    std::multimap<std::string, std::pair<Address::ptr, uint32_t>> results;

    // 调用辅助函数获取所有接口的地址信息，如果失败则返回 false。
    if(!GetInterfaceAddresses(results, family)){
        return false;
    }

    // 查找指定接口名称的所有地址信息，并将其添加到结果列表中。
    auto its = results.equal_range(iface);
    for(; its.first != its.second ; ++its.first){
        result.push_back(its.first->second);
    }

    // 如果结果列表不为空，则返回 true，否则返回 false。
    return !result.empty();
}

int Address::getFamily() const{
    return getAddr()->sa_family;
}

std::string Address::toString() const{
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

/**
 * @brief 重载小于运算符，用于比较两个Address对象的大小。
 * 
 * 该函数首先比较两个Address对象的地址内容，如果地址内容不同，则根据内容的大小关系返回结果。
 * 如果地址内容相同，则比较两个Address对象的地址长度，长度较小的对象被认为更小。
 * 
 * @param rhs 要与之比较的另一个Address对象。
 * @return bool 如果当前对象小于rhs对象，则返回true；否则返回false。
 */
bool Address::operator<(const Address &rhs) const {
    // 获取两个地址的最小长度，用于比较
    socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
    
    // 比较两个地址的内容
    int result = memcmp(getAddr(), rhs.getAddr(), minlen);
    
    // 如果内容不同，根据比较结果返回
    if (result < 0) {
        return true;
    } else if (result > 0) {
        return false;
    }
    
    // 如果内容相同，比较地址长度，长度较小的对象被认为更小
    if (getAddrLen() < rhs.getAddrLen()) {
        return true;
    }
    
    // 如果地址内容和长度都相同，则返回false
    return false;
}

/**
 * @brief 等于函数
 */
bool Address::operator==(const Address &rhs) const{
    return getAddrLen() == rhs.getAddrLen() && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

/**
 * @brief 不等于函数
 */
bool Address::operator!=(const Address &rhs) const{
    return !(*this == rhs);
}


// **************** IPAddress ****************

IPAddress::ptr IPAddress::Create(const char *address, uint16_t port){
    addrinfo hints, *results;
    memset(&hints, 0, sizeof(addrinfo));

    // 设置getaddrinfo的提示信息，要求地址必须是数字形式，且不指定地址族（IPv4或IPv6）
    hints.ai_flags  = AI_NUMERICHOST;   // address必须是数字地址字符串
    hints.ai_family = AF_UNSPEC;        // 不指定

    // 调用getaddrinfo解析地址字符串
    int error = getaddrinfo(address, NULL, &hints, &results);
    if (error) {
        // 如果解析失败，记录错误日志并返回nullptr
        SYLAR_LOG_DEBUG(g_logger) << "IPAddress::Create(" << address
                                  << ", " << port << ") error=" << error
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return nullptr;
    }

    try {
        // 使用解析结果创建Address对象，并尝试将其转换为IPAddress对象
        IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(
            Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
        if (result) {
            // 如果转换成功，设置端口号
            result->setPort(port);
        }
        // 释放getaddrinfo分配的内存
        freeaddrinfo(results);
        return result;
    } catch (...) {
        // 如果发生异常，释放getaddrinfo分配的内存并返回nullptr
        freeaddrinfo(results);
        return nullptr;
    }
}


// **************** IPv4Address ****************
IPv4Address::ptr IPv4Address::Create(const char *address, uint16_t port){
    IPv4Address::ptr rt(new IPv4Address);
    rt->m_addr.sin_port = byteswapOnLittleEndian(port);
    int result          = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if (result <= 0) {
        SYLAR_LOG_DEBUG(g_logger) << "IPv4Address::Create(" << address << ", "
                                  << port << ") rt=" << result << " errno=" << errno
                                  << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv4Address::IPv4Address(const sockaddr_in &address){
    m_addr = address;
}

IPv4Address::IPv4Address(uint32_t address, uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = byteswapOnLittleEndian(port);
    m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
}
    
const sockaddr *IPv4Address::getAddr() const{
    return (sockaddr*)&m_addr;
}
    
sockaddr *IPv4Address::getAddr(){
    return (sockaddr*)&m_addr; 
}

socklen_t IPv4Address::getAddrLen() const{
    return sizeof(m_addr);
}

std::ostream &IPv4Address::insert(std::ostream &os) const{
    uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
    os  << ((addr >> 24) & 0xff) << "."
        << ((addr >> 16) & 0xff) << "."
        << ((addr >> 8)  & 0xff) << "."
        << (addr & 0xff);
    os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
    return os;
}


IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len){
    if(prefix_len > 32){
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr |= byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));

    return IPv4Address::ptr(new IPv4Address(baddr));
}

IPAddress::ptr IPv4Address::networkAddress(uint32_t prefix_len){
    if(prefix_len > 32){
        return nullptr;
    }

    sockaddr_in naddr(m_addr);
    naddr.sin_addr.s_addr &= byteswapOnLittleEndian(~CreateMask<uint32_t>(prefix_len));
    return  IPv4Address::ptr(new IPv4Address(naddr));
}

IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len){
    sockaddr_in subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin_family = AF_INET;
    subnet.sin_addr.s_addr = byteswapOnLittleEndian(~CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(subnet));
}

uint32_t IPv4Address::getPort() const{
    return byteswapOnLittleEndian(m_addr.sin_port);
}    

void IPv4Address::setPort(uint16_t v){
    m_addr.sin_port = byteswapOnLittleEndian(v);
}



// **************** IPv6Address ****************

IPv6Address::ptr IPv6Address::Create(const char *address, uint16_t port){
    IPv6Address::ptr rt(new IPv6Address);
    rt->m_addr.sin6_port = byteswapOnLittleEndian(port);
    int result           = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
    if (result <= 0) {
        SYLAR_LOG_DEBUG(g_logger) << "IPv6Address::Create(" << address << ", "
                                  << port << ") rt=" << result << " errno=" << errno
                                  << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv6Address::IPv6Address(){
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
}

IPv6Address::IPv6Address(const sockaddr_in6 &address){
    m_addr = address;
}

IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port){
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port = byteswapOnLittleEndian(port);
    memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
}
    
const sockaddr *IPv6Address::getAddr() const{
    return (sockaddr*)&m_addr;
}
    
sockaddr *IPv6Address::getAddr(){
    return (sockaddr*)&m_addr; 
}

socklen_t IPv6Address::getAddrLen() const{
    return sizeof(m_addr);
}

std::ostream &IPv6Address::insert(std::ostream &os) const{
    os << "[";
    // uint16_t __u6_addr16[8];     8 * 16位
    uint16_t *addr = (uint16_t *)m_addr.sin6_addr.s6_addr;
    bool used_zeros = false;
    for(size_t i = 0; i < 8; ++i){
        if(addr[i] == 0 && !used_zeros){    //折叠
            continue;
        }
        if(i && addr[i-1] == 0 && !used_zeros){ // 遇到了非0，且之前有0
            os << ":";
            used_zeros = true;
        }
        if(i){
            os << ":";
        }
        // std::hex 设置十六进制
        // std::dec 恢复十进制
        os << std::hex << (int)byteswapOnLittleEndian(addr[i]) << std::dec;
    }

    if(!used_zeros && addr[7] == 0){
        os << "::";
    }

    os << "]:" << byteswapOnLittleEndian(m_addr.sin6_port);
    return os;
}


IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len){
    sockaddr_in6 baddr(m_addr);
    baddr.sin6_addr.s6_addr[prefix_len / 8] |= CreateMask<uint8_t>(prefix_len % 8);
    for(int i = prefix_len / 8 + 1; i < 16 ; i++){
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}

IPAddress::ptr IPv6Address::networkAddress(uint32_t prefix_len){
    sockaddr_in6 naddr(m_addr);
    naddr.sin6_addr.s6_addr[prefix_len / 8] &= CreateMask<uint8_t>(prefix_len % 8);
    for (int i = prefix_len / 8 + 1; i < 16; ++i) {
        naddr.sin6_addr.s6_addr[i] = 0x00;
    }
    return IPv6Address::ptr(new IPv6Address(naddr));
}
IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len){
    sockaddr_in6 subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin6_family = AF_INET6;
    subnet.sin6_addr.s6_addr[prefix_len / 8] = ~CreateMask<uint8_t>(prefix_len % 8);
    for (uint32_t i = 0; i < prefix_len / 8; ++i) {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(subnet));
}

uint32_t IPv6Address::getPort() const{
    return byteswapOnLittleEndian(m_addr.sin6_port);
}    

void IPv6Address::setPort(uint16_t v){
    m_addr.sin6_port = byteswapOnLittleEndian(v);
}



// **************** UnixAddress ****************

static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

UnixAddress::UnixAddress(){
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    // offsetof 计算结构体中某个成员相对于结构体起始地址的偏移量
    // offsetof(sockaddr_un, sun_path) 这里算的是 sun_family 的大小
    m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixAddress::UnixAddress(const std::string &path){
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    m_length = path.size() + 1;
    
    if(!path.empty() && path[0] == '\0'){
        --m_length;
    }

    if(m_length > sizeof(m_addr.sun_path)){
        throw std::logic_error("path too long");
    }

    memcpy(m_addr.sun_path, path.c_str(), m_length);
    m_length += offsetof(sockaddr_un, sun_path);
}

const sockaddr *UnixAddress::getAddr() const{
    return (sockaddr*)&m_addr;
}

sockaddr *UnixAddress::getAddr(){
    return (sockaddr*)&m_addr; 
}

socklen_t UnixAddress::getAddrLen() const{
    return m_length;
}

void UnixAddress::setAddrLen(uint32_t v){
    m_length = v;
}

std::string UnixAddress::getPath() const{
    std::stringstream ss;
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') {
        ss << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    } else {
        ss << m_addr.sun_path;
    }
    return ss.str();
}

std::ostream &UnixAddress::insert(std::ostream &os) const{
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') {
        return os << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    }
    return os << m_addr.sun_path;
}


// **************** UnknownAddress ****************
UnknownAddress::UnknownAddress(int family){
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

UnknownAddress::UnknownAddress(const sockaddr &addr){
    m_addr = addr;
}

const sockaddr *UnknownAddress::getAddr() const{
    return (sockaddr*)&m_addr;
}

sockaddr *UnknownAddress::getAddr(){
    return &m_addr;
}

socklen_t UnknownAddress::getAddrLen() const{
    return sizeof(m_addr);
}

std::ostream &UnknownAddress::insert(std::ostream &os) const{
    os << "[UnknowAddress family = " << m_addr.sa_family << "]";
    return os;
}

} // namespace sylar
