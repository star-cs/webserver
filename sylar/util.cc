#include "util.h"
#include <execinfo.h>
#include <stdlib.h>
#include "log.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

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

    void Backtrace(std::vector<std::string>& bt, int size = 64, int skip = 1){
        // 存储堆栈地址
        void** array = (void**)malloc(sizeof(void*) * size);    
        // 获取当前堆栈地址信息，返回实际获取的层数
        size_t s = ::backtrace(array, size);      

        // 将地址信息转换为可读的符号字符串数组
        char** strings = backtrace_symbols(array, size);
        if(strings == NULL){
            SYLAR_LOG_ERROR(g_logger) << "backtrace_symbols error";
            return ;
        }

        for(size_t i = skip ; i < s ; ++i){
            bt.push_back(strings[i]);
        }
    }

    std::string BacktraceToString(int size, int skip, const std::string &prefix){
        std::vector<std::string> bt;
        Backtrace(bt, size, skip);
        std::stringstream ss;
        for (size_t i = 0; i < bt.size(); ++i) {
            ss << prefix << bt[i] << std::endl;
        }
        return ss.str();
    }

}