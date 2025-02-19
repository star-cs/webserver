#include "log.h"
#include <tuple>
#include <map>
#include <fstream>
#include <iostream>
#include <time.h>
#include <stdarg.h>

#include "config.h"
#include "thread.h"
namespace sylar{

/**
     * %m -- 消息体
     * %p -- level
     * %c -- 日志名称
     * %t -- 线程id
     * %n -- 回车换行
     * %d -- 时间
     * %f -- 文件名
     * %l -- 行号
     */

class MessageFormatterItem : public LogFormatter::FormatterItem{
    public:
        MessageFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger ,LogEvent::ptr event) override{
            os << event->getContent();
        }
};

class LevelFormatterItem : public LogFormatter::FormatterItem{
    public:
        LevelFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << LogLevel::ToString(event->getLevel());
        }
};

class ElapseFormatterItem : public LogFormatter::FormatterItem{
    public:
        ElapseFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << event->getElapse();
        }
};

class LoggerNameFormatterItem : public LogFormatter::FormatterItem{
    public:
        LoggerNameFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << logger->getName();
        }
};

class ThreadIdFormatterItem : public LogFormatter::FormatterItem{
    public:
        ThreadIdFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << event->getThreadId();
        }
};

class ThreadNameFormatterItem : public LogFormatter::FormatterItem{
    public:
        ThreadNameFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << Thread::GetName();
        }
};

class FiberIdFormatterItem : public LogFormatter::FormatterItem{
    public:
        FiberIdFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << event->getFiberId();
        }
};

class DataTimeFormatterItem : public LogFormatter::FormatterItem{
    public:
        DataTimeFormatterItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
            :m_format(format){
                if(m_format.empty()){
                    m_format = "%Y-%m-%d %H:%M:%S";
                }
            }
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            // 当前事件获取
            struct tm tm;
            time_t t = event->getTime();
            localtime_r(&t, &tm);
            char buf[64];
            strftime(buf, sizeof(buf), m_format.c_str(), &tm);
            os << buf;
        }

    private:
        std::string m_format;
};

class FilenameFormatterItem : public LogFormatter::FormatterItem{
    public:
        FilenameFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << event->getFile();
        }
};

class LineFormatterItem : public LogFormatter::FormatterItem{
    public:
        LineFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << event->getLine();
        }
};

class NewLineFormatterItem : public LogFormatter::FormatterItem{
    public:
        NewLineFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << std::endl;
        }
};

class StringFormatterItem : public LogFormatter::FormatterItem{
    public:
        StringFormatterItem(const std::string& str): m_string(str){}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << m_string;
        }
    private:
        std::string m_string;
};

class TabFormatterItem : public LogFormatter::FormatterItem{
    public:
        TabFormatterItem(const std::string& str = "") {}
        void format(std::ostream& os, Logger::ptr logger, LogEvent::ptr event) override{
            os << "\t";
        }
};

// LogEvent

LogEvent::LogEvent(const char* file, int32_t line, uint32_t elapse, uint32_t threadId, const std::string& threadName, uint32_t fiberId , uint64_t time , LogLevel::Level level)
    :m_file(file), m_line(line), m_elapse(elapse), m_threadId(threadId), m_threadName(threadName), m_fiberId(fiberId), m_time(time) , m_level(level){
}
    
void LogEvent::format(const char* fmt, ...){
    va_list al;
    va_start(al, fmt);
    format(fmt, al);
    va_end(al);
}

void LogEvent::format(const char* fmt, va_list al){
    char* buf = nullptr;
    int len = vasprintf(&buf, fmt, al);
    if(len != -1){
        m_ss << std::string(buf, len);
        free(buf);
    }
}

// LogLevel
const char* LogLevel::ToString(LogLevel::Level level){
    switch(level){
#define XX(name) \
    case LogLevel::name: \
        return #name; \
        break;

    XX(DEBUG);
    XX(INFO);
    XX(WARN);
    XX(ERROR);
    XX(FATAL);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

/**
 *  UNKNOW = 0,
            DEBUG = 1,
            INFO = 2,
            WARN = 3,
            ERROR = 4,
            FATAL = 5
 */
LogLevel::Level LogLevel::FromString(const std::string& str){
#define XX(level,v) if(str == #v) {return LogLevel::level;}

    XX(DEBUG, debug)
    XX(INFO, info)
    XX(WARN, warn)
    XX(ERROR, error)
    XX(FATAL, fatal)
    
    XX(DEBUG, DEBUG)
    XX(INFO, INFO)
    XX(WARN, WARN)
    XX(ERROR, ERROR)
    XX(FATAL, FATAL)
#undef XX
    return LogLevel::UNKNOW;
}

// Logger
Logger::Logger(const std::string name)
    : m_name(name)
    , m_level(LogLevel::UNKNOW){        // logger默认最低的日志权重，大于等于这个日志权重的事件都要被记录
}

void Logger::addAppender(LogAppender::ptr appender){
    MutexType::Lock lock(m_mutex);
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender){
    MutexType::Lock lock(m_mutex);
    for(auto it = m_appenders.begin(); it != m_appenders.end(); ++it){
        if(*it == appender){
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::clearAppender(){
    MutexType::Lock lock(m_mutex);
    m_appenders.clear();
}

void Logger::log(LogEvent::ptr event){
    if(event->getLevel() >= m_level){
        // todo
        auto self = shared_from_this();
        for(auto& i: m_appenders){
            i->log(self, event);
        }
    }
}

std::string Logger::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = YAML::Load(m_name);
    node["m_level"] = YAML::Load(LogLevel::ToString(m_level));
    for(auto& i : m_appenders){
        node["appenders"].push_back(YAML::Load(i->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// LogAppender
LogAppender::LogAppender(LogLevel::Level level, LogFormatter::ptr formatter)
    : m_level(level), m_formatter(formatter){
}

void LogAppender::setFormatter(LogFormatter::ptr val){
    MutexType::Lock lock(m_mutex);
    m_formatter = val;
}

LogFormatter::ptr LogAppender::getFormatter(){
    MutexType::Lock lock(m_mutex);
    return  m_formatter;
}
    

StdoutLogAppender::StdoutLogAppender(LogLevel::Level level, LogFormatter::ptr formatter)
    : LogAppender(level, formatter){

}

std::string StdoutLogAppender::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    node["pattern"] = m_formatter->getPattern();
    node["level"] = YAML::Load(LogLevel::ToString(m_level));
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void StdoutLogAppender::log(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    MutexType::Lock lock(m_mutex);
    if(event->getLevel() >= m_level){
        std::cout << m_formatter->format(logger, event);
    }
}

FileLogAppender::FileLogAppender(const std::string& filename, LogLevel::Level level, LogFormatter::ptr formatter)
    : LogAppender(level, formatter){ 
    m_filename = filename;
    m_lastTime = 0;
    reopen(time(0), 0);
}

std::string FileLogAppender::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["pattern"] = m_formatter->getPattern();
    node["file"] = m_filename;
    node["level"] = YAML::Load(LogLevel::ToString(m_level));
    std::stringstream ss;
    ss << node;
    return ss.str();
}

bool FileLogAppender::reopen(uint64_t now, uint64_t interval){
    MutexType::Lock lock(m_mutex);
    if(now >= (m_lastTime + interval)){    
        if(m_filestream.is_open()){
            m_filestream.close();
        }

        //追加写入，在原来基础上加了ios::app 
        m_filestream.open(m_filename, std::ios::out | std::ios::app);
        m_reopenError = !m_filestream;
        if (!m_reopenError) {
            m_lastTime = now;
        } else {
            std::cout << "Failed to reopen file " << m_filename << " at time " << now << ": " << strerror(errno) << std::endl;
        }
    }
    return !m_reopenError;
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    time_t now = event->getTime();

    if(event->getLevel() >= m_level){
        if(reopen(now , 3)){
            MutexType::Lock lock(m_mutex);
            m_filestream << m_formatter->format(logger , event);
            m_filestream.flush();
        }
    }
}

//Formmater
LogFormatter::LogFormatter(const std::string& pattern)
    : m_pattern(pattern){
    //解析字符串模板
    init();
}

std::string LogFormatter::format(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    // TODO
    std::stringstream ss;
    for(auto& it : m_items){
        it->format(ss, logger, event);
    }
    return ss.str();
}


void LogFormatter::init(){
    // 例如 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n" 
    // %d {%Y-%m-%d %H:%M:%S}
    // 解析对象分成两类 一类是普通字符串 另一类是可被解析的
    // tuple来定义 需要的格式 std::tuple<std::string,std::string,int> 
    // <符号,子串,类型>  类型0-普通字符串 类型1-可被解析的字符串
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string nstr;
    // 是否解析出错
    bool error = false;

    for(size_t i = 0 ; i < m_pattern.size() ; ++i){
        // 如果不是%，添加到 nstr
        // 仔细思考，这个地方只有 普通字符串会接收到。
        // 正常来说，访问到 '%'，经过查找之后，会直接跳过 一段 %str[fmt]。到下一个'%'或普通字符
        // nstr 保存的就是 普通字符，特别的是，它的添加是和 下一段%str[fmt] 一起添加。
        if(m_pattern[i] != '%'){
            nstr.append(1, m_pattern[i]);
            continue;
        }

        // %%转义字符 --> %
        if((i+1) < m_pattern.size() && m_pattern[i+1] == '%'){
            nstr.append(1, '%');
            continue;
        }

        // m_pattern[i]是% && m_pattern[i + 1] != '%', 需要进行解析
        size_t pos = i+1;   //下一个字符
        int fmt_status = 0;
        size_t fmt_begin = 0;

        std::string str;    // %之后，'{'之前的字符串
        std::string fmt;    // '{}'里的字符
        while(pos < m_pattern.size()){
            // 1. 不是 { } ，那么可能pos移动到了 '%' '_'(空格) '['(普通字符串) 这个时候，str就得到了。
            if(fmt_status == 0 && m_pattern[pos] != '{' && m_pattern[pos] != '}' && !isalpha(m_pattern[pos])){
                str = m_pattern.substr(i+1, pos-i-1);
                break;
            }

            if(fmt_status == 0){
                if(m_pattern[pos] == '{'){
                    str = m_pattern.substr(i+1, pos-i-1);
                    fmt_status = 1;         // 进入'{'
                    fmt_begin = pos;        // '{' 下标
                    ++pos;
                    continue;
                }
            } else if(fmt_status == 1){
                if(m_pattern[pos] == '}'){
                    fmt = m_pattern.substr(fmt_begin+1, pos-fmt_begin-1);
                    fmt_status = 0;
                    ++pos;
                    // 找到了'}'，退出循环
                    break;
                }
            }

            ++pos;
            if(pos == m_pattern.size()){    //如果到了结尾，还是没有str
                if(str.empty()){
                    str = m_pattern.substr(i+1);
                }
            }
        }

        if(fmt_status == 0){
            //1. 前面的普通字符串
            if(!nstr.empty()){
                vec.push_back(std::make_tuple(nstr, std::string() , 0));
                nstr.clear();
            }
            //2. 当前 %str{fmt}
            vec.push_back(std::make_tuple(str, fmt, 1));

            // i 移动到 pos-1，经过for循环再+1。之后 i=pos 跳过前一段 %str{fmt}
            i = pos-1;
        } else if(fmt_status == 1){
            // 没有找到与'{'相对应的'}' 所以解析报错，格式错误
            std::cout << "pattern parse error:" << m_pattern << " - " << m_pattern.substr(i) << std::endl;
            vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));
            error = true;
        }
    }

    if(error){
        m_error = true;
        return;
    }

    //结尾 判断普通字符串
    if(!nstr.empty()){
        vec.push_back(std::make_tuple(nstr, std::string() , 0));
        nstr.clear();
    }

    /**
     * %m -- 消息体
     * %p -- level
     * %r -- 启动后的时间
     * %c -- 日志名称
     * %t -- 线程id
     * %N -- 线程name
     * %F -- 协程id
     * %l -- 行号
     * %d -- 时间
     * %n -- 回车换行
     * %f -- 文件名
     * %T -- Tab
     * %s -- string
     */
    // 这里的map，每个字符对于的仿函数。
    // 普通字符串，DataTime 需要初始化str。
    // 需要输出FormatterItem::ptr对应的子类指针
    static std::map<std::string, std::function<FormatterItem::ptr(const std::string& str)>> s_format_items = {
#define XX(str , C) \
        {#str , [](const std::string& fmt){return FormatterItem::ptr(new C(fmt));}}
        // #str 表示 字符串
        XX(m , MessageFormatterItem),
        XX(p , LevelFormatterItem),
        XX(r , ElapseFormatterItem),
        XX(c , LoggerNameFormatterItem),
        XX(t , ThreadIdFormatterItem),
        XX(N , ThreadNameFormatterItem),
        XX(F , FiberIdFormatterItem),
        XX(l , LineFormatterItem),
        XX(d , DataTimeFormatterItem),
        XX(n , NewLineFormatterItem),
        XX(f , FilenameFormatterItem),
        XX(T , TabFormatterItem),
#undef XX
    };

    // for循环中的auto&用于引用遍历容器中的元素。
    // 把每一种 日志格式 添加到 m_items。
    // 后续直接在 m_items 里面找即可
    for(auto& i : vec){
        if(std::get<2>(i) == 0){
            // 普通字符串输出 的 vec里 三元组 是 <str , "", 1> 
            m_items.push_back(FormatterItem::ptr(new StringFormatterItem(std::get<0>(i))));
        } else {
            auto it = s_format_items.find(std::get<0>(i));
            if(it == s_format_items.end()){
                m_items.push_back(FormatterItem::ptr(new StringFormatterItem("<< error_format %" + std::get<1>(i) + ">>")));
                error = true;
            } else {
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }
        // std::cout << std::get<0>(i) << " - " <<  std::get<1>(i) << " - " << std::get<2>(i) << std::endl;
    }

    if(error){
        m_error = true;
        return;
    }
}

LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
    :m_logger(logger), m_event(event){

}

LogEventWrap::~LogEventWrap(){
    m_logger->log(m_event);
}

LoggerManager::LoggerManager(){
    m_root.reset(new Logger);               // 默认构造Logger，此时level是UNKNOW最低
    // 默认是输出std，跟随m_root的level，以及使用默认的格式器
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender(m_root->getLevel(), LogFormatter::ptr(new LogFormatter()))));  
    m_loggers[m_root->getName()] = m_root;  // 添加到m_loggers
    // 后续，配置项发送改变，能改变这个。
}

/**
 * 如果指定名称的日志器未找到，那么就新创建一个，但是新创建的Logger 不带 Apppender
 * 需要手动添加 Appender
 */
Logger::ptr LoggerManager::getLogger(const std::string& name){
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    if(it != m_loggers.end()){
        return it->second;
    }
    Logger::ptr logger(new Logger(name));
    // 添加一个默认的 std 输出器
    logger->addAppender(LogAppender::ptr(new StdoutLogAppender(m_root->getLevel(), LogFormatter::ptr(new LogFormatter()))));
    m_loggers[name] = logger;
    return logger;
}


void LoggerManager::init(){

}

std::string LoggerManager::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    for(auto& i : m_loggers){
        node.push_back(YAML::Load(i.second->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

////////////////////////////////////////////////////////////////////////////////
// 从配置文件中加载日志配置

struct LogAppenderDefine
{
    int type = 0;   // 1 File, 2 Stdout
    LogLevel::Level level = LogLevel::UNKNOW;       // 如果没有设置Appender的level，那么就用Logger的。
    std::string pattern;
    std::string file;

    bool operator==(const LogAppenderDefine& oth) const {
        return type == oth.type && 
            pattern == oth.pattern && 
            file == oth.file; 
    }
};

struct LogDefine
{
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOW;
    std::vector<LogAppenderDefine> appenders;

    bool operator==(const LogDefine& oth) const {
        return name == oth.name && level == oth.level && appenders == oth.appenders;
    }

    // set 需要重载 < 
    bool operator < (const LogDefine& oth) const {
        return name < oth.name;
    }
};

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

        if(node["appenders"].IsDefined()){  // 对应 YAML 数组结构
            LogAppenderDefine lad;
            for(size_t i = 0 ; i < node["appenders"].size() ; ++i){
                auto it = node["appenders"][i];
                if(!it["type"].IsDefined()){
                    std::cout << "log config error: appender type is null, " << it << std::endl;
                    throw std::logic_error("log config appender type is null");
                    continue;
                }
                std::string type_name = it["type"].as<std::string>();
                if(type_name == "StdoutLogAppender"){
                    lad.type = 2;
                    if(it["pattern"].IsDefined()){
                        lad.pattern = it["pattern"].as<std::string>();
                    }
                }else if(type_name == "FileLogAppender"){
                    lad.type = 1;
                    if(!it["file"].IsDefined()){
                        std::cout << "log config error: FilelogAppender file is null, " << it << std::endl;
                        continue;
                    }
                    lad.file = it["file"].as<std::string>();
                    if(it["pattern"].IsDefined()){
                        lad.pattern = it["pattern"].as<std::string>();
                    }
                } else {
                    std::cout << "log appender config error: appender type is invalid, " << it << std::endl;
                    continue;
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

        for(auto& it : ld.appenders){
            YAML::Node items;
            if(it.type == 1){
                items["type"] = "FileLogAppender";
                items["file"] = it.file;
            }else if(it.type == 2){
                items["type"] = "StdoutLogAppender";
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

sylar::ConfigVar< std::set<LogDefine> >::ptr g_log_defines = sylar::Config::Lookup("logs", std::set<LogDefine>(), "logs");

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
                Logger::ptr logger;
                if(it == old_log.end()){
                    // 需要新增
                    logger = SYLAR_LOG_NAME(i.name);
                }else {
                    if(!(i == *it)){    // name相同，但存在level或appender不同。
                        logger = SYLAR_LOG_NAME(i.name);
                    } else {
                        continue;
                    }
                }
                logger->setLevel(i.level);
                logger->clearAppender();
                //往logger里添加appender
                for(auto &a : i.appenders){
                    LogAppender::ptr ap;
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

                    if(a.type == 1){
                        ap.reset(new FileLogAppender(a.file, ll, lap));
                    } else if(a.type == 2){
                        ap.reset(new StdoutLogAppender(ll, lap));
                    }
                    
                    logger->addAppender(ap);
                }
            }
            // 以配置文件为主，如果程序里定义了配置文件中未定义的logger，那么把程序里定义的logger设置成无效
            // 新的没，旧的有。需要删除
            for(auto &i : old_log){
                auto it = new_log.find(i);
                if(it == new_log.end()){
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

}// namespace sylar
