#ifndef __SYLAR__LOG_H_
#define __SYLAR__LOG_H_

#include <string>
#include <stdint.h>
#include <memory>
#include <list>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdio.h>
#include <map>
#include "singleton.h"
#include "mutex.h"

#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(logger, sylar::LogEvent::ptr(new sylar::LogEvent(__FILE__, __LINE__, 0 , sylar::GetThreadId(), sylar::GetFiberId(), time(0), level))).getSS() 


#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)
#define SYLAR_LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)
#define SYLAR_LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)
#define SYLAR_LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)

// 提供另一种不同个 << 传入，而是类似 printf 的。
#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(logger, sylar::LogEvent::ptr(new sylar::LogEvent(__FILE__, __LINE__, 0, \
                                                                            sylar::GetThreadId(), sylar::GetFiberId(), \
                                                                            time(0), level))).getEvent()->format(fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_DEBUG(logger, format, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::DEBUG, format,  __VA_ARGS__)
#define SYLAR_LOG_FMT_INFO(logger, format, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::INFO, format, __VA_ARGS__)
#define SYLAR_LOG_FMT_WARN(logger, format, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::WARN, format, __VA_ARGS__)
#define SYLAR_LOG_FMT_ERROR(logger, format, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::ERROR, format, __VA_ARGS__)
#define SYLAR_LOG_FMT_FATAL(logger, format, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::FATAL, format, __VA_ARGS__)                                                                      

#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()
#define SYLAR_LOG_NAME(name) sylar::LoggerMgr::GetInstance()->getLogger(name)

#define SYLAR_Log_YAML2String() sylar::LoggerMgr::GetInstance()->toYamlString()

namespace sylar{

class Logger;

//日志的级别
class LogLevel{
    public:
        enum Level{
            UNKNOW = 0,
            DEBUG = 1,
            INFO = 2,
            WARN = 3,
            ERROR = 4,
            FATAL = 5,
            NOTEST = 6,    
        };

        static const char* ToString(LogLevel::Level level);     // 枚举转字符表示
        static LogLevel::Level FromString(const std::string& str);    // 字符串转枚举表达
};

//日志事件
class LogEvent{
    public:
        typedef std::shared_ptr<LogEvent> ptr;      //使用ptr，就避免了对象的复制和拷贝
        LogEvent(const char* file, int32_t line, uint32_t elapse, uint32_t threadId, uint32_t fiberId , uint64_t time, LogLevel::Level level);
         
        const char* getFile() const {return m_file;}
        int32_t getLine() const {return m_line;}
        uint32_t getElapse() const {return m_elapse;}
        uint32_t getThreadId() const {return m_threadId;}
        uint32_t getFiberId() const {return m_fiberId;}
        uint64_t getTime() const {return m_time;}
        std::string getContent() const {return m_ss.str();}
        LogLevel::Level getLevel() const {return m_level;}

        std::stringstream& getSS() {return m_ss;}

        void format(const char* fmt, ...);
        void format(const char* fmt, va_list al);

    private:
        const char* m_file = nullptr;   //文件名
        int32_t m_line = 0;             //行号
        uint32_t m_elapse = 0;          //程序启动到现在的毫秒数
        uint32_t m_threadId = 0;        //线程
        uint32_t m_fiberId = 0;         //协程
        uint64_t m_time = 0;            //时间戳
        std::stringstream m_ss;
        LogLevel::Level m_level;               //日志级别
};



//日志格式器
class LogFormatter{
    public:
        typedef std::shared_ptr<LogFormatter> ptr;
        /**
         * @brief 构造函数
         * @param[in] pattern 格式模板，参考sylar与log4cpp
         * @details 模板参数说明：
         * - %%m 消息
         * - %%p 日志级别
         * - %%c 日志器名称
         * - %%d 日期时间，后面可跟一对括号指定时间格式，比如%%d{%%Y-%%m-%%d %%H:%%M:%%S}，这里的格式字符与C语言strftime一致
         * - %%r 该日志器创建后的累计运行毫秒数
         * - %%f 文件名
         * - %%l 行号
         * - %%t 线程id
         * - %%F 协程id
         * - %%N 线程名称
         * - %%% 百分号
         * - %%T 制表符
         * - %%n 换行
         * 
         * 默认格式：%%d{%%Y-%%m-%%d %%H:%%M:%%S}%%T%%t%%T%%N%%T%%F%%T[%%p]%%T[%%c]%%T%%f:%%l%%T%%m%%n
         * 
         * 默认格式描述：年-月-日 时:分:秒 [累计运行毫秒数] \\t 线程id \\t 线程名称 \\t 协程id \\t [日志级别] \\t [日志器名称] \\t 文件名:行号 \\t 日志消息 换行符
         */
        LogFormatter(const std::string& pattern = "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T[%m]%n");
        std::string format(std::shared_ptr<Logger> logger, LogEvent::ptr event);
        void init();                                // 解析模板字符串
        bool isError() const {return m_error;}
        std::string getPattern(){return m_pattern;}
    public:
        class FormatterItem{
            public:
                typedef std::shared_ptr<FormatterItem> ptr;
                virtual ~FormatterItem(){}
                virtual void format(std::ostream& os, std::shared_ptr<Logger> logger ,LogEvent::ptr event) = 0;
        };

    private:
        std::string m_pattern;                      // 模板字符串 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
        std::vector<FormatterItem::ptr> m_items;    // 存储每一个日志类型的输出子类
        bool m_error = false;
};

//日志输出器
class LogAppender{
    public:
        typedef std::shared_ptr<LogAppender> ptr;
        typedef SpinkLock MutexType;

        LogAppender(LogLevel::Level level, LogFormatter::ptr formatter);
        virtual ~LogAppender(){}
        //纯虚函数
        virtual void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) = 0;
        virtual std::string toYamlString() = 0;
        void setLevel(LogLevel::Level level){m_level = level;}
        void setFormatter(LogFormatter::ptr val);
        LogFormatter::ptr getFormatter();
    
    protected:
        LogLevel::Level m_level;
        LogFormatter::ptr m_formatter;      //通过这个类，对日志进行格式化
        MutexType m_mutex;
};

//输出到控制台
class StdoutLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    StdoutLogAppender(LogLevel::Level level, LogFormatter::ptr formatter);
    std::string toYamlString();
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
private:
};

//输出到文件
class FileLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename, LogLevel::Level level, LogFormatter::ptr formatter);
    std::string toYamlString();
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
    
    // 重新打开文件
    // 如果 当前的日志事件 比 上一个日志事件 超过 interval 秒，就重新打开一次日志文件。把缓存写入。也避免线程间频繁开关 文件。
    // 当 interval = 0时，直接重新打开文件。
    bool reopen(uint64_t now, uint64_t interval = 3);
private:
    std::string m_filename;
    std::ofstream m_filestream;     //#include <fstream>
    bool m_reopenError; 
    uint64_t m_lastTime;              // 文件最近一次打开的事件 事件
};


//日志器
//继承 public std::enable_shared_from_this<Logger> 可以在成员函数获得自己的shard_ptr
class Logger : public std::enable_shared_from_this<Logger>{
    public:
        typedef std::shared_ptr<Logger> ptr;
        typedef SpinkLock MutexType;

        Logger(const std::string name = "root");
        void log(LogEvent::ptr event);

        void setLevel(LogLevel::Level level) {m_level = level;}
        LogLevel::Level getLevel(){return m_level;}
        const std::string& getName() const {return m_name;}

        void addAppender(LogAppender::ptr appender);
        void delAppender(LogAppender::ptr appender);
        void clearAppender();

        std::string toYamlString();
    private:
        std::string m_name;                         //日志名称
        LogLevel::Level m_level;                    //日志级别，当事件的日志级别大于等于该日志器的级别时，才输出
        std::list<LogAppender::ptr> m_appenders;    //Appender集合
        MutexType m_mutex;
};

// 使用LogEventWarp管理 event和logger，在析构的时候，调用logger的方法 流式输出 event
class LogEventWrap{
    public:
        LogEventWrap(Logger::ptr logger, LogEvent::ptr event);
        ~LogEventWrap();
        std::stringstream& getSS() {return m_event->getSS();}
        LogEvent::ptr getEvent() {return m_event;}
    private:
        Logger::ptr m_logger; 
        LogEvent::ptr m_event;
};

class LoggerManager{
    public:
        typedef SpinkLock MutexType;
        LoggerManager();
        Logger::ptr getLogger(const std::string& name);
        Logger::ptr getRoot(){return m_root;}
        void init();
        std::string toYamlString(); // 将实例序列化
    private:
        MutexType m_mutex;
        std::map<std::string, Logger::ptr> m_loggers;
        Logger::ptr m_root;
};

typedef sylar::Singleton<LoggerManager> LoggerMgr;

}// namespace sylar

#endif