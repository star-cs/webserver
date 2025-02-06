#include <iostream>
#include "../sylar/log.h"
#include "../sylar/util.h"

int main(int argc, char** argv){
    sylar::Logger::ptr logger(new sylar::Logger);
    
    logger->addAppender(sylar::LogAppender::ptr(new sylar::StdoutLogAppender));     // stdout Appender会使用默认的格式
    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("./log.txt"));

    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));

    file_appender->setFormatter(fmt);
    file_appender->setLevel(sylar::LogLevel::ERROR);
    
    logger->addAppender(file_appender);

    sylar::LogEvent::ptr event(new sylar::LogEvent(__FILE__, __LINE__, 0 , sylar::GetThreadId(), sylar::GetFiberId(), time(0)));
    event->getSS() << "你好";
    logger->log(sylar::LogLevel::DEBUG , event);        // 命令行输出
    logger->log(sylar::LogLevel::ERROR , event);        // 命令行 + 文本输出

    return 0;
}
