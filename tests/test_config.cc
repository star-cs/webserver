#include "sylar/config.h"
#include "sylar/log.h"
#include "sylar/util.h"
#include <yaml-cpp/yaml.h>

#if 0
// 这里 就是 意味着 在 Config.s_datas里添加了  "server.port"字符串  ConfigVar<int>的实例 键值对
sylar::ConfigVar<int>::ptr g_int_value_config = 
    sylar::Config::Lookup("server.port", (int)8080, "server port");

// 存在 K 相关， T 类型不同  需要报错。
// sylar::ConfigVar<float>::ptr g_int_valuex_config = 
//     sylar::Config::Lookup("server.port", (float)8080, "server port");
        

sylar::ConfigVar<std::vector<int> >::ptr g_int_vec_value_config = 
    sylar::Config::Lookup("server.array", std::vector<int>{-1, -1}, "server vec int");

sylar::ConfigVar<std::list<int> >::ptr g_int_list_value_config = 
    sylar::Config::Lookup("server.list", std::list<int>{-1, -1}, "server list int");

sylar::ConfigVar<std::set<int> >::ptr g_int_set_value_config = 
    sylar::Config::Lookup("server.set", std::set<int>{-1, -1}, "server set int");

sylar::ConfigVar<std::unordered_set<int> >::ptr g_int_unordered_set_value_config = 
    sylar::Config::Lookup("server.unordered_set", std::unordered_set<int>{-1, -1}, "server unordered set int");

sylar::ConfigVar<std::map<std::string, int> >::ptr g_str_int_map_value_config = 
    sylar::Config::Lookup("server.map", std::map<std::string, int>{{"sss", 10}}, "server map str-int");

sylar::ConfigVar<std::unordered_map<std::string, int> >::ptr g_str_int_unordered_map_value_config = 
    sylar::Config::Lookup("server.unordered_map", std::unordered_map<std::string, int>{{"sss", 10}}, "server unordered_map str-int");


void test_config(){
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before:" << g_int_value_config->getName();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before:" << g_int_value_config->getValue();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "before:" << g_int_value_config->getDescription();
#define XX(g_value, name, prefix) \
    { \
        auto v = g_value->getValue();  \
        for(auto& i : v){ \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #name " " #prefix " : " << i; \
        } \
    }

#define XXM(g_value, name, prefix) \
    { \
        auto v = g_value->getValue(); \
        for(auto& i : v){ \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #name " " #prefix " : " <<  i.first << "-" << i.second;\
        } \
    }

    XX(g_int_vec_value_config, int_vec, before)
    XX(g_int_list_value_config, int_list , before)
    XX(g_int_set_value_config, int_set , before)
    XX(g_int_unordered_set_value_config, unordered_int_set, before)

    XXM(g_str_int_map_value_config, str_int_map, before)
    XXM(g_str_int_unordered_map_value_config, str_int_unordered_map, before)

    YAML::Node node = YAML::LoadFile("/home/yang/projects/webserver/bin/conf/test.yml");
    std::list<std::pair<std::string, const YAML::Node> > all_nodes;
    sylar::Config::LoadFromYaml(node);

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after:" << g_int_value_config->getName();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after:" << g_int_value_config->getValue();
    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "after:" << g_int_value_config->getDescription();
   
    XX(g_int_vec_value_config, int_vec, after)
    XX(g_int_list_value_config, int_list , after)
    XX(g_int_set_value_config, int_set , after)
    XX(g_int_unordered_set_value_config, unordered_int_set, after)
    XXM(g_str_int_map_value_config, str_int_map, after)
    XXM(g_str_int_unordered_map_value_config, str_int_unordered_map, after)
}
#endif 

class Person{
public: 
    std::string name = "";
    int age = 0;
    bool man = 0;

    std::string toString() const{
        std::stringstream ss;
        ss << "[person:  name:" << name << "agg:" << age << "man:" << man << "]";
        return ss.str(); 
    }
};

namespace sylar{
template<>
class LexicalCast<std::string, Person>{
public:
    Person operator()(const std::string& val){
        YAML::Node node = YAML::Load(val);
        Person p;
        p.name = node["name"].as<std::string>();
        p.age = node["age"].as<int>();
        p.man = node["man"].as<bool>();
        return p;
    }
}; 

template<>
class LexicalCast<Person, std::string>{
public:
    std::string operator()(const Person& person){
        YAML::Node node;
        node["name"] = person.name;
        node["age"] = person.age;
        node["man"] = person.man;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
}; 

}

void test_class(){
    sylar::ConfigVar<Person>::ptr g_person = sylar::Config::Lookup<Person>("class.person", Person(), "class person");
    sylar::ConfigVar<std::map<std::string, Person> >::ptr g_map_person = sylar::Config::Lookup<std::map<std::string, Person> >("class.map_person", std::map<std::string , Person>(), "class map_person");

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << g_person->getValue().toString();

#define XX(ptr, name, prefix) \
    { \
        auto v = ptr->getValue(); \
        for(auto& it : v){ \
            SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << #name " " #prefix " : " << it.first << " - " << it.second.toString(); \
        } \
    }

    XX(g_map_person, map_person, after)

    YAML::Node node = YAML::LoadFile("/home/yang/projects/webserver/bin/conf/test.yml");
    sylar::Config::LoadFromYaml(node);

    XX(g_map_person, map_person, before)

    SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << g_person->getValue().toString();
}

int main(int argc, char** argv){
    // test_config();
    test_class();
    return 0;
}