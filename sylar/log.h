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

#define SYLAR_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

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
            FATAL = 5
        };

        static const char* ToString(LogLevel::Level level);     // 枚举转字符表示
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
        LogFormatter(const std::string& pattern);
        std::string format(std::shared_ptr<Logger> logger, LogEvent::ptr event);

    public:
        class FormatterItem{
            public:
                typedef std::shared_ptr<FormatterItem> ptr;
                virtual ~FormatterItem(){}
                virtual void format(std::ostream& os, std::shared_ptr<Logger> logger ,LogEvent::ptr event) = 0;
        };

        void init();                                // 解析模板字符串
    private:
        std::string m_pattern;                      // 模板字符串 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
        std::vector<FormatterItem::ptr> m_items;    // 存储每一个日志类型的输出子类
};

//日志输出器
class LogAppender{
    public:
        typedef std::shared_ptr<LogAppender> ptr;
        LogAppender(){}
        virtual ~LogAppender(){}
        //纯虚函数
        virtual void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) = 0;
        void setLevel(LogLevel::Level level){m_level = level;}
        void setFormatter(LogFormatter::ptr val){m_formatter = val;}
        LogFormatter::ptr getFormatter(){return m_formatter;}
    
    protected:
        LogLevel::Level m_level;
        LogFormatter::ptr m_formatter;      //通过这个类，对日志进行格式化
};

//输出到控制台
class StdoutLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
private:
};

//输出到文件
class FileLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename);
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
    
    //重新打开文件
    bool reopen();
private:
    std::string m_filename;
    std::ofstream m_filestream;     //#include <fstream>
};


//日志器
//继承 public std::enable_shared_from_this<Logger> 可以在成员函数获得自己的shard_ptr
class Logger : public std::enable_shared_from_this<Logger>{
    public:
        typedef std::shared_ptr<Logger> ptr;

        Logger(const std::string name = "root");
        void log(LogEvent::ptr event);

        void setLevel(LogLevel::Level level) {m_level = level;}
        LogLevel::Level getLevel(){return m_level;}
        const std::string& getName() const {return m_name;}

        void addAppender(LogAppender::ptr appender);
        void delAppender(LogAppender::ptr appender);

    private:
        std::string m_name;                         //日志名称
        LogLevel::Level m_level;                    //日志级别，当事件的日志级别大于等于该日志器的级别时，才输出
        std::list<LogAppender::ptr> m_appenders;    //Appender集合
        LogFormatter::ptr m_formatter;              //最基础的formatter，防止添加appenders的时候没有设置对应的fmt。
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
        LoggerManager();
        Logger::ptr getLogger(const std::string& name);
        Logger::ptr getRoot(){return m_root;}
        void init();
    private:
        std::map<std::string, Logger::ptr> m_loggers;
        Logger::ptr m_root;
};

typedef sylar::Singleton<LoggerManager> LoggerMgr;

}// namespace sylar

#endif