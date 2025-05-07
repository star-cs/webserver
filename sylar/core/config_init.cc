#include <yaml-cpp/yaml.h>

#include "config_init.h"
#include "config.h"
#include "log.h"
#include "iomanager.h"
#include "worker.h"

namespace sylar
{
// 从配置文件中加载日志配置
template<>
class LexicalCast<std::string, LogDefine>{
public:
    LogDefine operator() (const std::string& str){
        YAML::Node node = YAML::Load(str);
        LogDefine ld;
        if(!node["name"].IsDefined()){
            std::cout << "log config error: name is null, " << node << std::endl;
            throw std::logic_error("log config name is null");
        }
        ld.name = node["name"].as<std::string>();
        ld.level = LogLevel::FromString(node["level"].IsDefined() ? node["level"].as<std::string>() : "UNKNOW");

        if(node["buf_mgr"].IsDefined()){
            auto buf_mgr_node = node["buf_mgr"];
            BufMgrDefine bmd;
            
            // 必须全部定义好
            bmd.work_type = buf_mgr_node["work_type"].IsDefined() ? buf_mgr_node["work_type"].as<std::string>() : "";
            bmd.type = buf_mgr_node["type"].IsDefined() ? AsyncType::FromString(buf_mgr_node["type"].as<std::string>()) : AsyncType::Type::UNKNOW;
            bmd.size = buf_mgr_node["size"].IsDefined() ? buf_mgr_node["size"].as<size_t>() : 0;
            bmd.threshold = buf_mgr_node["threshold"].IsDefined() ? buf_mgr_node["threshold"].as<size_t>() : 0;
            bmd.linear_growth = buf_mgr_node["linear_growth"].IsDefined() ? buf_mgr_node["linear_growth"].as<size_t>() : 0;
            bmd.swap_time = buf_mgr_node["swap_time"].IsDefined() ? buf_mgr_node["swap_time"].as<size_t>() : 0;
            
            auto errors = bmd.ValidateBufMgrDefine();
            if(!errors.empty()) {
                std::cout << "buf_mgr配置错误 (" << bmd.work_type << "):" << std::endl;
                for(const auto& err : errors) {
                    std::cout << "  * " << err << std::endl;
                }
                throw std::invalid_argument("缓冲区管理器配置验证失败");
            }

            ld.bufMgr = bmd;
        }

        if(node["appenders"].IsDefined()){  // 对应 YAML 数组结构
            LogAppenderDefine lad;
            for(size_t i = 0 ; i < node["appenders"].size() ; ++i){
                auto it = node["appenders"][i];
                if(!it["type"].IsDefined()){
                    std::cout << "log config error: appender type is null, " << it << std::endl;
                    throw std::logic_error("log config appender type is null");
                    continue;
                }
                lad.type = AppenderType::FromString(it["type"].as<std::string>());
                switch(lad.type)
                {
                    case AppenderType::Type::StdoutLogAppender:
                        if(it["pattern"].IsDefined()){
                            lad.pattern = it["pattern"].as<std::string>();
                        }
                        break;
                    case AppenderType::Type::FileLogAppender:
                        if(it["pattern"].IsDefined()){
                            lad.pattern = it["pattern"].as<std::string>();
                        }
                        if(!it["file"].IsDefined()){
                            std::cout << "log config error: FilelogAppender file is null, " << it << std::endl;
                            continue;
                        }
                        lad.file = it["file"].as<std::string>();
                        lad.flush_rule = it["flush_rule"].IsDefined() ? FlushRule::FromString(it["flush_rule"].as<std::string>()) : FlushRule::Rule::FFLUSH;
                        break;
                    case AppenderType::Type::RotatingFileLogAppender:
                        if(it["pattern"].IsDefined()){
                            lad.pattern = it["pattern"].as<std::string>();
                        }
                        if(!it["file"].IsDefined()){
                            std::cout << "log config error: FilelogAppender file is null, " << it << std::endl;
                            continue;
                        }
                        lad.file = it["file"].as<std::string>();
                        lad.flush_rule = it["flush_rule"].IsDefined() ? FlushRule::FromString(it["flush_rule"].as<std::string>()) : FlushRule::Rule::FFLUSH;
                        if(!it["max_size"].IsDefined()){
                            std::cout << "log config error: RotatingFileLogAppender max_size is null, " << it << std::endl;
                        }
                        lad.max_size = it["max_size"].as<size_t>();
                        lad.max_file = it["max_file"].IsDefined() ? it["max_file"].as<size_t>() : 0;  // 0，就是无限增加
                        break;
                    default:
                        std::cout << "log config error: appender type is invalid, " << it << std::endl;
                }
                
                
                if(it["level"].IsDefined()){    
                    // 强制要求 appender的level >= logger的level。
                    LogLevel::Level tem = LogLevel::FromString(it["level"].as<std::string>());
                    lad.level = std::max(tem, ld.level);
                }else{
                    lad.level = ld.level;       // 如果没有定义Appender的level，就使用logger的
                }
                ld.appenders.push_back(lad);
            }
        }
        return ld;
    }
};

template<>
class LexicalCast<LogDefine, std::string>{
public:
    std::string operator()(const LogDefine& ld){
        YAML::Node node;
        node["name"] = ld.name;
        node["level"] = LogLevel::ToString(ld.level);
        if(ld.bufMgr.isValid()){
            YAML::Node buf_mgr_node;
            buf_mgr_node["work_type"] = ld.bufMgr.work_type;
            buf_mgr_node["type"] = AsyncType::ToString(ld.bufMgr.type);
            buf_mgr_node["size"] = ld.bufMgr.size;
            buf_mgr_node["threshold"] = ld.bufMgr.threshold;
            buf_mgr_node["linear_growth"] = ld.bufMgr.linear_growth;
            buf_mgr_node["swap_time"] = ld.bufMgr.swap_time;
            node["buf_mgr"] = buf_mgr_node;
        }
        for(auto& it : ld.appenders){
            YAML::Node items;
            switch(it.type){
                case AppenderType::Type::StdoutLogAppender:
                    items["type"] = "StdoutLogAppender";
                    break;
                case AppenderType::Type::FileLogAppender:
                    items["type"] = "FileLogAppender";
                    items["file"] = it.file;
                    items["flush_rule"] = FlushRule::ToString(it.flush_rule);
                    break;
                case AppenderType::Type::RotatingFileLogAppender:
                    items["type"] = "RotatingFileLogAppender";  // 保持与类名一致
                    items["file"] = it.file;
                    items["flush_rule"] = FlushRule::ToString(it.flush_rule);
                    items["max_size"] = it.max_size;
                    items["max_file"] = it.max_file;
                    break;
                default:
                    std::cout << "Unknown appender type" << std::endl;
            }
                
            if(!it.pattern.empty()){
                items["pattern"] = it.pattern;
            }
            items["level"] = LogLevel::ToString(it.level);
            node["appenders"].push_back(items);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

static ConfigVar< std::set<LogDefine> >::ptr g_log_defines = Config::Lookup("logs", std::set<LogDefine>(), "logs");

struct LogIniter{
    LogIniter(){    // 构造函数
        /**
         * 这个函数是，真正的，根据Define的结构体，创建相应的logger，appender。
         */
        g_log_defines->addListener([](const std::set<LogDefine>& old_log, const std::set<LogDefine>& new_log){
            // std::cout << "测试 仿函数调用" << std::endl;
            // 新增
            // 修改
            // 删除
            for(auto& i : new_log){
                auto it = old_log.find(i);  // set<T> 按照 operator< 查找，那么也就是按照LogDefine name排序及查找
                auto builder = std::make_shared<LoggerBuilder>();
                if(it == old_log.end()){
                    // 需要新增
                    builder->setLoggerName(i.name);
                }else {
                    if(!(i == *it)){    // name相同，但存在level或appender不同。
                        builder->setLoggerName(i.name);
                        
                    } else {
                        continue;
                    }
                }
                builder->setLoggerLevel(i.level);
                // 添加appender
                for(auto &a : i.appenders){
                    LogLevel::Level ll = a.level;
                    LogFormatter::ptr lap;
                    if(!a.pattern.empty()){
                        lap.reset(new LogFormatter(a.pattern));
                        if(lap->isError()){ // 如果日志格式有错误
                            std::cout << "< formatter pattern error : " << i.name << " " << a.type << " " << a.pattern << " > " << std::endl;
                            lap.reset(new LogFormatter); // 此时使用默认的。
                        }
                    } else {
                        // 当没有 pattern，那么默认构造的时候，pattern有默认格式赋值。
                        lap.reset(new LogFormatter);
                    }

                    switch (a.type) {
                        case AppenderType::Type::StdoutLogAppender:
                            builder->BuildLogAppender<StdoutLogAppender>(ll, lap);
                            break;
                        case AppenderType::Type::FileLogAppender:
                            builder->BuildLogAppender<FileLogAppender>(a.file, ll, lap, a.flush_rule);
                            break;
                        case AppenderType::Type::RotatingFileLogAppender:
                            builder->BuildLogAppender<RotatingFileLogAppender>(a.file, ll, lap, a.max_size, a.max_file, a.flush_rule);
                            break;
                        default:
                            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Invalid appender type="<< a.type;
                    }
                }
                if(i.bufMgr.isValid()){
                    auto cur_worker = WorkerMgr::GetInstance()->getAsIOManager(i.bufMgr.work_type).get();
                    if(cur_worker == nullptr){
                        std::cout << "请检查work_type, 是否是正确注册的调度器名" << i.bufMgr.work_type << std::endl;
                        continue;
                    }
                    BufferParams bufParams(
                        i.bufMgr.type,
                        i.bufMgr.size,
                        i.bufMgr.threshold,
                        i.bufMgr.linear_growth,
                        i.bufMgr.swap_time,
                        cur_worker
                    );
                    builder->setBufferParams(bufParams);
                }
                LoggerMgr::GetInstance()->addLogger(builder->Build());
            }
            // 以配置文件为主，如果程序里定义了配置文件中未定义的logger，那么把程序里定义的logger设置成无效
            // 新的没，旧的有。需要删除
            for(auto &i : old_log){
                auto it = new_log.find(i);
                if(it == new_log.end()){
                    // 逻辑删除
                    auto logger = SYLAR_LOG_NAME(i.name);
                    logger->setLevel(LogLevel::NOTEST);     // logger的级别足够大，就不会输出事件。
                    logger->clearAppender();
                }
            }
        });
    }
};

//在main函数之前注册配置更改的回调函数
//用于在更新配置时将log相关的配置加载到Config
static LogIniter __log_init;

} // namespace sylar
