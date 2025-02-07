#include "log.h"
#include <tuple>
#include <map>
#include <fstream>
#include <iostream>
#include <time.h>
#include <stdarg.h>

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

LogEvent::LogEvent(const char* file, int32_t line, uint32_t elapse, uint32_t threadId, uint32_t fiberId , uint64_t time , LogLevel::Level level)
    :m_file(file), m_line(line), m_elapse(elapse), m_threadId(threadId), m_fiberId(fiberId), m_time(time) , m_level(level){
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

Logger::Logger(const std::string name)
    : m_name(name)
    , m_level(LogLevel::UNKNOW){        // logger默认最低的日志权重，大于等于这个日志权重的事件都要被记录
    m_formatter.reset(new LogFormatter("%d%T[%p]%T%f:%l%T%m%n"));
   
}


void Logger::addAppender(LogAppender::ptr appender){
    if(!appender->getFormatter()){
        appender->setFormatter(m_formatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender){
    for(auto it = m_appenders.begin(); it != m_appenders.end(); ++it){
        if(*it == appender){
            m_appenders.erase(it);
            break;
        }
    }
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

void StdoutLogAppender::log(Logger::ptr logger, LogEvent::ptr event){
    if(event->getLevel() >= m_level){
        std::cout << m_formatter->format(logger, event);
    }
}

FileLogAppender::FileLogAppender(const std::string& filename)
    : m_filename(filename){ 
    // 相当于创建文件 或 清空文件
    m_filestream.open(m_filename);
}

bool FileLogAppender::reopen(){
    if(m_filestream){
        m_filestream.close();
    }
    //追加写入,在原来基础上加了ios::app 
    m_filestream.open(m_filename, std::ios::out | std::ios::app);
    return !!m_filestream;
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, LogEvent::ptr event){
    if(event->getLevel() >= m_level){
        int res = reopen();
        if(res){
            m_filestream << m_formatter->format(logger , event);
        }else{
            std::cout << "<<error filename"  + m_filename + ">>" << std::endl;
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
        }
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
            } else {
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }
        
        // std::cout << std::get<0>(i) << " - " <<  std::get<1>(i) << " - " << std::get<2>(i) << std::endl;
    }
}

LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr event)
    :m_logger(logger), m_event(event){

}

LogEventWrap::~LogEventWrap(){
    m_logger->log(m_event);
}

LoggerManager::LoggerManager(){
    m_root.reset(new Logger);
    // 默认是输出std，如果 m_loggers 为空，默认使用 m_root
    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T[%m]%n"));
    LogAppender::ptr appender(new StdoutLogAppender);
    appender->setFormatter(fmt);
    m_root->addAppender(appender);
}

Logger::ptr LoggerManager::getLogger(const std::string& name){
    auto it = m_loggers.find(name);
    return it == m_loggers.end() ? m_root : it->second;
}

void LoggerManager::init(){

}


};// namespace sylar
