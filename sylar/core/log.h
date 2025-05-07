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
#include <cstring>
#include <sys/eventfd.h>
#include <iostream>

#include "sylar/common/macro.h"
#include "sylar/common/singleton.h"
#include "mutex.h"
#include "thread.h"
#include "buffermanager.h"

#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(logger, sylar::LogEvent::ptr( \
            new sylar::LogEvent(std::string(__FILE__), __LINE__, 0 , \
                            sylar::GetThreadId(), sylar::Thread::GetName(), \
                            sylar::GetFiberId(), time(0), level))).getSS() 


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

#pragma pack(push, 1)
struct LogMeta {
    uint64_t timestamp;    // 时间戳
    uint32_t threadId;     // 线程ID
    uint32_t fiberId;      // 协程ID
    int32_t line;          // 行号
    uint32_t elapse;
    LogLevel::Level level; // 日志级别
    uint16_t fileLen;      // 文件名长度
    uint32_t threadNameLen;// 线程名长度
    uint32_t msgLen;       // 消息内容长度
};
#pragma pack(pop)

//日志事件
class LogEvent{
    public:
        typedef std::shared_ptr<LogEvent> ptr;      //使用ptr，就避免了对象的复制和拷贝
        LogEvent(std::string file, int32_t line, uint32_t elapse, uint32_t threadId, const std::string& threadName, uint32_t fiberId , uint64_t time, LogLevel::Level level);
         
        const std::string& getFile() const {return m_file;}
        int32_t getLine() const {return m_line;}
        uint32_t getElapse() const {return m_elapse;}
        uint32_t getThreadId() const {return m_threadId;}
        std::string getThreadName() const {return m_threadName;}
        uint32_t getFiberId() const {return m_fiberId;}
        uint64_t getTime() const {return m_time;}
        std::string getContent() const {return m_ss.str();}
        LogLevel::Level getLevel() const {return m_level;}

        std::stringstream& getSS() {return m_ss;}

        void format(const char* fmt, ...);
        void format(const char* fmt, va_list al);

        Buffer::ptr serialize() const {
            LogMeta meta{
                .timestamp = m_time,
                .threadId = m_threadId,
                .fiberId = m_fiberId,
                .line = m_line,
                .elapse = m_elapse,
                .level = m_level,
                .fileLen = static_cast<uint16_t>(m_file.size()),
                .threadNameLen = static_cast<uint32_t>(m_threadName.size()),
                .msgLen = static_cast<uint32_t>(m_ss.str().size())
            };

            const size_t total_need = sizeof(LogMeta) + meta.fileLen + meta.threadNameLen + meta.msgLen;
            auto buffer = std::make_shared<Buffer>(total_need); // 使用 shared_ptr 管理内存

            // 序列化元数据
            buffer->push(reinterpret_cast<const char*>(&meta), sizeof(meta));

            // 序列化变长数据（包含终止符）
            buffer->push(m_file.c_str(), meta.fileLen);
            buffer->push(m_threadName.c_str(), meta.threadNameLen);
            buffer->push(m_ss.str().c_str(), meta.msgLen);

            return buffer; // 返回 shared_ptr
        }

        // 每次调用，解析一个LogEvent
        static LogEvent::ptr deserialize(Buffer& buffer) {
            if(buffer.readableSize() < sizeof(LogMeta)) {
                return nullptr;
            }
            LogMeta meta;
            memcpy(&meta, buffer.Begin(), sizeof(LogMeta));

            const size_t total_need = sizeof(LogMeta) + meta.fileLen + meta.threadNameLen + meta.msgLen;
            if(buffer.readableSize() < total_need){
                return nullptr;
            }

            // 4. 提取各字段数据（使用临时指针操作）
            const char* data_ptr = buffer.Begin() + sizeof(LogMeta);
            
            // 文件名处理
            std::string file(data_ptr, meta.fileLen);
            data_ptr += meta.fileLen;

            // 线程名
            std::string thread_name(data_ptr, meta.threadNameLen);
            data_ptr += meta.threadNameLen;

            // 消息内容处理
            std::string message(data_ptr, meta.msgLen);

            // 5. 统一移动读指针（原子操作保证数据一致性）
            buffer.moveReadPos(total_need);

            // 6. 构建日志事件对象
            auto event = std::make_shared<LogEvent>(
                std::move(file),
                meta.line,
                meta.elapse,
                meta.threadId,
                std::move(thread_name),
                meta.fiberId,
                meta.timestamp,
                meta.level
            );

            event->getSS() << message;

            return event;
        }

    private:
        std::string m_file;   //文件名
        int32_t m_line = 0;             //行号
        uint32_t m_elapse = 0;          //程序启动到现在的毫秒数
        uint32_t m_threadId = 0;        //线程
        std::string m_threadName;       //线程名
        uint32_t m_fiberId = 0;         //协程
        uint64_t m_time = 0;            //时间戳
        std::stringstream m_ss;
        LogLevel::Level m_level;        //日志级别
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
        LogFormatter(const std::string& pattern = "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T[%m]%n");
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
        typedef Spinlock MutexType;

        LogAppender(LogLevel::Level level, LogFormatter::ptr formatter);
        
        
        template <typename LogLogAppenderType, typename... Args>
        static std::shared_ptr<LogLogAppenderType> CreateAppender(Args && ... args){
            return std::make_shared<LogLogAppenderType>(std::forward<Args>(args)...);
        }

        virtual ~LogAppender(){}
        //纯虚函数
        virtual void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) = 0;
        //适用于 双缓冲区，批量输入
        virtual void log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events) = 0;     

        virtual std::string toYamlString() = 0;
        void setLevel(LogLevel::Level level){m_level = level;}
        void setFormatter(LogFormatter::ptr val);
        LogFormatter::ptr getFormatter();
    
    protected:
        LogLevel::Level m_level;
        LogFormatter::ptr m_formatter;      //通过这个类，对日志进行格式化
        MutexType m_mutex;
};

class AppenderType{
public:
    enum Type{
        UNKNOW,
        StdoutLogAppender,          // std输出
        FileLogAppender,            // 不断输入到单一文件，不考虑文件大小
        RotatingFileLogAppender,    // 当文件大小达到阈值，创建新文件
    };

    static const char* ToString(AppenderType::Type type);     // 枚举转字符表示
    static AppenderType::Type FromString(const std::string& str);    // 字符串转枚举表达

};

//输出到控制台
class StdoutLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    StdoutLogAppender(LogLevel::Level level, LogFormatter::ptr formatter);
    std::string toYamlString();
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
    void log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events) override;     
private:
    Buffer m_buffer;
};

class FlushRule{
public:
    enum Rule{
        UNKNOW,
        FFLUSH,             // 普通日志场景，允许少量日志丢失但要求高性能
        FSYNC               // 适用于财务交易、关键操作日志等不能容忍数据丢失的场景
    };
    static const char* ToString(FlushRule::Rule rule);     // 枚举转字符表示
    static FlushRule::Rule FromString(const std::string& str);    // 字符串转枚举表达
};


class FileLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename, 
        LogLevel::Level level, 
        LogFormatter::ptr formatter,
        FlushRule::Rule flush_rule = FlushRule::Rule::FFLUSH    // 默认是普通日志
    );
    ~FileLogAppender(){
        if(m_fs){
            fclose(m_fs);
            m_fs = NULL;
        }
    }
    std::string toYamlString();
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
    void log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events) override;     

private:
    std::string m_filename;
    FILE* m_fs = NULL;
    FlushRule::Rule m_flushRule;
};

class RotatingFileLogAppender : public LogAppender{
public:
    typedef std::shared_ptr<RotatingFileLogAppender> ptr;
    RotatingFileLogAppender(const std::string& filename, 
        LogLevel::Level level, 
        LogFormatter::ptr formatter,
        size_t max_size,
        size_t max_file = 0,  // 默认是无限增加
        FlushRule::Rule flush_rule = FlushRule::Rule::FFLUSH  // 默认是普通日志 
    );
    ~RotatingFileLogAppender(){
        if(m_curFile){
            fclose(m_curFile);
            m_curFile = NULL;
        }
    }
    std::string toYamlString();
    void log(std::shared_ptr<Logger> logger, LogEvent::ptr event) override;
    void log(std::shared_ptr<Logger> logger, std::vector<LogEvent::ptr> events) override;     

private:
    void initLogFile(size_t len = 0);
    /**
     * 判断是否写的下，如果写的下就 ss<<str，缓存
     * 如果写不写了，就把 ss 缓存一次性写入。重置ss 
     */
    bool checkLogFile(const std::string& str);
    std::string createFilename();
private:
    std::string m_filename;
    FILE* m_curFile = NULL;
    std::vector<std::string> m_fileNames;
    size_t m_maxSize;
    size_t m_maxFile;
    FlushRule::Rule m_flushRule;
    size_t m_curFilePos = 0;
    size_t m_curFileIndex = 0;
    Buffer m_buffer; 
};


//日志器
//继承 public std::enable_shared_from_this<Logger> 可以在成员函数获得自己的shard_ptr、
//1. LoggerBuild.Build()之后
//2. 配置ConfigIOM
class Logger : public std::enable_shared_from_this<Logger>{
    public:
        typedef std::shared_ptr<Logger> ptr;
        typedef Spinlock MutexType;

        Logger(
            const std::string name, 
            LogLevel::Level level,
            std::vector<LogAppender::ptr>& appenders, 
            const BufferParams& bufParams
        )   :m_name(name), 
            m_level(level), 
            m_appenders(appenders.begin(), appenders.end())
        {
            if(bufParams.isValid()){
                m_bufMgr = std::make_shared<BufferManager>(
                    std::bind(&Logger::realLog, this, std::placeholders::_1), bufParams);
            }else{
                m_bufMgr = nullptr;
            }
        }

        // 由 iom_log 写入真正的文件。
        void realLog(Buffer::ptr buffer) {
            MutexType::Lock lock(m_log_mutex);      // 强制 只能 一个线程写入。

            if (!buffer) {
                std::cerr << "realLog: invalid buffer pointer" << std::endl;
                return;
            }
            
            std::vector<LogEvent::ptr> events;
            while (true) {  // 解析 buffer
                LogEvent::ptr event = LogEvent::deserialize(*buffer);
                // 理论上 buffer 里是多个Event的数据，不存在处理失败。
                
                if (event) {
                    events.push_back(event);
                } else {
                    if (buffer->readableSize() == 0) { // 读完了
                        break;
                    } else {
                        // 处理失败但数据未读完（说明发生严重错误）
                        std::cout << "Log deserialization error, remaining data: " << buffer->readableSize() << std::endl;
                        break;
                    }
                }
            }
            auto self = shared_from_this();
            for (auto& appender : m_appenders) {
                appender->log(self, events);
            }
        }
 
        // 写入缓冲区
        // 多个线程的 写日志，写入缓存区
        void log(LogEvent::ptr event){
            if(event->getLevel() >= m_level){
                if(m_bufMgr != nullptr){
                    // MutexType::Lock lock(m_mutex);   当协程阻塞，这个锁就一直没释放。搞半天，给我整的怀疑人生了。
                    Buffer::ptr buf = event->serialize();
                    m_bufMgr->push(buf);
                }else{
                    // 如果没有配置iom，直接同步输出日志
                    auto self = shared_from_this();
                    for(auto& appender : m_appenders) {
                        appender->log(self, event);
                    }
                }
            }
        }

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
        std::vector<LogAppender::ptr> m_appenders;  //Appender集合
        MutexType m_mutex;
        MutexType m_log_mutex;
        BufferManager::ptr m_bufMgr;                // 双缓冲区（所有logger公用一个BufferManager双缓冲区）
};


class LoggerBuilder{
    public:
        using ptr = std::shared_ptr<LoggerBuilder>;
        // 默认构造root，UNKNOW
        LoggerBuilder(const std::string &name = "root", 
                    LogLevel::Level level = LogLevel::Level::UNKNOW) 
            : m_logger_name(name),  m_level(level)
        {}
 
        void setLoggerName(const std::string& name = "root") { m_logger_name = name;}
        void setLoggerLevel(LogLevel::Level level) { m_level = level;}
        void setBufferParams(const BufferParams& params) {m_bufParams = params;}

        template<typename LogAppenderType, typename... Args>
        void BuildLogAppender(Args&& ... args){
            m_appenders.emplace_back(
                LogAppender::CreateAppender<LogAppenderType>(std::forward<Args>(args)...)
            );
        }

        Logger::ptr Build(){
            EnsureDefaults();
            return std::make_shared<Logger>(
                m_logger_name, 
                m_level,
                m_appenders,
                m_bufParams
            );
        }

    private:
        void EnsureDefaults() {
            if (m_appenders.empty()) {
                BuildLogAppender<StdoutLogAppender>(
                    LogLevel::UNKNOW, 
                    std::make_shared<LogFormatter>()
                );
            }
        }

    private:
        std::string m_logger_name;// 日志器名称
        LogLevel::Level m_level;
        std::vector<LogAppender::ptr> m_appenders; // 写日志方式
        BufferParams m_bufParams;
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
        typedef Spinlock MutexType;
        LoggerManager();

        
        void addLogger(const Logger::ptr & logger);
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