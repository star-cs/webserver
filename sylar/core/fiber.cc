#include <atomic>

#include "sylar/core/fiber.h"
#include "sylar/core/log/log.h"
#include "sylar/core/config/config.h"
#include "sylar/core/common/macro.h"
#include "scheduler.h"
#include "sylar/core/memory/memorypool.h"

namespace sylar
{
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");
// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};
// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

// 当前正在运行的协程，会不停修改的~
static thread_local Fiber *t_fiber{nullptr};
// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber{nullptr};

static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 128 * 1024, "fiber stack size");

// 上下文
Context::Context(fn_t fn, intptr_t vp, std::size_t stackSize) : fn_(fn), vp_(vp)
{
    stackSize_ = stackSize_ ? stackSize : g_fiber_stack_size->getValue();
    stack_ = (char *)SYLAR_THREAD_MALLOC(stackSize_);

    // malloc分配的堆地址，但是实际协程会模拟为栈（从高地址开始使用空间）
    ctx_ = libgo_make_fcontext(stack_ + stackSize_, stackSize_, fn_);

    // 设置保护页（设置在低地址处）
    SYLAR_MALLOC_PROTECT(stack_, stackSize_);
}

Context::~Context()
{
    if (stack_) {
        SYLAR_MALLOC_UNPROTECT(stack_);
        SYLAR_THREAD_FREE(stack_, stackSize_);
        stack_ = nullptr;
    }
}

// 主协程
Fiber::Fiber()
{
    m_state = RUNNING;
    SetThis(this);
    ++s_fiber_count;
    m_id = s_fiber_id++; // 协程id从0开始。
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() main id = " << m_id;
}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
    : m_id(s_fiber_id++), m_ctx(&Fiber::MainFunc, (intptr_t)(this), stacksize), m_cb(cb),
      m_runInScheduler(run_in_scheduler)
{

    ++s_fiber_count;

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}

Fiber::~Fiber()
{
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::~Fiber() id = " << m_id;

    --s_fiber_count;
    if (m_ctx.hasStack()) { // 如果有协程栈地址，那么就是子协程。
        SYLAR_ASSERT(m_state == TERM);

        // SYLAR_LOG_DEBUG(g_logger) << "dealloc stack, id = " << m_id;
    } else {
        // 主协程
        SYLAR_ASSERT(!m_cb);              // 主协程没有目标函数 cb
        SYLAR_ASSERT(m_state == RUNNING); // 主协程执行状态

        Fiber *cur = t_fiber;
        if (cur == this) {
            SetThis(nullptr);
        }
    }
}

void Fiber::reset(std::function<void()> cb)
{
    SYLAR_ASSERT(m_ctx.hasStack());
    SYLAR_ASSERT(m_state == TERM);

    m_cb = cb;
    m_state = READY;
}

/**
 * 当前和正在运行的协程交换，前者RUNNING，后者READY
 * 想要执行的协程调用这个~
 * 如果协程参与调度器调度，那么应该和调度器的主协程进行swap，而不是线程主协程
 */
void Fiber::resume()
{
    SYLAR_ASSERT(m_state == READY); // 当前的子协程应该是 READY
    SetThis(this);
    m_state = RUNNING;
    if (m_runInScheduler) { // 相当于当前协程，是任务协程。 t_scheduler_fiber --> t_fiber
        Scheduler::GetMainFiber()->m_ctx.SwapTo(m_ctx);

    } else { // t_thread_fiber --> t_scheduler_fiber
        t_thread_fiber->m_ctx.SwapTo(m_ctx);
    }
}

/**
 * 当前协程与上次resume时退到后台的协程进行交换，前者状态变为READY，后者状态变为RUNNING
 * 当前子协程，运行完cb之后会为TERM，调用yield；同时运行时RUNNING也能调用yield.
 * 当READY时，没有必要调用yield，因为当时还没有使用到资源~
 */
void Fiber::yield()
{
    SYLAR_ASSERT(m_state == TERM || m_state == RUNNING) // 当前子协程可以是 TERM，RUNNING
    if (m_state != TERM) { // 如果没有结束，中途进行yield，状态设置为READY，可能还会回来继续执行。
        m_state = READY;
    }

    if (m_runInScheduler) { // 同 resume()   t_fiber --> t_scheduler_fiber
        SetThis(Scheduler::GetMainFiber());
        m_ctx.SwapTo(Scheduler::GetMainFiber()->m_ctx);

    } else { // t_scheduler_fiber --> t_thread_fiber
        SetThis(
            t_thread_fiber
                .get()); // 这个是 在swapcontext前确定好？？保证上下文切换后t_fiber指针立即指向目标协程（主协程）

        m_ctx.SwapTo(t_thread_fiber->m_ctx);
    }
}

// static -------------------

void Fiber::SetThis(Fiber *f)
{
    t_fiber = f;
}

Fiber::ptr Fiber::GetThis()
{
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }

    // 如果 当前没有协程，那么就创建第一个协程。
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_fiber->shared_from_this();
}

uint64_t Fiber::TotalFibers()
{
    return s_fiber_count;
}

void Fiber::MainFunc(intptr_t vp)
{
    Fiber *cur = (Fiber *)(vp);
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
    } catch (std::exception &e) {
        SYLAR_LOG_ERROR(g_logger) << "Fiber::MainFunc() error " << e.what();
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "Fiber Excpet ";
    }
    cur->m_cb = nullptr;
    cur->m_state = State::TERM;

    cur->yield();
}

uint64_t Fiber::GetFiberId()
{
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

} // namespace sylar
