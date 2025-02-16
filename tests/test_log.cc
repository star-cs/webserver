#include <iostream>
#include "../sylar/log.h"
#include "../sylar/util.h"

int main(int argc, char** argv){
    #if 0
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
    #endif

    return 0;   
}
