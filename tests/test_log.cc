#include <iostream>
#include <yaml-cpp/yaml.h>

#include "../sylar/buffermanager.h"
#include "../sylar/log.h"
#include "../sylar/util.h"
#include "../sylar/iomanager.h"
#include "../sylar/config.h"
#include "../sylar/worker.h"
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

void test(){
    auto network_iom = sylar::WorkerMgr::GetInstance()->get("net").get();

    auto logger1 = SYLAR_LOG_NAME("root");
    auto logger2 = SYLAR_LOG_NAME("system");
    
    int count = 0;
    for(int i = 0; i < 10000; i++){
        network_iom->schedule([logger1, logger2, &count](){
            SYLAR_LOG_INFO(logger1) << "Log to file1: " << count++;
            SYLAR_LOG_ERROR(logger2) << "Log to file2: " << count++;
        });
    }
    sylar::WorkerMgr::GetInstance()->stop();
}


int main(){
    std::cout << "main start" << std::endl;
    
    // 问题：代码里存在 static g_logger这种，它会默认创建BufferManager，但是这个时候线程池还没创建完。
    YAML::Node workNode = YAML::LoadFile("/home/yang/projects/webserver/bin/conf/works.yml");
    sylar::Config::LoadFromYaml(workNode);

    sylar::WorkerMgr::GetInstance()->init();    // 先初始化 所有调度器

    YAML::Node logNode = YAML::LoadFile("/home/yang/projects/webserver/bin/conf/log.yml");
    sylar::Config::LoadFromYaml(logNode);       // 再导入日志

    // 在这之前，当前的线程名还都是 UNKNOW
    sylar::IOManager::ptr m_mainIOmanager;
    m_mainIOmanager.reset(new sylar::IOManager(1, true, "main"));
    m_mainIOmanager->schedule(&test);

    m_mainIOmanager->stop();    // 这个就会一直等待任务结束，任务里的
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