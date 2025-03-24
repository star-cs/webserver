#ifndef __SYLAR_HOOK_H__
#define __SYLAR_HOOK_H__

#include <unistd.h>

namespace sylar{
    bool is_hook_enable();

    void set_hook_enable(bool flag);
}

extern "C"{

    // sleep 
    // 定义了函数指针类型 sleep_fun
    // 该类型对应原生 sleep 函数的签名（接收 unsigned int 参数，返回 unsigned int）
    typedef unsigned int (*sleep_fun)(unsigned int seconds);
    // 声明外部的全局函数指针变量 sleep_f，用于保存原始 sleep 函数的地址
    // 通过 sleep_f 仍能调用原版函数
    extern sleep_fun sleep_f;

    typedef int (*usleep_fun)(useconds_t usec);
    extern usleep_fun usleep_f;

}

#endif