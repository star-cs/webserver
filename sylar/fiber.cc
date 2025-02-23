#include <atomic>

#include "fiber.h"
#include "log.h"
#include "config.h"
#include "macro.h"
#include "scheduler.h"

namespace sylar
{
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};
// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

// 当前正在运行的协程，会不停修改的~
static thread_local Fiber* t_fiber = nullptr;
// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber = nullptr;

static ConfigVar<uint32_t>::ptr g_fiber_stack_size = Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

class MallocStackAllocator{
public:
    static void *Alloc(size_t size){return malloc(size);}
    static void Dealloc(void* vp, size_t size){return free(vp);}
};

using StackAllocator = MallocStackAllocator;

// 主协程
Fiber::Fiber(){
    m_state = RUNNING;
    SetThis(this);
    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }
    ++s_fiber_count;
    m_id = s_fiber_id++;        // 协程id从0开始。
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() main id = " << m_id;
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler): m_id(s_fiber_id++), m_cb(cb), m_runInScheduler(run_in_scheduler) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);
    
    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getContext");
    }
    m_ctx.uc_link = nullptr;                // 下一次指向的上下文 任务~
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}

Fiber::~Fiber(){
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber() id = " << m_id;

    --s_fiber_count;
    if(m_stack){    // 如果有协程栈地址，那么就是子协程。
        SYLAR_ASSERT(m_state == TERM);
        StackAllocator::Dealloc(m_stack, m_stacksize);
        SYLAR_LOG_DEBUG(g_logger) << "dealloc stack, id = " << m_id;

    } else {
        // 主协程
        SYLAR_ASSERT(!m_cb);        // 主协程没有目标函数 cb
        SYLAR_ASSERT(m_state == RUNNING);   // 主协程执行状态

        Fiber* cur = t_fiber;
        if(cur == this){
            SetThis(nullptr);
        }
    }
}


void Fiber::reset(std::function<void()> cb){
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM);

    m_cb = cb;

    if(getcontext(&m_ctx)){
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    
    m_state = READY;
}

/**
 * 当前和正在运行的协程交换，前者RUNNING，后者READY
 * 想要执行的协程调用这个~
 * 如果协程参与调度器调度，那么应该和调度器的主协程进行swap，而不是线程主协程
 */
void Fiber::resume(){
    SYLAR_ASSERT(m_state == READY);    // 当前的子协程应该是 READY
    SetThis(this);
    m_state = RUNNING;
    if(m_runInScheduler){   // 相当于当前协程，是任务协程。 t_scheduler_fiber --> t_fiber
        if(swapcontext(&(Scheduler::GetMainFiber()->m_ctx) , &m_ctx)){
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }else{      // t_thread_fiber --> t_scheduler_fiber
        if(swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)){
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

/**
 * 当前协程与上次resume时退到后台的协程进行交换，前者状态变为READY，后者状态变为RUNNING
 * 当前子协程，运行完cb之后会为TERM，调用yield；同时运行时RUNNING也能调用yield.
 * 当READY时，没有必要调用yield，因为当时还没有使用到资源~
 */
void Fiber::yield(){
    SYLAR_ASSERT(m_state == TERM || m_state == RUNNING)     // 当前子协程可以是 TERM，RUNNING
    SetThis(t_thread_fiber.get());                          // 这个是 在swapcontext前确定好？？保证上下文切换后t_fiber指针立即指向目标协程（主协程）
    if(m_state != TERM){    // 如果没有结束，中途进行yield，状态设置为READY，可能还会回来继续执行。
        m_state = READY;
    }

    if(m_runInScheduler){   // 同 resume()   t_fiber --> t_scheduler_fiber
        if(swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx))){
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }else {     // t_scheduler_fiber --> t_thread_fiber
        if(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))){
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

// static -------------------

void Fiber::SetThis(Fiber *f){
    t_fiber = f;
}

Fiber::ptr Fiber::GetThis(){
    if(t_fiber){
        return t_fiber->shared_from_this();
    }

    // 如果 当前没有协程，那么就创建第一个协程。
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

uint64_t Fiber::TotalFibers(){
    return s_fiber_count;
}

void Fiber::MainFunc(){
    Fiber::ptr cur = GetThis();     // 这里会获取到当前运行协程的 智能指针，其引用+1
    SYLAR_ASSERT(cur);
    try{
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = State::TERM;
    } catch(std::exception e){
        SYLAR_LOG_ERROR(g_logger) << "Fiber::MainFunc() error " << e.what();
    } catch(...){
        SYLAR_LOG_ERROR(g_logger) << "Fiber Excpet ";
    }
    
    auto raw_ptr = cur.get();      // 获取原始指针避免智能指针的引用计数干扰
    cur.reset();                   // 解除当前上下文对协程对象的所有权
    raw_ptr->yield();
}

uint64_t Fiber::GetFiberId(){
    if(t_fiber){
        return t_fiber->getId();
    }
    return 0;
}


} // namespace sylar

