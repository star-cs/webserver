#include "signal.h"
#include <iostream>
#include <algorithm>
#include "sylar/core/scheduler.h"
#include "sylar/core/thread.h"
#include "sylar/core/memory/memorypool.h"

namespace sylar
{
static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

std::vector<int> SignalManager::s_exitSignals;

void SignalManager::Init()
{
    // 初始化退出信号
    SetExitSignal(SIGINT);
    SetExitSignal(SIGTERM);

    // 注册保护页异常信号处理
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = HandleProtectPageSignal;
    sa.sa_flags = SA_SIGINFO; // 允许获取信号详情（如错误地址）
    sigemptyset(&sa.sa_mask);

    // 段错误
    if (sigaction(SIGSEGV, &sa, nullptr) != 0) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to register SIGSEGV signal handler";
    }
    // 总线错误
    if (sigaction(SIGBUS, &sa, nullptr) != 0) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to register SIGBUS signal handler";
    }

    SYLAR_LOG_INFO(g_logger) << "SignalManager initialized";
}

void SignalManager::RegisterSignal(int signo, const SignalCallback &callback, bool restart)
{
    // 检查是否是保护页信号，如果是则不覆盖其处理函数
    if (signo == SIGSEGV || signo == SIGBUS) {
        SYLAR_LOG_WARN(g_logger) << "Cannot register exit handler for signal " << signo
                                 << ", it's used for protect page handling";
        return;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int sig) {
        auto scheduler = Scheduler::GetThis();
        if (scheduler) {
            scheduler->stop();
        }
        SYLAR_LOG_INFO(g_logger) << "Received exit signal " << sig << ", exiting...";
        exit(0);
    };

    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }

    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) != 0) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to register signal " << signo;
    }
}

void SignalManager::IgnoreSignal(int signo)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) != 0) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to ignore signal " << signo;
    }
}

void SignalManager::RestoreSignal(int signo)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) != 0) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to restore signal " << signo;
    }
}

void SignalManager::SetExitSignal(int signo)
{
    if (std::find(s_exitSignals.begin(), s_exitSignals.end(), signo) == s_exitSignals.end()) {
        s_exitSignals.push_back(signo);
        RegisterSignal(signo, nullptr);
    }
}

bool SignalManager::IsExitSignal(int signo)
{
    return std::find(s_exitSignals.begin(), s_exitSignals.end(), signo) != s_exitSignals.end();
}

void SignalManager::HandleProtectPageSignal(int signo, siginfo_t *info, void *context)
{
    // 获取出错地址
    void *fault_addr = info->si_addr;
    std::cerr << "HandleProtectPageSignal signo " << signo << std::endl;
    // 尝试找到对应的协程
    Fiber::ptr current_fiber = Fiber::GetThis();
    if (current_fiber) {
        std::cerr << "Fault occurred in fiber " << current_fiber->getId()
                  << ", stack address range: [" << current_fiber->getContext().getStackAddr()
                  << ", "
                  << current_fiber->getContext().getStackAddr()
                         + current_fiber->getContext().getStackSize()
                  << ")" << std::endl;

        // 检查是否是栈溢出
        char *stack_start = current_fiber->getContext().getStackAddr();
        uint32_t stack_size = current_fiber->getContext().getStackSize();
        char *stack_end = stack_start + stack_size;
        char *fault_ptr = (char *)fault_addr;

        if (fault_ptr >= stack_start && fault_ptr < stack_end) {
            std::cerr << "Stack overflow detected in fiber " << current_fiber->getId() << std::endl;

            // 尝试销毁该协程
            current_fiber->setState(Fiber::TERM);

            // 切换到主协程，不尝试释放内存以避免二次崩溃
            Fiber *main_fiber = Scheduler::GetMainFiber();
            if (main_fiber) {
                std::cerr << "Switching to main fiber..." << std::endl;
                // 协程切换
                current_fiber->yield();
            } else {
                std::cerr << "Main fiber not found, cannot recover" << std::endl;
                exit(1);
            }
        } else {
            std::cerr << "Fault not in fiber stack, exiting" << std::endl;
            exit(1);
        }
    } else {
        std::cerr << "No current fiber, exiting" << std::endl;
        exit(1);
    }
}

} // namespace sylar