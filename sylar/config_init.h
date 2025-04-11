#ifndef __SYLAR_CONFIG_INIT_H
#define __SYLAR_CONFIG_INIT_H

#include "log.h"

namespace sylar{


    struct LogAppenderDefine
    {
        AppenderType::Type type;  
        LogLevel::Level level = LogLevel::UNKNOW;       // 如果没有设置Appender的level，那么就用Logger的。
        std::string pattern;
        std::string file;
        FlushRule::Rule flush_rule;
        size_t max_size;
        size_t max_file;

        bool operator==(const LogAppenderDefine& oth) const {
            return type == oth.type && 
                pattern == oth.pattern && 
                file == oth.file; 
        }
    };
    
    struct BufMgrDefine{
        std::string work_type = "";
        AsyncType::Type type = AsyncType::Type::ASYNC_SAFE;
        size_t size = 0;
        size_t threshold = 0;
        size_t linear_growth = 0;
        size_t swap_time = 0;

        bool operator==(const BufMgrDefine& oth) const{
            return work_type == oth.work_type
                && type == oth.type 
                && size == oth.size 
                && threshold == oth.threshold 
                && linear_growth == oth.linear_growth 
                && swap_time == oth.swap_time;
        }

        std::vector<std::string> ValidateBufMgrDefine() const {
            std::vector<std::string> errors;
            
            if(size <= 0) 
                errors.emplace_back("size必须大于0");
            
            // 非安全模式时才检查 threshold 和 linear_growth
            if(type != AsyncType::Type::ASYNC_SAFE) {
                if(threshold <= size) 
                    errors.emplace_back("threshold必须大于size");
                if(linear_growth <= 0) 
                    errors.emplace_back("linear_growth必须大于0");
            }
            
            if(swap_time <= 0) 
                errors.emplace_back("swap_time必须大于0");
            if(work_type.empty()) 
                errors.emplace_back("work_type不能为空");
            if(type == AsyncType::Type::UNKNOW) 
                errors.emplace_back("type不能为UNKNOW");

            return errors;
        }

        bool isValid() const {
            return ValidateBufMgrDefine().empty();
        } 
    };

    struct LogDefine
    {
        std::string name;
        BufMgrDefine bufMgr;
        LogLevel::Level level = LogLevel::UNKNOW;
        std::vector<LogAppenderDefine> appenders;
    
        bool operator==(const LogDefine& oth) const {
            return name == oth.name && level == oth.level && bufMgr == oth.bufMgr && appenders == oth.appenders;
        }
    
        // set 需要重载 < 
        bool operator < (const LogDefine& oth) const {
            return name < oth.name;
        }
    };


}

#endif