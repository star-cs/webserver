#include "sylar/sylar.h"

using namespace sylar;

Logger::ptr g_logger = SYLAR_LOG_ROOT();

// 递归函数，用于触发栈溢出
void recursive_function(int depth)
{
    if (depth > 20) {
        return;
    }
    // 在栈上分配大量内存
    char buffer[1024 * 3];
    // memset(buffer, 0, sizeof(buffer));

    std::cout << "Recursive depth: " << depth << ", Stack address: " << &buffer << std::endl;
    // std::cout << "Recursive depth: " << depth << std::endl;
    // 继续递归，直到栈溢出
    recursive_function(depth + 1);
}

// 协程入口函数
void fiber_func()
{
    SYLAR_LOG_INFO(g_logger) << "Fiber started, trying to overflow stack...";
    try {
        recursive_function(0);
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "Exception caught in fiber";
    }
    SYLAR_LOG_INFO(g_logger) << "Fiber should not reach here";
}

// 测试函数
void test_stack_overflow()
{
    SYLAR_LOG_INFO(g_logger) << "Testing fiber stack overflow protection...";

    // 创建一个协程，设置较小的栈大小以更快触发溢出
    Fiber::ptr fiber(new Fiber(&fiber_func, 16 * 1024, true));

    IOManager iom;
    iom.schedule(fiber);

    SYLAR_LOG_INFO(g_logger) << "Main thread continues after fiber stack overflow";

    // 等待一段时间，确保异常处理完成
    sleep(1);
}

int main(int argc, char **argv)
{
    Thread::SetName("main");

    // 初始化信号管理器
    SignalManager::Init();

    SYLAR_LOG_INFO(g_logger) << "Main start";

    // 运行测试
    test_stack_overflow();

    SYLAR_LOG_INFO(g_logger) << "Main end";
    return 0;
}