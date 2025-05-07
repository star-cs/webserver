#include <iostream>
#include <yaml-cpp/yaml.h>

#include "sylar/sylar.h"
#if 0
void test(){
    sylar::Logger::ptr logger(new sylar::Logger);

    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T[%m]%n"));

    sylar::StdoutLogAppender::ptr std_appender(new sylar::StdoutLogAppender());
    std_appender->setFormatter(fmt);
    std_appender->setLevel(sylar::LogLevel::INFO);
    logger->addAppender(std_appender);

    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("./log.txt"));
    file_appender->setFormatter(fmt);
    file_appender->setLevel(sylar::LogLevel::ERROR);
    logger->addAppender(file_appender);

    SYLAR_LOG_DEBUG(logger) << "debug logger";
    SYLAR_LOG_INFO(logger) << "info logger";
    SYLAR_LOG_WARN(logger) << "warn logger";
    SYLAR_LOG_ERROR(logger) << "error logger";
    SYLAR_LOG_FATAL(logger) << "fatal logger"; 

    SYLAR_LOG_FMT_WARN(logger, "error %d %s" , 100, "哈哈哈");

    auto l = sylar::LoggerMgr::GetInstance()->getLogger("xxx");
    //这个时候，l的logger里的appender使用默认的，因为没有创建 xxx 的logger
    SYLAR_LOG_INFO(l) << "xxx";
}
#endif

static int count = 0;

void test(){
    auto logger = SYLAR_LOG_ROOT();
   
    for(int i = 0; i < 100; i++){
        sylar::WorkerMgr::GetInstance()->schedule("net", [logger](){
            SYLAR_LOG_INFO(logger) << "Log to file1: " << count++;
            SYLAR_LOG_ERROR(logger) << "Log to file1: " << count++;
            sleep(1);
        });
    }
}


int main(int argc, char** argv){
    std::cout << "main start" << std::endl;
    
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    // 在这之前，当前的线程名还都是 UNKNOW
    sylar::IOManager iom(2, true, "main");
    iom.schedule(&test);
    std::cout << "main end" << std::endl;
    return 0;   
}

/**
 * 初始化安全策略
 * 根据分析，以下是安全的初始化策略：
 * 1. 程序启动时：
 * - 首先只使用同步日志模式，不配置双缓冲区
 * - 这样可以安全地记录启动过程中的日志，不需要线程池支持
 * 
 * 2. 线程池初始化后：
 * - 初始化WorkerMgr
 * - 然后再加载配置，创建带双缓冲区的日志器
 * 
 * 3. 日志配置加载：
 * - 配置包含BufferParams的日志器时，必须确保已有可用的IOManager
 */