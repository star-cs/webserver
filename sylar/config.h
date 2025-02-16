#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include "log.h"

#include <string>
#include <sstream>
#include <memory>
#include <boost/lexical_cast.hpp>       // 字符串与目标类型之间的转换
#include <iostream>
#include <yaml-cpp/yaml.h>
#include <functional>

#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "util.h"

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
class ConfigVarBase{
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;

    ConfigVarBase(const std::string& name , const std::string& description = "")
        : m_name(name), m_description(description){
        std::transform(m_name.begin() , m_name.end(), m_name.begin(), ::tolower);
    }

    const std::string getName() const {return m_name;}
    const std::string getDescription () const {return m_description;}

    virtual ~ConfigVarBase(){ }
    virtual std::string toString() = 0;
    virtual bool fromString(const std::string& val) = 0;
    virtual std::string getTypeName() const = 0;        //获取 m_val 的类型名
private:
    std::string m_name;
    std::string m_description;
};

// 知识点：模板类的定义 放在 头文件。这是因为模板类的实例化需要编译器在编译时知道完整的模板定义。

// 基础类型 序列化 反序列化
template<class F, class T>
class LexicalCast{
public:
    T operator() (const F& val){
        return boost::lexical_cast<T>(val);
    };
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
template<class T>
class LexicalCast<std::string, std::vector<T> >{        // FromString
public:
    std::vector<T> operator() (const std::string& val){
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::vector<T> vec_T;
        for(size_t i = 0 ; i < node.size() ; ++i){
            ss.str("");
            ss << node[i];
            vec_T.push_back(LexicalCast<std::string, T>()(ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return vec_T;
    }
};

template<class T>   
class LexicalCast<std::vector<T>, std::string>{     // ToString
public:
    std::string operator() (const std::vector<T>& val){
        YAML::Node node;
        for(auto& it : val){
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// list<F> 类型
template<class T>
class LexicalCast<std::string, std::list<T> >{        // FromString
public:
    std::list<T> operator() (const std::string& val){
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::list<T> list_T;
        for(size_t i = 0 ; i < node.size() ; ++i){
            ss.str("");
            ss << node[i];
            list_T.push_back(LexicalCast<std::string, T>()(ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return list_T;
    }
};

template<class T>   
class LexicalCast<std::list<T>, std::string>{     // ToString
public:
    std::string operator() (const std::list<T>& val){
        YAML::Node node;
        for(auto& it : val){
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// set<F> 类型
template<class T>
class LexicalCast<std::string, std::set<T> >{        // FromString
public:
    std::set<T> operator() (const std::string& val){
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::set<T> set_T;
        for(size_t i = 0 ; i < node.size() ; ++i){
            ss.str("");
            ss << node[i];
            set_T.insert(LexicalCast<std::string, T>()(ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return set_T;
    }
};

template<class T>   
class LexicalCast<std::set<T>, std::string>{     // ToString
public:
    std::string operator() (const std::set<T>& val){
        YAML::Node node;
        for(auto& it : val){
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// unordered_set <F> 类型
template<class T>
class LexicalCast<std::string, std::unordered_set<T> >{        // FromString
public:
    std::unordered_set<T> operator() (const std::string& val){
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::unordered_set<T> unordered_set_T;
        for(size_t i = 0 ; i < node.size() ; ++i){
            ss.str("");
            ss << node[i];
            unordered_set_T.insert(LexicalCast<std::string, T>()(ss.str())); //这里的LexicalCast<std::string, T> >不需要typename
        }
        return unordered_set_T;
    }
};

template<class T>   
class LexicalCast<std::unordered_set<T>, std::string>{     // ToString
public:
    std::string operator() (const std::unordered_set<T>& val){
        YAML::Node node;
        for(auto& it : val){
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// map<F> 类型
template<class T>
class LexicalCast<std::string, std::map<std::string, T> > {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

template<class T>   
class LexicalCast<std::map<std::string, T>, std::string>{     // ToString
public:
    std::string operator() (const std::map<std::string, T>& val){
        YAML::Node node;
        for(auto& i : val) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// unordered_map<F> 类型
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> >{        // FromString
public:
    std::unordered_map<std::string, T> operator() (const std::string& val){
        YAML::Node node = YAML::Load(val);
        std::stringstream ss;
        std::unordered_map<std::string, T> un_map_str_T;
        for(auto it = node.begin(); it != node.end(); ++it){
            ss.str("");
            ss << it->second;
            un_map_str_T.insert(std::make_pair(it->first.Scalar() , LexicalCast<std::string, T>()(ss.str())));
        }
        return un_map_str_T;
    }
};



template<class T>   
class LexicalCast<std::unordered_map<std::string, T>, std::string>{     // ToString
public:
    std::string operator() (const std::unordered_map<std::string, T>& val){
        YAML::Node node;
        for(auto& i : val) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// FromString   T operator()(const std::string&)
// ToString     std::string operator(const T&)
template<class T, class FromString = LexicalCast<std::string, T>
                ,  class ToString = LexicalCast<T, std::string> >
class ConfigVar : public ConfigVarBase{
public:
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;

    ConfigVar(const std::string& name, const T& val, const std::string& description = "")
    : ConfigVarBase(name, description), m_val(val){

    }

    std::string toString() override{
        try{
            return ToString()(m_val);
        }catch(std::exception& e){
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception"
                    << e.what() << " convert: " << typeid(m_val).name() << " to string";
        }
        return "";
    }

    bool fromString(const std::string& val) override{
        try{
            setValue(FromString()(val));
        }catch(std::exception& e){
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception "
                << e.what() << " convert: string to " << getTypeName()
                << " - " << val;
        }
        return false;
    }

    const T getValue() const {return m_val;}

    void setValue(const T& val) {
        if(val == m_val){   // T 类型 需要 operator==
            return;
        }
        for(auto& it : m_cbs){
            it.second(m_val, val);  // 调用相关的事件更改 通知 仿函数。
        }
        m_val = val;
    }

    std::string getTypeName() const override {return typeid(m_val).name();}

    void addListener(uint64_t key , on_change_cb fun){
        m_cbs[key] = fun;
    }

    on_change_cb getListener(uint64_t key){
        auto it = m_cbs.find(key);
        return it == nullptr ? nullptr : it->second;
    }

    void delListener(uint64_t key){
        m_cbs.erase(key);
    }
private:
    T m_val;
    // 需要一个map，管理不同多个的 事件通知函数
    std::map<uint64_t, on_change_cb> m_cbs; 
};

/**
 * Config 类 处理 yaml 配置文件的解析
 * ConfigVarMap 存储每一个 name 对应的 value 的 ConfigVar<T>实例指针
 */
class Config{
public:
    // 如果改成 string : ConfigVar<T> 就只能是 T 单一类型了。
    // 所以 map 的键值不能为模板类，需要一个基类
    typedef std::map<std::string, ConfigVarBase::ptr> ConfigVarMap;

    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name, 
        const T& default_value, const std::string& description = ""){
        // auto temp = Lockup<T>(name);   
        auto it = GetDatas().find(name);
        if(it != GetDatas().end()){
            auto temp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
            if(temp){
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name = " << name << " exists";
                return temp;
            } else {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name = " << name << " exists but type is not "<< typeid(T).name() 
                                << " real_type = " << it->second->getTypeName() << " " << it->second->toString();
                return nullptr;
            }
        }    
        
        // find_first_not_of 方法查找第一个不属于指定 字符集 的字符位置。
        if(name.find_first_not_of("abcdefghijklmnopqrstuvwsyz._1234567890") != std::string::npos){
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lockup name invalid" << name;
            throw std::invalid_argument(name);
        }
        
        //初始化 新的 键值对
        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    // 
    template <class T>
    static typename ConfigVar<T>::ptr Lockup(const std::string& name){
        auto it = GetDatas().find(name);
        if(it == GetDatas().end()){
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }

    static void LoadFromYaml(const YAML::Node& root);
    static ConfigVarBase::ptr LookupBase(const std::string& name);

private:
    static ConfigVarMap& GetDatas(){
        static ConfigVarMap s_datas;
        return s_datas;
    }  
};

}; // namespace sylar

#endif