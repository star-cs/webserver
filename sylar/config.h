#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include "log.h"

#include <string>
#include <sstream>
#include <memory>
#include <boost/lexical_cast.hpp>       // 字符串与目标类型之间的转换
#include <iostream>
#include <map>

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
template<class T>
class ConfigVar : public ConfigVarBase{
public:
    typedef std::shared_ptr<ConfigVar> ptr;

    ConfigVar(const std::string& name, const T& val, const std::string& description = "")
    : ConfigVarBase(name, description), m_val(val){

    }

    std::string toString() override{
        try{
            return boost::lexical_cast<std::string>(m_val);
        }catch(std::exception& e){
            SYLAR_LOG_ERROR(SYLAR_ROOT()) << "ConfigVar::toString exception"
                    << e.what() << " convert: " << typeid(m_val).name() << " to string";
        }
        return "";
    }

    bool fromString(const std::string& val) override{
        try{
            m_val = boost::lexical_cast<T>(val);
        }catch(std::exception& e){
            SYLAR_LOG_ERROR(SYLAR_ROOT()) << "ConfigVar::toString exception"
                << e.what() << " convert: string to" << typeid(m_val).name()
                << " - " << val;
        }
        return false;
    }

    const T getValue() const {return m_val;}
    void setValue(const T& val) {m_val = val;}

    std::string getTypeName() const override {return typeid(m_val).name();}
private:
    T m_val;
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
        auto temp = Lockup<T>(name);       
        if(temp){// 找到了
            SYLAR_LOG_INFO(SYLAR_ROOT()) << "Lookup name = " << name << " exists";
            return temp; 
        }
        
        // find_first_not_of 方法查找第一个不属于指定 字符集 的字符位置。
        if(name.find_first_not_of("abcdefghijklmnopqrstuvwsyzABCDEFGHIJKLMNOPQRSTUVWSYZ._1234567890") != std::string::npos){
            SYLAR_LOG_ERROR(SYLAR_ROOT()) << "Lockup name invalid" << name;
            throw std::invalid_argument(name);
        }

        //初始化 新的 键值对
        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        s_datas[name] = v;
        return v;
    }

    // 
    template <class T>
    static typename ConfigVar<T>::ptr Lockup(const std::string& name){
        auto it = s_datas.find(name);
        if(it == s_datas.end()){
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }

private:
    static ConfigVarMap s_datas;  
};


} // namespace sylar



#endif