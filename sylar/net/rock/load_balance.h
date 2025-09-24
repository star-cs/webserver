#ifndef __SYLAR_STREAMS_SOCKET_STREAM_POOL_H__
#define __SYLAR_STREAMS_SOCKET_STREAM_POOL_H__

// Rock 生态使用的连接负载均衡基础设施
//
// 组成：
// - HolderStats/HolderStatsSet：按时间片统计单连接调用指标
// - LoadBalanceItem：封装一个后端连接与其统计信息
// - LoadBalance 接口与多种策略（轮询/权重/公平）
// - SDLoadBalance：对接服务发现，维护 domain/service -> LoadBalance 映射
#include "sylar/net/streams/socket_stream.h"
#include "sylar/core/mutex.h"
#include "sylar/core/util/util.h"
#include "sylar/net/streams/service_discovery.h"
#include <vector>
#include <unordered_map>

namespace sylar
{

class HolderStatsSet;
// 单时间片内的连接统计，用于衡量权重与健康度
class HolderStats
{
    friend class HolderStatsSet;

public:
    uint32_t getUsedTime() const { return m_usedTime; }
    uint32_t getTotal() const { return m_total; }
    uint32_t getDoing() const { return m_doing; }
    uint32_t getTimeouts() const { return m_timeouts; }
    uint32_t getOks() const { return m_oks; }
    uint32_t getErrs() const { return m_errs; }

    uint32_t incUsedTime(uint32_t v) { return sylar::Atomic::addFetch(m_usedTime, v); }
    uint32_t incTotal(uint32_t v) { return sylar::Atomic::addFetch(m_total, v); }
    uint32_t incDoing(uint32_t v) { return sylar::Atomic::addFetch(m_doing, v); }
    uint32_t incTimeouts(uint32_t v) { return sylar::Atomic::addFetch(m_timeouts, v); }
    uint32_t incOks(uint32_t v) { return sylar::Atomic::addFetch(m_oks, v); }
    uint32_t incErrs(uint32_t v) { return sylar::Atomic::addFetch(m_errs, v); }

    uint32_t decDoing(uint32_t v) { return sylar::Atomic::subFetch(m_doing, v); }
    void clear();

    // 计算当前片段的权重（经验公式，可调参）
    float getWeight(float rate = 1.0f);

    std::string toString();

    void add(const HolderStats &hs);

    // 结合全局统计与加入时间，计算“公平权重”
    uint64_t getWeight(const HolderStats &hs, uint64_t join_time);

private:
    uint32_t m_usedTime = 0;
    uint32_t m_total = 0;
    uint32_t m_doing = 0;
    uint32_t m_timeouts = 0;
    uint32_t m_oks = 0;
    uint32_t m_errs = 0;
};

// 多时间片统计集合（滑动窗口）
class HolderStatsSet
{
public:
    HolderStatsSet(uint32_t size = 5);
    HolderStats &get(const uint32_t &now = time(0));

    // 滑动窗口加权后的总体权重
    float getWeight(const uint32_t &now = time(0));

    HolderStats getTotal() const;

private:
    void init(const uint32_t &now);

private:
    uint32_t m_lastUpdateTime = 0; // seconds
    std::vector<HolderStats> m_stats;
};

// 负载均衡的基本单元：一个后端连接及其统计信息
class LoadBalanceItem
{
public:
    typedef std::shared_ptr<LoadBalanceItem> ptr;
    virtual ~LoadBalanceItem() {}

    SocketStream::ptr getStream() const { return m_stream; }
    void setStream(SocketStream::ptr v) { m_stream = v; }

    void setId(uint64_t v) { m_id = v; }
    uint64_t getId() const { return m_id; }

    HolderStats &get(const uint32_t &now = time(0));
    const HolderStatsSet &getStatsSet() const { return m_stats; }

    template <class T>
    std::shared_ptr<T> getStreamAs()
    {
        return std::dynamic_pointer_cast<T>(m_stream);
    }

    virtual int32_t getWeight() { return m_weight; }
    void setWeight(int32_t v) { m_weight = v; }

    // 连接是否可用（已建立且存活）
    virtual bool isValid();
    void close();

    std::string toString();

    uint64_t getDiscoveryTime() const { return m_discoveryTime; }

protected:
    uint64_t m_id = 0;
    SocketStream::ptr m_stream;
    HolderStatsSet m_stats;
    int32_t m_weight = 0;
    uint64_t m_discoveryTime = time(0);
};

// 负载均衡策略接口
class ILoadBalance
{
public:
    enum Type { UNKNOW = 0, ROUNDROBIN = 1, WEIGHT = 2, FAIR = 3 };

    enum Error {
        NO_SERVICE = -101,
        NO_CONNECTION = -102,
    };
    typedef std::shared_ptr<ILoadBalance> ptr;
    virtual ~ILoadBalance() {}
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) = 0;
};

// 负载均衡公共基类：维护 items 集合与并发读写
class LoadBalance : public ILoadBalance
{
public:
    typedef sylar::RWSpinlock RWMutexType;
    typedef std::shared_ptr<LoadBalance> ptr;
    void add(LoadBalanceItem::ptr v);
    void del(LoadBalanceItem::ptr v);
    void set(const std::vector<LoadBalanceItem::ptr> &vs);

    LoadBalanceItem::ptr getById(uint64_t id);
    void update(const std::unordered_map<uint64_t, LoadBalanceItem::ptr> &adds,
                std::unordered_map<uint64_t, LoadBalanceItem::ptr> &dels);
    void init();

    std::string statusString(const std::string &prefix);

    void checkInit();

protected:
    // 由子类重建内部选择结构（如数组/前缀和）
    virtual void initNolock() = 0;

protected:
    RWMutexType m_mutex;
    std::unordered_map<uint64_t, LoadBalanceItem::ptr> m_datas;
    uint64_t m_lastInitTime = 0;
};

// 轮询策略：按顺序选择下一个可用连接
class RoundRobinLoadBalance : public LoadBalance
{
public:
    typedef std::shared_ptr<RoundRobinLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) override;

protected:
    virtual void initNolock() override;

protected:
    std::vector<LoadBalanceItem::ptr> m_items;
};

////class FairLoadBalance;
// class FairLoadBalanceItem : public LoadBalanceItem {
////friend class FairLoadBalance;
// public:
//     typedef std::shared_ptr<FairLoadBalanceItem> ptr;
//
//     void clear();
//     virtual int32_t getWeight();
// };

// 权重策略：使用前缀和与 upper_bound 按权重随机
class WeightLoadBalance : public LoadBalance
{
public:
    typedef std::shared_ptr<WeightLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) override;

protected:
    virtual void initNolock() override;

private:
    int32_t getIdx(uint64_t v = -1);

protected:
    std::vector<LoadBalanceItem::ptr> m_items;
    std::vector<int64_t> m_weights;
};

// 公平策略：基于统计动态计算权重，继承权重选择流程
class FairLoadBalance : public WeightLoadBalance
{
public:
    typedef std::shared_ptr<FairLoadBalance> ptr;

protected:
    virtual void initNolock() override;
};

class SDLoadBalance
{
public:
    typedef std::shared_ptr<SDLoadBalance> ptr;
    typedef std::function<SocketStream::ptr(const std::string &domain, const std::string &service,
                                            ServiceItemInfo::ptr)>
        stream_callback;
    typedef sylar::RWSpinlock RWMutexType;

    SDLoadBalance(IServiceDiscovery::ptr sd);
    virtual ~SDLoadBalance() {}

    virtual void start();
    virtual void stop();
    virtual bool doQuery();
    virtual bool doRegister();

    stream_callback getCb() const { return m_cb; }
    void setCb(stream_callback v) { m_cb = v; }

    LoadBalance::ptr get(const std::string &domain, const std::string &service,
                         bool auto_create = false);

    void initConf(const std::unordered_map<std::string,
                                           std::unordered_map<std::string, std::string> > &confs);

    std::string statusString();

    template <class Conn>
    std::shared_ptr<Conn> getConnAs(const std::string &domain, const std::string &service,
                                    uint32_t idx = -1)
    {
        auto lb = get(domain, service);
        if (!lb) {
            return nullptr;
        }
        auto conn = lb->get(idx);
        if (!conn) {
            return nullptr;
        }
        return conn->getStreamAs<Conn>();
    }

private:
    void onServiceChange(const std::string &domain, const std::string &service,
                         const std::unordered_map<uint64_t, ServiceItemInfo::ptr> &old_value,
                         const std::unordered_map<uint64_t, ServiceItemInfo::ptr> &new_value);

    ILoadBalance::Type getType(const std::string &domain, const std::string &service);
    LoadBalance::ptr createLoadBalance(ILoadBalance::Type type);
    LoadBalanceItem::ptr createLoadBalanceItem(ILoadBalance::Type type);

private:
    void refresh();

protected:
    RWMutexType m_mutex;
    IServiceDiscovery::ptr m_sd;
    // domain -> [ service -> [ LoadBalance ] ]
    std::unordered_map<std::string, std::unordered_map<std::string, LoadBalance::ptr> > m_datas;
    std::unordered_map<std::string, std::unordered_map<std::string, ILoadBalance::Type> > m_types;
    // ILoadBalance::Type m_defaultType = ILoadBalance::FAIR;
    stream_callback m_cb;

    sylar::Timer::ptr m_timer;
    std::string m_type;
    bool m_isRefresh = false;
};

} // namespace sylar

#endif
