#include "sylar/hook.h"
#include "sylar/sylar.h"


static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_sleep(){
    SYLAR_LOG_INFO(g_logger) << "test sleep begin";
    sylar::IOManager iom;
    iom.schedule([]{
        SYLAR_LOG_INFO(g_logger) << "begin sleep 2";
        sleep(2);
        SYLAR_LOG_INFO(g_logger) << "end sleep 2";
    });

    iom.schedule([]{
        SYLAR_LOG_INFO(g_logger) << "begin sleep 3";
        sleep(3);
        SYLAR_LOG_INFO(g_logger) << "end sleep 3";
    });

    SYLAR_LOG_INFO(g_logger) << "test sleep end";
}

int main(){
    test_sleep();

    return 0;
}