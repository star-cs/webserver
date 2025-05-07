#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "env.h"

namespace sylar{
    
// Config::ConfigVarMap Config::s_datas;    即使是这样，s_datas静态的初始化可能还是会晚于Lookup

static void ListAllMember(const std::string& prefix,
                            const YAML::Node& node,
                            std::list<std::pair<std::string, const YAML::Node> >& output){
    if(prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos){  
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Conifg invalid name:" << prefix << ":" << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if(node.IsMap()){
        for(auto it = node.begin();
                it != node.end();
                ++it){
            std::string cur_name = it->first.Scalar();
            std::transform(cur_name.begin(), cur_name.end(), cur_name.begin(), ::tolower);
            ListAllMember(prefix.empty() ? cur_name : prefix + "." + cur_name, it->second, output);
        }
    }
} 

ConfigVarBase::ptr Config::LookupBase(const std::string& name){
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}



void Config::LoadFromYaml(const YAML::Node& root){
    std::list<std::pair<std::string, const YAML::Node> > all_notes;
    ListAllMember("", root, all_notes);     // 把yaml的结构 全部 通用成 a.b.c - node 相当于消除map结构

    for(auto& i : all_notes){
        if(i.first.empty()){
            continue;
        }

        std::string key = i.first;
        std::transform(key.begin() , key.end() , key.begin() , ::tolower);
        ConfigVarBase::ptr val_ptr = Config::LookupBase(key);       // 查找之前约定好的 s_datas

        if(val_ptr){
            if(i.second.IsScalar()){ // 标量，基础类型。
                val_ptr->fromString(i.second.Scalar());
            }else{  // 数组 或 映射，转成string，在后续的可以使用 YAML::Load转回来，遍历构造 对应类型T。
                std::stringstream ss;   
                ss << i.second; 
                val_ptr->fromString(ss.str());
            }
        }
    }
}

/// 记录每个文件的修改时间
static std::map<std::string, uint64_t> s_file2modifytime;
/// 是否强制加载配置文件，非强制加载的情况下，如果记录的文件修改时间未变化，则跳过该文件的加载
static sylar::Mutex s_mutex;

void Config::LoadFromConfDir(const std::string &path, bool force){
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, absoulte_path, ".yml");
    std::vector<YAML::Node> temp_nodes; // 需要先worker的
    for (auto &i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);
            sylar::Mutex::Lock lock(s_mutex);
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            if(root["workers"].IsDefined()){        /// 先注册 workers 
                LoadFromYaml(root);
            }else{
                temp_nodes.push_back(std::move(root));
            }
            std::cout << "LoadConfFile file=" << i << " ok" << std::endl;
        } catch (...) {
            std::cout << "LoadConfFile file=" << i << " failed" << std::endl;
        }
    }
    for(auto& i : temp_nodes){
        LoadFromYaml(i);
    }
}


void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb){
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& map = GetDatas();
    for(auto it = map.begin() ; it != map.end() ; it++){
        cb(it->second);
    }
}

} // namespace sylar