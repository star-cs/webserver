#include "log.h"
#include <tuple>
#include <map>
#include <fstream>
#include <iostream>
#include <time.h>
#include <stdarg.h>
#include <yaml-cpp/yaml.h>

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
            os << event->getThreadName();
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

// FileLogAppender
FileLogAppender::FileLogAppender(const std::string& filename, LogLevel::Level level, LogFormatter::ptr formatter)
    : LogAppender(level, formatter), m_filename(filename), m_lastTime(0){ 
    FSUtil::Mkdir(FSUtil::Dirname(m_filename));
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

//RollFileLogAppender


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


// 初始化日志格式解析器
void LogFormatter::init() {
    // 解析模式字符串，示例："%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    
    // 存储解析结果的三元组容器：
    // tuple<原始字符串, 格式参数, 类型> 
    // 类型说明：0-普通字符串，1-格式项
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string nstr; // 临时存储普通字符
    bool error = false; // 解析错误标志

    // 遍历模式字符串每个字符
    for(size_t i = 0 ; i < m_pattern.size() ; ++i) {
        // 处理普通字符（非%开头）
        if(m_pattern[i] != '%') {
            nstr.append(1, m_pattern[i]);
            continue;
        }

        // 处理转义字符%%
        if((i+1) < m_pattern.size() && m_pattern[i+1] == '%') {
            nstr.append(1, '%');
            i++; // 跳过下一个%
            continue;
        }

        // 开始解析格式项（如%d{...}）
        size_t pos = i + 1;
        int fmt_status = 0;    // 0-寻找格式项，1-解析{}内参数
        size_t fmt_begin = 0;  // {的位置索引
        std::string str;       // 格式项标识符（如d）
        std::string fmt;       // {}内的格式参数（如%Y-%m-%d）

        while(pos < m_pattern.size()) {
            // 状态0：寻找格式项结束或{
            if(fmt_status == 0 && 
              !isalpha(m_pattern[pos]) && // 非字母字符作为分隔
              m_pattern[pos] != '{' && 
              m_pattern[pos] != '}') {
                str = m_pattern.substr(i+1, pos-i-1);
                break;
            }
            
            // 遇到{开始解析格式参数
            if(fmt_status == 0 && m_pattern[pos] == '{') {
                str = m_pattern.substr(i+1, pos-i-1);
                fmt_status = 1;
                fmt_begin = pos;
                pos++;
                continue;
            }
            
            // 状态1：寻找}结束
            if(fmt_status == 1) {
                if(m_pattern[pos] == '}') {
                    fmt = m_pattern.substr(fmt_begin+1, pos-fmt_begin-1);
                    fmt_status = 0;
                    pos++;
                    break;
                }
            }
            pos++;
            if(pos == m_pattern.size()){    //如果到了结尾，还是没有str
                if(str.empty()){
                    str = m_pattern.substr(i+1);
                }
            }
        }

        // 处理解析结果
        if(fmt_status == 0) {
            // 保存之前的普通字符串
            if(!nstr.empty()) {
                vec.emplace_back(nstr, "", 0);
                nstr.clear();
            }
            // 保存当前格式项
            vec.emplace_back(str, fmt, 1);
            i = pos - 1; // 跳过已解析部分
        } else {
            // {}不匹配的错误处理
            std::cout << "pattern parse error:" << m_pattern << " - " 
                     << m_pattern.substr(i) << std::endl;
            vec.emplace_back("<<pattern_error>>", fmt, 0);
            error = true;
        }
    }

    // 保存最后的普通字符串
    if(!nstr.empty()) {
        vec.emplace_back(nstr, "", 0);
    }

    // 格式项映射表（关键说明）
    static std::map<std::string, std::function<FormatterItem::ptr(const std::string&)>> s_format_items = {
        // 格式说明：
        // %m 消息内容 | %p 日志级别 | %r 耗时(毫秒)
        // %c 日志器名称 | %t 线程ID | %N 线程名称
        // %F 协程ID | %l 行号 | %d 时间 | %n 换行
        // %f 文件名 | %T 制表符
#define XX(str, C) {#str, [](const std::string& fmt) { return FormatterItem::ptr(new C(fmt)); }}
        XX(m, MessageFormatterItem),    // 消息体
        XX(p, LevelFormatterItem),      // 日志级别
        XX(r, ElapseFormatterItem),     // 程序启动后的耗时
        XX(c, LoggerNameFormatterItem), // 日志器名称
        XX(t, ThreadIdFormatterItem),   // 线程ID
        XX(N, ThreadNameFormatterItem), // 线程名称
        XX(F, FiberIdFormatterItem),    // 协程ID
        XX(l, LineFormatterItem),       // 行号
        XX(d, DataTimeFormatterItem),   // 时间（可带格式参数）
        XX(n, NewLineFormatterItem),    // 换行符
        XX(f, FilenameFormatterItem),   // 文件名
        XX(T, TabFormatterItem)         // 制表符
#undef XX
    };

    // 构建最终的格式化项列表
    for(auto& i : vec) {
        if(std::get<2>(i) == 0) {
            // 普通字符串项
            m_items.push_back(std::make_shared<StringFormatterItem>(std::get<0>(i)));
        } else {
            // 查找格式项映射
            auto it = s_format_items.find(std::get<0>(i));
            if(it == s_format_items.end()) {
                // 无效格式项处理
                m_items.push_back(std::make_shared<StringFormatterItem>(
                    "<<error_format %" + std::get<0>(i) + ">>"));
                error = true;
            } else {
                // 创建对应的格式化项（如时间格式化项）
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }
    }

    // 设置最终错误状态
    if(error) {
        m_error = true;
    }
}

LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
    :m_logger(logger), m_event(event){

}

LogEventWrap::~LogEventWrap(){
    m_logger->log(m_event);
}

LoggerManager::LoggerManager(){
    // 在这里创建的，都是默认static logger，并不是配置系统里的，所以我们不配置 BufferParams
    // 先 默认 走 同步输出日志
    std::unique_ptr<LoggerBuilder> builder(new LoggerBuilder("root", LogLevel::Level::UNKNOW));
    builder->BuildLogAppender<StdoutLogAppender>(
        LogLevel::UNKNOW, 
        std::make_shared<LogFormatter>()
    );
    // 5. 显式构建并注册日志器
    m_root = builder->Build();
    m_loggers.emplace("root", m_root);  // 使用emplace替代insert
}

/**
 * 如果指定名称的日志器未找到，那么就新创建一个，新创建的 Logger 配置默认的 Appender和默认的 LogFormatter 
 */
Logger::ptr LoggerManager::getLogger(const std::string& name){
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    if(it != m_loggers.end()){
        return it->second;
    }
    // 如果没有创建
    std::unique_ptr<LoggerBuilder> builder(new LoggerBuilder(name));
    // 默认std输出
    builder->BuildLogAppender<StdoutLogAppender>(
        LogLevel::UNKNOW, 
        std::make_shared<LogFormatter>()
    );
    auto log = builder->Build();
    m_loggers.emplace(name, log);
    return log;
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

}// namespace sylar
