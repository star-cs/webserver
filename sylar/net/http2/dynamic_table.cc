#include "dynamic_table.h"
#include <sstream>

namespace sylar::http2
{ // HTTP/2 静态表 RFC 7541 规范

#define STATIC_HEADERS(XX)                                                                         \
    XX("", "")                                                                                     \
    XX(":authority", "")                                                                           \
    XX(":method", "GET")                                                                           \
    XX(":method", "POST")                                                                          \
    XX(":path", "/")                                                                               \
    XX(":path", "/index.html")                                                                     \
    XX(":scheme", "http")                                                                          \
    XX(":scheme", "https")                                                                         \
    XX(":status", "200")                                                                           \
    XX(":status", "204")                                                                           \
    XX(":status", "206")                                                                           \
    XX(":status", "304")                                                                           \
    XX(":status", "400")                                                                           \
    XX(":status", "404")                                                                           \
    XX(":status", "500")                                                                           \
    XX("accept-charset", "")                                                                       \
    XX("accept-encoding", "gzip, deflate")                                                         \
    XX("accept-language", "")                                                                      \
    XX("accept-ranges", "")                                                                        \
    XX("accept", "")                                                                               \
    XX("access-control-allow-origin", "")                                                          \
    XX("age", "")                                                                                  \
    XX("allow", "")                                                                                \
    XX("authorization", "")                                                                        \
    XX("cache-control", "")                                                                        \
    XX("content-disposition", "")                                                                  \
    XX("content-encoding", "")                                                                     \
    XX("content-language", "")                                                                     \
    XX("content-length", "")                                                                       \
    XX("content-location", "")                                                                     \
    XX("content-range", "")                                                                        \
    XX("content-type", "")                                                                         \
    XX("cookie", "")                                                                               \
    XX("date", "")                                                                                 \
    XX("etag", "")                                                                                 \
    XX("expect", "")                                                                               \
    XX("expires", "")                                                                              \
    XX("from", "")                                                                                 \
    XX("host", "")                                                                                 \
    XX("if-match", "")                                                                             \
    XX("if-modified-since", "")                                                                    \
    XX("if-none-match", "")                                                                        \
    XX("if-range", "")                                                                             \
    XX("if-unmodified-since", "")                                                                  \
    XX("last-modified", "")                                                                        \
    XX("link", "")                                                                                 \
    XX("location", "")                                                                             \
    XX("max-forwards", "")                                                                         \
    XX("proxy-authenticate", "")                                                                   \
    XX("proxy-authorization", "")                                                                  \
    XX("range", "")                                                                                \
    XX("referer", "")                                                                              \
    XX("refresh", "")                                                                              \
    XX("retry-after", "")                                                                          \
    XX("server", "")                                                                               \
    XX("set-cookie", "")                                                                           \
    XX("strict-transport-security", "")                                                            \
    XX("transfer-encoding", "")                                                                    \
    XX("user-agent", "")                                                                           \
    XX("vary", "")                                                                                 \
    XX("via", "")                                                                                  \
    XX("www-authenticate", "")

/**
 * @brief 静态表存储 - 根据RFC 7541规范预定义的HTTP头部字段
 * @details 使用宏展开的方式初始化静态头部字段表
 */
static std::vector<std::pair<std::string, std::string> > s_static_headers = {
#define XX(k, v) {k, v},
    STATIC_HEADERS(XX)
#undef XX
};

/**
 * @brief 根据索引获取静态表中的头部字段
 * @param idx 索引值，范围0-61
 * @return 包含头部名称和值的键值对
 */
std::pair<std::string, std::string> DynamicTable::GetStaticHeaders(uint32_t idx)
{
    return s_static_headers[idx];
}

/**
 * @brief 根据头部名称查找其在静态表中的索引
 * @param name 头部字段名称
 * @return 找到的索引值，未找到则返回-1
 */
int32_t DynamicTable::GetStaticHeadersIndex(const std::string &name)
{
    for (int i = 1; i < (int)s_static_headers.size(); ++i) {
        if (s_static_headers[i].first == name) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 根据头部名称和值查找其在静态表中的索引和匹配状态
 * @param name 头部字段名称
 * @param val 头部字段值
 * @return 包含索引值和是否完全匹配的pair
 *         first: 索引值或-1（未找到）
 *         second: true表示名称和值都匹配，false表示只匹配名称
 */
std::pair<int32_t, bool> DynamicTable::GetStaticHeadersPair(const std::string &name,
                                                            const std::string &val)
{
    std::pair<int32_t, bool> rt = std::make_pair(-1, false);
    for (int i = 0; i < (int)s_static_headers.size(); ++i) {
        if (s_static_headers[i].first == name) {
            if (rt.first == -1) {
                rt.first = i;
            }
        } else {
            continue;
        }
        if (s_static_headers[i].second == val) {
            rt.first = i;
            rt.second = true;
            break;
        }
    }
    return rt;
}

/**
 * @brief DynamicTable类的构造函数
 * @details 初始化动态表的最大大小和当前大小
 *         - 最大大小默认为4KB（4*1024字节）
 *         - 当前大小初始化为0
 */
DynamicTable::DynamicTable() : m_maxDataSize(4 * 1024), m_dataSize(0)
{
}

/**
 * @brief 更新动态表，添加新的头部字段
 * @param name 头部字段名称
 * @param value 头部字段值
 * @return 0表示成功
 * @note 该方法会根据动态表的最大大小自动淘汰旧的条目
 */
int32_t DynamicTable::update(const std::string &name, const std::string &value)
{
    int len = name.length() + value.length() + 32;
    while ((m_dataSize + len) > m_maxDataSize && !m_datas.empty()) {
        auto &p = m_datas[0];
        m_dataSize -= p.first.length() + p.second.length() + 32;
        m_datas.erase(m_datas.begin());
    }
    m_dataSize += len;
    m_datas.push_back(std::make_pair(name, value));
    return 0;
}

/**
 * @brief 根据头部名称查找其在静态表或动态表中的索引
 * @param name 头部字段名称
 * @return 找到的索引值，未找到则返回-1
 * @note 索引62及以上对应动态表中的条目，索引1-61对应静态表中的条目
 */
int32_t DynamicTable::findIndex(const std::string &name) const
{
    int32_t idx = GetStaticHeadersIndex(name);
    if (idx == -1) {
        size_t len = m_datas.size() - 1;
        for (size_t i = 0; i < m_datas.size(); ++i) {
            if (m_datas[len - i].first == name) {
                idx = i + 62;
                break;
            }
        }
    }
    return idx;
}

/**
 * @brief 根据头部名称和值查找其在静态表或动态表中的索引和匹配状态
 * @param name 头部字段名称
 * @param value 头部字段值
 * @return 包含索引值和是否完全匹配的pair
 *         first: 索引值或-1（未找到）
 *         second: true表示名称和值都匹配，false表示只匹配名称
 */
std::pair<int32_t, bool> DynamicTable::findPair(const std::string &name,
                                                const std::string &value) const
{
    auto p = GetStaticHeadersPair(name, value);
    if (!p.second) {
        size_t len = m_datas.size() - 1;
        for (size_t i = 0; i < m_datas.size(); ++i) {
            if (m_datas[len - i].first == name) {
                if (p.first == -1) {
                    p.first = i + 62;
                }
            } else {
                continue;
            }
            if (m_datas[len - i].second == value) {
                p.first = i + 62;
                p.second = true;
                break;
            }
        }
    }
    return p;
}

/**
 * @brief 根据索引获取头部字段的名称和值
 * @param idx 索引值
 * @return 包含头部名称和值的键值对
 *         如果索引无效，返回空的键值对
 */
std::pair<std::string, std::string> DynamicTable::getPair(uint32_t idx) const
{
    if (idx < 62) {
        return GetStaticHeaders(idx);
    }
    idx -= 62;
    if (idx < m_datas.size()) {
        return m_datas[m_datas.size() - idx - 1];
    }
    return std::make_pair("", "");
}

/**
 * @brief 根据索引获取头部字段的名称
 * @param idx 索引值
 * @return 头部字段的名称
 *         如果索引无效，返回空字符串
 */
std::string DynamicTable::getName(uint32_t idx) const
{
    return getPair(idx).first;
}

/**
 * @brief 将动态表的状态转换为字符串表示
 * @return 包含动态表状态信息的字符串
 * @note 主要用于调试和日志记录
 */
std::string DynamicTable::toString() const
{
    std::stringstream ss;
    ss << "[DynamicTable max_data_size=" << m_maxDataSize << " data_size=" << m_dataSize << "]"
       << std::endl;
    int idx = 62;
    for (int i = m_datas.size() - 1; i >= 0; --i) {
        ss << "\t" << idx++ << ":" << m_datas[i].first << " - " << m_datas[i].second << std::endl;
    }
    return ss.str();
}

} // namespace sylar::http2