#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include "sylar/core/log/log.h"

#include <string>
#include <sstream>
#include <memory>
#include <boost/lexical_cast.hpp> // 字符串与目标类型之间的转换
#include <iostream>
#include <yaml-cpp/yaml.h>
#include <functional>

#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "sylar/core/util/util.h"
#include "sylar/core/mutex.h"
#include "sylar/net/pack/yaml_decoder.h"
#include "sylar/net/pack/yaml_encoder.h"

namespace sylar
{
/**
 * 分析：
 * 每一个配置项： name , value, description
 * 其中name和description可以使用 string 表示，但是 value就 涉及到不同的类
 * value 需要 从 string 转成 T 类型（为了初始化）， 还需要 从 T 类型 转成 string（为了输出打印）
 *
 *
 */

// 基类
class ConfigVarBase
{
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;

    ConfigVarBase(const std::string &name, const std::string &description = "")
        : m_name(name), m_description(description)
    {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }

    const std::string getName() const { return m_name; }
    const std::string getDescription() const { return m_description; }

    virtual ~ConfigVarBase() {}
    virtual std::string toString() = 0;
    virtual bool fromString(const std::string &val) = 0;
    virtual std::string getTypeName() const = 0; //获取 m_val 的类型名
private:
    std::string m_name;
    std::string m_description;
};

// 知识点：模板类的定义 放在 头文件。这是因为模板类的实例化需要编译器在编译时知道完整的模板定义。

// 基础类型 序列化 反序列化
template <class F, class T>
class LexicalCast
{
public:
    T operator()(const F &val) { return boost::lexical_cast<T>(val); };
};
/**
 * 知识点 ： YAML 的转换
 *
 * 1. Node 转 string
 * std::stringstream ss;
 * YAML::Node node;
 * ss << node;
 * ss.str();
 *
 *
 * 2. string 转 Node （按照上面的方法得到的string，可以通过Load转回去）
 * YAML::Node YAML::Load(std::string);
 *
 *
 * 3. 构造 Sequence 类型的Node，
 *  可以在 YAML::Node node; node.push_back(YAML::Node); 使用push_back添加Node
 *
 */
// 这些操作行云流水，牛逼
// vector<F> 类型
template <class T>
class LexicalCast<std::string, std::vector<T>>
{ // FromString
public:
    std::vector<T> operator()(const std::string &val)
    {
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::vector<T> vec_T;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec_T.push_back(LexicalCast<std::string, T>()(
                ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return vec_T;
    }
};

template <class T>
class LexicalCast<std::vector<T>, std::string>
{ // ToString
public:
    std::string operator()(const std::vector<T> &val)
    {
        YAML::Node node;
        for (auto &it : val) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// list<F> 类型
template <class T>
class LexicalCast<std::string, std::list<T>>
{ // FromString
public:
    std::list<T> operator()(const std::string &val)
    {
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::list<T> list_T;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            list_T.push_back(LexicalCast<std::string, T>()(
                ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return list_T;
    }
};

template <class T>
class LexicalCast<std::list<T>, std::string>
{ // ToString
public:
    std::string operator()(const std::list<T> &val)
    {
        YAML::Node node;
        for (auto &it : val) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// set<F> 类型
template <class T>
class LexicalCast<std::string, std::set<T>>
{ // FromString
public:
    std::set<T> operator()(const std::string &val)
    {
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::set<T> set_T;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            set_T.insert(LexicalCast<std::string, T>()(
                ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return set_T;
    }
};

template <class T>
class LexicalCast<std::set<T>, std::string>
{ // ToString
public:
    std::string operator()(const std::set<T> &val)
    {
        YAML::Node node;
        for (auto &it : val) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// unordered_set <F> 类型
template <class T>
class LexicalCast<std::string, std::unordered_set<T>>
{ // FromString
public:
    std::unordered_set<T> operator()(const std::string &val)
    {
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::unordered_set<T> unordered_set_T;
        for (size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            unordered_set_T.insert(LexicalCast<std::string, T>()(
                ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return unordered_set_T;
    }
};

template <class T>
class LexicalCast<std::unordered_set<T>, std::string>
{ // ToString
public:
    std::string operator()(const std::unordered_set<T> &val)
    {
        YAML::Node node;
        for (auto &it : val) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// map<F> 类型
template <class T>
class LexicalCast<std::string, std::map<std::string, T>>
{
public:
    std::map<std::string, T> operator()(const std::string &v)
    {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

template <class T>
class LexicalCast<std::map<std::string, T>, std::string>
{ // ToString
public:
    std::string operator()(const std::map<std::string, T> &val)
    {
        YAML::Node node;
        for (auto &i : val) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// unordered_map<F> 类型
template <class T>
class LexicalCast<std::string, std::unordered_map<std::string, T>>
{ // FromString
public:
    std::unordered_map<std::string, T> operator()(const std::string &val)
    {
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::unordered_map<std::string, T> un_map_str_T;
        for (auto it = node.begin(); it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            un_map_str_T.insert(
                std::make_pair(it->first.Scalar(), LexicalCast<std::string, T>()(ss.str())));
        }
        return un_map_str_T;
    }
};

template <class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string>
{ // ToString
public:
    std::string operator()(const std::unordered_map<std::string, T> &val)
    {
        YAML::Node node;
        for (auto &i : val) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief YAML字符串解码转换器模板类
 * @tparam T 目标数据类型
 *
 * 用于将YAML格式的字符串转换为指定类型的对象
 * 通过重载operator()实现函数对象模式，支持类型安全的转换
 */
template <class T>
class PackDecodeCast
{
public:
    /**
     * @brief 将YAML字符串转换为指定类型对象
     * @param str YAML格式的字符串
     * @return T 转换后的对象
     */
    T operator()(const std::string &str)
    {
        T t;                                          // 创建目标类型的默认对象
        sylar::pack::DecodeFromYamlString(str, t, 0); // 从YAML字符串解码到对象
        return t;                                     // 返回转换后的对象
    }
};

/**
 * @brief YAML字符串编码转换器模板类
 * @tparam T 源数据类型
 *
 * 用于将指定类型的对象转换为YAML格式的字符串
 * 通过重载operator()实现函数对象模式，支持类型安全的转换
 */
template <class T>
class PackEncodeCast
{
public:
    /**
     * @brief 将对象转换为YAML字符串
     * @param v 要转换的对象引用
     * @return std::string YAML格式的字符串
     */
    std::string operator()(const T &v) { return sylar::pack::EncodeToYamlString(v, 0); }
};

/**
 * @brief 定义配置变量的宏
 *
 * 用于简化配置变量的定义，自动处理类型转换和注册
 *
 * @param type 配置项的数据类型
 * @param name 配置变量的名称（全局变量名）
 * @param attr 配置项的属性名（在YAML配置文件中的键名）
 * @param def 配置项的默认值
 * @param desc 配置项的描述信息
 *
 * 使用示例：
 * @code
 * SYLAR_DEFINE_CONFIG(std::string, g_server_name, "server.name", "default_server", "服务器名称");
 * SYLAR_DEFINE_CONFIG(int, g_server_port, "server.port", 8080, "服务器端口");
 * @endcode
 */
#define SYLAR_DEFINE_CONFIG(type, name, attr, def, desc)                                           \
    static sylar::ConfigVar<type, sylar::PackDecodeCast<type>, sylar::PackEncodeCast<type>>::ptr   \
        name =                                                                                     \
            sylar::Config::Lookup<type, sylar::PackDecodeCast<type>, sylar::PackEncodeCast<type>>( \
                attr, (type)def, desc);

/**
 * @brief 获取配置变量的宏
 *
 * 用于获取已定义的配置变量，提供类型安全的访问方式
 *
 * @param type 配置项的数据类型
 * @param attr 配置项的属性名（在YAML配置文件中的键名）
 * @return 配置变量的智能指针
 *
 * 使用示例：
 * @code
 * auto server_name = SYLAR_GET_CONFIG(std::string, "server.name");
 * auto server_port = SYLAR_GET_CONFIG(int, "server.port");
 * @endcode
 */
#define SYLAR_GET_CONFIG(type, attr)                                                               \
    sylar::Config::Lookup<type, sylar::PackDecodeCast<type>, sylar::PackEncodeCast<type>>(attr)

// FromString   T operator()(const std::string&)
// ToString     std::string operator(const T&)
template <class T, class FromString = LexicalCast<std::string, T>,
          class ToString = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase
{
public:
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void(const T &old_value, const T &new_value)> on_change_cb;
    typedef RWSpinlock RWMutexType;

    ConfigVar(const std::string &name, const T &val, const std::string &description = "")
        : ConfigVarBase(name, description), m_val(val)
    {
    }

    std::string toString() override
    {
        try {
            return ToString()(getValue());
        } catch (std::exception &e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT())
                << "ConfigVar::toString exception" << e.what()
                << " convert: " << typeid(m_val).name() << " to string";
        }
        return "";
    }

    bool fromString(const std::string &val) override
    {
        try {
            setValue(FromString()(val));
        } catch (std::exception &e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT())
                << "ConfigVar::fromString exception " << e.what() << " convert: string to "
                << getTypeName() << " - " << val;
        }
        return false;
    }

    const T getValue()
    {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val;
    }

    void setValue(const T &val)
    {
        {
            RWMutexType::ReadLock lock(m_mutex);
            if (val == m_val) { // T 类型 需要 operator==
                return;
            }
            for (auto &it : m_cbs) {
                it.second(m_val, val); // 调用相关的事件更改 通知 仿函数。
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        m_val = val;
    }

    std::string getTypeName() const override { return typeid(m_val).name(); }

    int addListener(on_change_cb fun)
    {
        static uint64_t num = 0;
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs[++num] = fun;
        return num;
    }

    on_change_cb getListener(uint64_t key)
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == nullptr ? nullptr : it->second;
    }

    void delListener(uint64_t key)
    {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }

    void clearListener()
    {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.clear();
    }

private:
    T m_val;
    // 需要一个map，管理不同多个的 事件通知函数
    std::map<uint64_t, on_change_cb> m_cbs;
    RWMutexType m_mutex;
};

/**
 * Config 类 处理 yaml 配置文件的解析
 * ConfigVarMap 存储每一个 name 对应的 value 的 ConfigVar<T>实例指针
 */
class Config
{
public:
    // 如果改成 string : ConfigVar<T> 就只能是 T 单一类型了。
    // 所以 map 的键值不能为模板类，需要一个基类
    typedef std::map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    typedef RWSpinlock RWMutexType;

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] default_value 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用default_value赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template <class T, class... Args>
    static typename ConfigVar<T, Args...>::ptr
    Lookup(const std::string &name, const T &default_value, const std::string &description = "")
    {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T, Args...>>(it->second);
            if (tmp) {
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT())
                    << "Lookup name=" << name << " exists but type not " << TypeToName<T>()
                    << " real_type=" << it->second->getTypeName() << " " << it->second->toString();
                return nullptr;
            }
        }

        if (name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T, Args...>::ptr v(
            new ConfigVar<T, Args...>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    /**
     * @brief 查找配置参数
     * @param[in] name 配置参数名称
     * @return 返回配置参数名为name的配置参数
     */
    template <class T, class... Args>
    static typename ConfigVar<T, Args...>::ptr Lookup(const std::string &name)
    {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T, Args...>>(it->second);
    }

    static void LoadFromYaml(const YAML::Node &root);

    static void LoadFromConfDir(const std::string &path, bool force = false);

    static ConfigVarBase::ptr LookupBase(const std::string &name);
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb); // 传入仿函数，用于操作s_datas

private:
    static ConfigVarMap &GetDatas()
    {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    static RWMutexType &GetMutex()
    {
        static RWMutexType m_mutex;
        return m_mutex;
    }
};

}; // namespace sylar

#endif