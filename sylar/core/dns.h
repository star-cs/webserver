#ifndef __SYLAR_DNS_H__
#define __SYLAR_DNS_H__

#include "sylar/core/common/singleton.h"
#include "sylar/net/address.h"
#include "sylar/net/socket.h"
#include "sylar/core/mutex.h"
#include "sylar/core/timermanager.h"
#include <set>

namespace sylar
{

// DNS 解析与连接池管理：
// - TYPE_DOMAIN: 通过域名解析得到地址列表并健康检查
// - TYPE_ADDRESS: 使用配置的静态地址列表并健康检查
// 可选为每个地址维护 Socket 连接池以提升复用效率。

class Dns
{
public:
    typedef std::shared_ptr<Dns> ptr;
    typedef sylar::RWMutex RWMutexType;
    enum Type { TYPE_DOMAIN = 1, TYPE_ADDRESS = 2 };
    // 构造函数: domain 形如 "www.sylar.top:80"
    // type 参见枚举 Type；pool_size>0 表示为每个地址维护连接池
    Dns(const std::string &domain, int type, uint32_t pool_size = 0);

    // 设置静态地址列表（仅当 type == TYPE_ADDRESS 有效）
    void set(const std::set<std::string> &addrs);
    // 轮询返回一个可用地址（依据健康检查状态）
    sylar::Address::ptr get(uint32_t seed = -1);
    // 返回一个可用 Socket（可能来自连接池或新建连接）
    sylar::Socket::ptr getSock(uint32_t seed = -1);

    const std::string &getDomain() const { return m_domain; }
    int getType() const { return m_type; }

    std::string getCheckPath() const { return m_checkPath; }
    void setCheckPath(const std::string &v) { m_checkPath = v; }

    std::string toString();

    // 刷新解析/地址列表并进行健康检查
    void refresh();

public:
    struct AddressItem : public std::enable_shared_from_this<AddressItem> {
        typedef std::shared_ptr<AddressItem> ptr;
        ~AddressItem() {}
        sylar::Address::ptr addr;
        std::list<Socket *> socks;
        sylar::Spinlock m_mutex;
        bool valid = false;
        uint32_t pool_size = 0;
        std::string check_path;

        // 上次健康检查结果
        bool isValid();
        // 健康检查并更新可用状态（含可选的 HTTP 检查）
        bool checkValid(uint32_t timeout_ms);

        // 将底层裸指针 Socket 放回池（内部使用）
        void push(Socket *sock);
        // 从池中弹出一个连接的 Socket（内部使用）
        Socket::ptr pop();
        // 获取一个可用 Socket；优先复用池连接，否则尝试新建
        Socket::ptr getSock();

        std::string toString();
    };

private:
    // 仅在 TYPE_ADDRESS 模式下，根据 m_addrs 初始化地址项
    void init();

    // 根据解析结果构建/更新地址条目并执行健康检查
    void initAddress(const std::vector<Address::ptr> &result);

private:
    std::string m_domain;
    int m_type;
    uint32_t m_idx;
    uint32_t m_poolSize = 0;
    std::string m_checkPath;
    RWMutexType m_mutex;
    std::vector<AddressItem::ptr> m_address;
    std::set<std::string> m_addrs;
};

class DnsManager
{
public:
    typedef sylar::RWMutex RWMutexType;
    void init();

    void add(Dns::ptr v);
    Dns::ptr get(const std::string &domain);

    // service: www.sylar.top:80
    // cache: 是否需要缓存
    sylar::Address::ptr getAddress(const std::string &service, bool cache, uint32_t seed = -1);
    sylar::Socket::ptr getSocket(const std::string &service, bool cache, uint32_t seed = -1);

    void start();

    std::ostream &dump(std::ostream &os);

private:
    RWMutexType m_mutex;
    std::map<std::string, Dns::ptr> m_dns;
    sylar::Timer::ptr m_timer;
    bool m_refresh = false;
    uint64_t m_lastUpdateTime = 0;
};

typedef sylar::Singleton<DnsManager> DnsMgr;

} // namespace sylar

#endif
