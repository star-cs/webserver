#include "sylar/sylar.h"


sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();


void func(){
    SYLAR_LOG_DEBUG(g_logger) << "func start";
    sylar::Fiber::GetThis()->yield();           // 暂时退出
    SYLAR_LOG_DEBUG(g_logger) << "func end";
}


void test_fun(){
    sylar::Fiber::GetThis();    // 创建主协程，主协程的state一直是RUNNING
    sylar::Fiber::ptr fiber(new sylar::Fiber(&func, 0, false));
    SYLAR_LOG_DEBUG(g_logger) << "func befor start";
    fiber->resume();
    SYLAR_LOG_DEBUG(g_logger) << "func after start";
    fiber->resume();
    SYLAR_LOG_DEBUG(g_logger) << "main end";
}

int main(int argc, char** argv){
    sylar::Thread::SetName("main");
    SYLAR_LOG_DEBUG(g_logger) << "main start";
    
    std::vector<sylar::Thread::ptr> ths;
    for(int i = 0 ; i < 3 ; ++i){
        ths.push_back(sylar::Thread::ptr(new sylar::Thread(&test_fun, "thread_" + std::to_string(i))));
    }
    for(auto i : ths){
        i->join();
    }
    // SYLAR_LOG_DEBUG(g_logger) << SYLAR_Log_YAML2String();
    return 0;
}