#include "util.h"

namespace sylar{
    pid_t GetThreadId(){
        return syscall(SYS_gettid);
    }

    uint64_t GetFiberId(){
        return 0;
    }

    std::string GetThreadName(){
        char thread_name[16] = {0};
        pthread_getname_np(pthread_self(), thread_name, 16);
        return std::string(thread_name);
    }

    void SetThreadName(const std::string &name){
        pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
    }

}