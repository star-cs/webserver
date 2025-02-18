#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int count = 0;

// sylar::RWMutex s_mutex;
sylar::Mutex s_mutex;

void func1(void* arg){
    SYLAR_LOG_INFO(g_logger) << "name:" << sylar::Thread::GetName() // t_thread_name
        << " this.name:" << sylar::Thread::GetThis()->getName()     // t_thread 里的 name
        << " thread name:" << sylar::GetThreadName()                // 查询 系统 的 name
        << " id:" << sylar::GetThreadId()
        << " this.id:" << sylar::Thread::GetThis()->getId();
    SYLAR_LOG_INFO(g_logger) << "arg: " << *(int*)arg;

    for(int i = 0 ; i < 10000 ; i++){
        // sylar::RWMutex::WriteLock lock(s_mutex);
        sylar::Mutex::Lock lock(s_mutex);
        count ++;
    }
}


void func2(void* arg){
    int num = *(int*)arg;
    std::cout << num;
    while(num--){
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "++++++++++func2++++++++++";
        sleep(1);
    }
}

void func3(void* arg){
    int num = *(int*)arg;
    std::cout << num;
    while(num--){
        SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "==========func3==========";
        sleep(1);
    }
}

int main(int argc, char** argv){
    SYLAR_LOG_INFO(g_logger) << "start";

    YAML::Node node = YAML::LoadFile("/home/yang/projects/webserver/bin/conf/log2.yml");
    sylar::Config::LoadFromYaml(node);

    SYLAR_LOG_INFO(g_logger) << "start";

    std::cout << SYLAR_Log_YAML2String() << std::endl;
    
    std::vector<sylar::Thread::ptr> thrs;
    int arg = 100;
    for(int i = 0 ; i < 2; ++i){
        thrs.push_back(sylar::Thread::ptr(new sylar::Thread(std::bind(func2, &arg), "thread_" + std::to_string(i * 2))));       // 0 2
        thrs.push_back(sylar::Thread::ptr(new sylar::Thread(std::bind(func3, &arg), "thread_" + std::to_string(i * 2 + 1))));   // 1 3
    }
    for(size_t i = 0 ; i < thrs.size(); ++i){
        thrs[i]->join();
    }

    SYLAR_LOG_INFO(g_logger) << "end";
    return 0;
}
