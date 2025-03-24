#include "hook.h"
#include <dlfcn.h>
#include "sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

namespace sylar
{
// 判断当前线程是否需要 HOOK
static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) 

void hook_init(){
    static bool is_inited = false;
    if(is_inited){
        return;
    }
    //保存原函数：hook_init() 通过 dlsym(RTLD_NEXT, "sleep") 获取系统原版 sleep 函数的地址，保存到 sleep_f 指针
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
}
    
struct _HookIniter {
    _HookIniter(){
        hook_init();
    }
};
    
static _HookIniter  s_hook_initer;

bool is_hook_enable(){
    return t_hook_enable;
}

void set_hook_enable(bool flag){
    t_hook_enable = flag;
}


} // namespace sylar


extern "C" {
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

unsigned int sleep(unsigned int seconds){
    if(!sylar::t_hook_enable){
        return sleep_f(seconds);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    /**
     * C++规定成员函数指针的类型包含类信息，即使存在继承关系，&IOManager::schedule 和 &Scheduler::schedule 属于不同类型。
     * 通过强制转换，使得类型系统接受子类对象iom调用基类成员函数的合法性。
     * 
     * schedule是模板函数
     * 子类继承的是模板的实例化版本，而非原始模板
     * 直接取地址会导致函数签名包含子类类型信息
     * 
     * std::bind 的类型安全机制
     * bind要求成员函数指针类型与对象类型严格匹配。当出现以下情况时必须转换：
     * 
     * 总结，当需要绑定 子类对象调用父类模板成员函数，父类函数需要强转成父类
     * (存在多继承或虚继承导致this指针偏移)
     * 
     * 或者
     * std::bind(&Scheduler::schedule, static_cast<Scheduler*>(iom), fiber, -1)
     * 
     */
    iom->addTimer(seconds * 1000 , std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))
                                                &sylar::IOManager::schedule, iom, fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

int usleep(useconds_t usec){
    if(!sylar::t_hook_enable){
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)(sylar::Fiber::ptr, int thread))
                                            &sylar::IOManager::schedule, iom , fiber, -1));
    sylar::Fiber::GetThis()->yield();
    return 0;
}

}