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

LogEvent::LogEvent(std::string file, int32_t line, uint32_t elapse, uint32_t threadId, const std::string& threadName, uint32_t fiberId , uint64_t time , LogLevel::Level level)
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

const char* AppenderType::ToString(AppenderType::Type type){
    switch(type){
#define XX(name) \
    case AppenderType::Type::name: \
        return #name; \
        break; 

    XX(StdoutLogAppender);
    XX(FileLogAppender);
    XX(RotatingFileLogAppender);
#undef XX
    default:
        std::cout << "invalid appender type" << std::endl;
        return "UNKNOW";
    }
    return "UNKNOW";
}
AppenderType::Type AppenderType::FromString(const std::string& str){
#define XX(name, val) \
    if(str == #val) { \
        return AppenderType::Type::name; \
    }
    XX(StdoutLogAppender, StdoutLogAppender);
    XX(FileLogAppender, FileLogAppender);
    XX(RotatingFileLogAppender, RotatingFileLogAppender);
#undef XX
    return AppenderType::Type::UNKNOW;
}

const char* FlushRule::ToString(FlushRule::Rule rule){
    switch(rule){
#define XX(name) \
    case FlushRule::Rule::name: \
        return #name; \
        break; 

    XX(FFLUSH);
    XX(FSYNC);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

FlushRule::Rule FlushRule::FromString(const std::string& str){
#define XX(name, val) \
    if(str == #val) { \
        return FlushRule::Rule::name; \
    }
    XX(FFLUSH, FFLUSH);
    XX(FSYNC, FSYNC);

    XX(FFLUSH, fflush);
    XX(FSYNC, fsync);
#undef XX
    std::cout << "Invalid FlushRule::Rule string: " << str << std::endl;
    return FlushRule::Rule::UNKNOW;
}


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
    : LogAppender(level, formatter), m_buffer(1024, 10240, 1024){
    // 默认m_buffer，[ 1kb, 10kb, 1kb ]
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

void StdoutLogAppender::log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events){
    for(auto& event : events){
        if(event->getLevel() >= m_level){
            m_buffer.push(m_formatter->format(logger, event));
        }
    }
    {
        MutexType::Lock lock(m_mutex);
        if(m_buffer.readableSize() > 0){
            std::cout.write(m_buffer.Begin(), m_buffer.readableSize());
            m_buffer.Reset();
        }
    }
}

// FileLogAppender
FileLogAppender::FileLogAppender(const std::string& filename, 
    LogLevel::Level level, 
    LogFormatter::ptr formatter,
    FlushRule::Rule flush_rule
)
    : LogAppender(level, formatter), m_filename(filename), m_flushRule(flush_rule)
{ 
    assert(m_flushRule != FlushRule::UNKNOW);
    FSUtil::Mkdir(FSUtil::Dirname(m_filename));
    m_fs = fopen(filename.c_str(), "ab");
    if(!m_fs) {
        std::cerr << "Failed to open log file: " << filename 
                 << " Error: " << strerror(errno) << std::endl;
    }
}

std::string FileLogAppender::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "FileLogAppender";
    node["pattern"] = m_formatter->getPattern();
    node["file"] = m_filename;
    node["level"] = YAML::Load(LogLevel::ToString(m_level));
    node["flush_rule"] = YAML::Load(FlushRule::ToString(m_flushRule));
    std::stringstream ss;
    ss << node;
    return ss.str();
}


void FileLogAppender::log(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    MutexType::Lock lock(m_mutex);
    if(event->getLevel() >= m_level && m_fs){
        std::string data = m_formatter->format(logger , event);
        fwrite(data.c_str(), 1, data.size(), m_fs);
        if(ferror(m_fs)){
            std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
            perror(NULL);
        }
        if(m_flushRule == FlushRule::Rule::FFLUSH){
            if(fflush(m_fs)==EOF){
                std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                perror(NULL);
            }
        }else if(m_flushRule == FlushRule::Rule::FSYNC){
            fflush(m_fs);
            fsync(fileno(m_fs));
        }
    }
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events){
    MutexType::Lock lock(m_mutex);
    std::ostringstream oss;
    for(auto& event : events){
        if(event->getLevel() >= m_level && m_fs){
            oss << m_formatter->format(logger, event);
        }
    }
    const std::string& str = oss.str();
    if(m_fs){
        fwrite(str.c_str(), 1, str.size(), m_fs);
        if(ferror(m_fs)){
            std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
            perror(NULL);
        }
        if(m_flushRule == FlushRule::Rule::FFLUSH){
            if(fflush(m_fs)==EOF){
                std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                perror(NULL);
            }
        }else if(m_flushRule == FlushRule::Rule::FSYNC){
            fflush(m_fs);
            fsync(fileno(m_fs));
        }
    }
}


// RotatingFileLogAppender
// 给的filename应该是一个文件夹
RotatingFileLogAppender::RotatingFileLogAppender(
    const std::string& filename, 
    LogLevel::Level level, 
    LogFormatter::ptr formatter,
    size_t max_size,
    size_t max_file,
    FlushRule::Rule flush_rule
)
    : LogAppender(level, formatter), 
    m_filename(filename), 
    m_maxSize(max_size),
    m_maxFile(max_file),
    m_flushRule(flush_rule),
    m_buffer(max_size)      // 预分配数据大小
{ 
    assert(m_flushRule != FlushRule::UNKNOW);
    assert(m_maxSize > 0);
    FSUtil::Mkdir(FSUtil::Dirname(m_filename));
    if(m_maxFile > 0){
        /**
         * vector：
         * reserve是容器预留空间，但在空间内不真正创建元素对象，所以在没有添加新的对象之前，不能引用容器内的元素。
         *          加入新的元素时，要调用push_back()/insert()函数。
         * 
         * resize是改变容器的大小，且在创建对象，因此，调用这个函数之后，就可以引用容器内的对象了，因此当加入新的元素时，用operator[]操作符，或者用迭代器来引用元素对象。、
         *      此时再调用push_back()函数，是加在这个新的空间后面的。
         */
        // m_fileNames.reserve(m_maxFile);
        m_fileNames.resize(m_maxFile);
    }
}

std::string RotatingFileLogAppender::toYamlString(){
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "RotatingFileLogAppender";
    node["pattern"] = m_formatter->getPattern();
    node["file"] = m_filename;
    node["level"] = YAML::Load(LogLevel::ToString(m_level));
    node["flush_rule"] = YAML::Load(FlushRule::ToString(m_flushRule));
    node["max_size"] = m_maxSize;
    node["max_file"] = m_maxFile;
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void RotatingFileLogAppender::initLogFile(size_t len){
    if(m_curFile == NULL || (m_curFilePos + len) > m_maxSize){
        // 写不下了，保证日志的完整性，直接新建文件。
        if(m_curFile != NULL){
            fflush(m_curFile);
            fclose(m_curFile);
            
            if(m_maxFile == 0){
                // 无限增加日志文件
                m_curFileIndex++;
            }else{
                m_curFileIndex = (m_curFileIndex + 1) % m_maxFile;

                if(!m_fileNames[m_curFileIndex].empty()){    // 说明 循环到 已有的文件了。
                    std::string newfilename = createFilename();
                    if(rename(m_fileNames[m_curFileIndex].c_str(), newfilename.c_str()) != 0){  // 文件 改新名字
                        perror("rename failed");
                    }   
                    m_fileNames[m_curFileIndex] = newfilename;
                    m_curFile = fopen(newfilename.c_str(), "r+b");
                    fseek(m_curFile, 0, SEEK_SET);   // 从头 覆盖，不考虑 日志文件名了。默认最后一个文件可能会存在过往的日志信息。
                    m_curFilePos = 0;
                    return;
                }
            }
        }
        std::string filename = createFilename();
        m_fileNames[m_curFileIndex] = filename;
        m_curFile = fopen(filename.c_str(), "ab");
        if(m_curFile==NULL){
            std::cout <<__FILE__<<__LINE__<<"open file failed"<< std::endl;
            perror(NULL);
        }
        m_curFilePos = 0;
        return;
    }
}

std::string RotatingFileLogAppender::createFilename() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm);
    return m_filename + "_" + time_buf + "_" + std::to_string(m_curFileIndex) + ".log";
}

void RotatingFileLogAppender::log(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    MutexType::Lock lock(m_mutex);
    if(event->getLevel() >= m_level){
        std::string data = m_formatter->format(logger , event);
        initLogFile(data.size());
        fwrite(data.c_str(), 1, data.size() , m_curFile);

        if(ferror(m_curFile)){
            std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
            perror(NULL);
        }
        m_curFilePos += data.size();
        if(m_flushRule == FlushRule::Rule::FFLUSH){
            if(fflush(m_curFile)==EOF){  // 刚好最后一个日志把文件写满了。
                std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                perror(NULL);
            }
        }else if(m_flushRule == FlushRule::Rule::FSYNC){
            fflush(m_curFile);
            fsync(fileno(m_curFile));
        }
    }
}

bool RotatingFileLogAppender::checkLogFile(const std::string& data){

    if(m_curFile == NULL || (m_curFilePos + data.size()) > m_maxSize){
        // 写不下了，保证日志的完整性，直接新建文件。
        if(m_curFile != NULL){
            // 把 ss 缓存一次性写入
            fwrite(m_buffer.Begin(), 1, m_buffer.readableSize(), m_curFile);
            m_buffer.Reset();

            // 判断错误信息
            if(ferror(m_curFile)){
                std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
                perror(NULL);
            }
            if(m_flushRule == FlushRule::Rule::FFLUSH){
                if(fflush(m_curFile)==EOF){  // 刚好最后一个日志把文件写满了。
                    std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                    perror(NULL);
                }
            }else if(m_flushRule == FlushRule::Rule::FSYNC){
                fflush(m_curFile);
                fsync(fileno(m_curFile));
            }
            
            fclose(m_curFile);
            
            if(m_maxFile == 0){
                // 无限增加日志文件
                m_curFileIndex++;
            }else{
                m_curFileIndex = (m_curFileIndex + 1) % m_maxFile;

                if(!m_fileNames[m_curFileIndex].empty()){    // 说明 循环到 已有的文件了。
                    std::string newfilename = createFilename();
                    if(rename(m_fileNames[m_curFileIndex].c_str(), newfilename.c_str()) != 0){  // 文件 改新名字
                        std::cout << "rename failed" << std::endl;
                        perror(NULL);
                    }
                    m_fileNames[m_curFileIndex] = newfilename;
                    m_curFile = fopen(newfilename.c_str(), "r+b");
                    if (m_curFile == NULL) {
                        std::cout << __FILE__ << __LINE__ << "open file failed" << std::endl;
                        perror(NULL);
                    }

                    // 从头 覆盖，不考虑 日志文件名了。默认最后一个文件可能会存在过往的日志信息。
                    fseek(m_curFile, 0, SEEK_SET); 
                    m_curFilePos = 0;

                    // 模拟写入文件，实际上写入缓存。
                    m_buffer.push(data);
                    m_curFilePos += data.size();

                    return true;
                }
            }
        }
        
        // m_curFile为空，创建新文件 
        std::string filename = createFilename();
        if(m_maxFile > 0){
            m_fileNames[m_curFileIndex] = filename;    // 只有限制最大文件数，记录文件名
        }
        m_curFile = fopen(filename.c_str(), "ab");
        if(m_curFile==NULL){
            std::cout <<__FILE__<<__LINE__<<"open file failed"<< std::endl;
            perror(NULL);
        }
        m_curFilePos = 0;
    }

    // 模拟写入文件，实际上写入缓存。
    m_buffer.push(data);
    m_curFilePos += data.size();
    return false;
}

void RotatingFileLogAppender::log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events){
    // 这个时候，m_curFilePos 转变成对 m_buffer 写入数据的 pos 长度。
    MutexType::Lock lock(m_mutex);
    for(auto& event : events){
        if(event->getLevel() >= m_level){
            std::string data = m_formatter->format(logger , event);
            checkLogFile(data);
        }
    }

    // 最后再次，把缓存里的写入
    if(m_buffer.readableSize() > 0 && m_curFile != NULL){
        fwrite(m_buffer.Begin(), 1, m_buffer.readableSize(), m_curFile);
        m_buffer.Reset();

        // 判断错误信息
        if(ferror(m_curFile)){
            std::cout <<__FILE__<<__LINE__<<"write log file failed"<< std::endl;
            perror(NULL);
        }
        if(m_flushRule == FlushRule::Rule::FFLUSH){
            if(fflush(m_curFile)==EOF){  // 刚好最后一个日志把文件写满了。
                std::cout <<__FILE__<<__LINE__<<"fflush file failed"<< std::endl;
                perror(NULL);
            }
        }else if(m_flushRule == FlushRule::Rule::FSYNC){
            fflush(m_curFile);
            fsync(fileno(m_curFile));
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
