#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void func1(void* arg){
    SYLAR_LOG_INFO(g_logger) << "name:" << sylar::Thread::GetName() // t_thread_name
        << " this.name:" << sylar::Thread::GetThis()->getName()     // t_thread 里的 name
        << " thread name:" << sylar::GetThreadName()                // 查询 系统 的 name
        << " id:" << sylar::GetThreadId()
        << " this.id:" << sylar::Thread::GetThis()->getId();
    SYLAR_LOG_INFO(g_logger) << "arg: " << *(int*)arg;
}


int main(int argc, char** argv){
    std::vector<sylar::Thread::ptr> thrs;
    int arg = 100;
    for(int i = 0 ; i < 3; ++i){
        thrs.push_back(sylar::Thread::ptr(new sylar::Thread(std::bind(func1, &arg), "thread_" + std::to_string(i))));
    }
    for(int i = 0 ; i < 3; ++i){
        thrs[i]->join();
    }
    return 0;
}
