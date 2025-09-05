#ifndef SYLAR_SIGNAL_H
#define SYLAR_SIGNAL_H

#include <signal.h>
#include <functional>
#include <memory>
#include "sylar/core/memory/memorypool.h"
#include "sylar/core/fiber.h"
#include "sylar/core/log/log.h"

namespace sylar {

/**
 * @brief 信号处理类
 */
class SignalManager {
public:
    using ptr = std::shared_ptr<SignalManager>;
    using SignalCallback = std::function<void(int)>;

    /**
     * @brief 初始化信号管理器
     */
    static void Init();

    /**
     * @brief 注册信号处理函数
     * @param signo 信号编号
     * @param callback 回调函数
     * @param restart 是否自动重启被信号中断的系统调用
     */
    static void RegisterSignal(int signo, const SignalCallback& callback, bool restart = true);

    /**
     * @brief 忽略信号
     * @param signo 信号编号
     */
    static void IgnoreSignal(int signo);

    /**
     * @brief 恢复信号默认处理
     * @param signo 信号编号
     */
    static void RestoreSignal(int signo);

    /**
     * @brief 设置优雅退出信号
     * @param signo 信号编号
     */
    static void SetExitSignal(int signo);

    /**
     * @brief 是否是退出信号
     * @param signo 信号编号
     * @return 是否是退出信号
     */
    static bool IsExitSignal(int signo);

    /**
     * @brief 处理保护页异常信号
     * @param signo 信号编号
     * @param info 信号信息
     * @param context 上下文信息
     */
    static void HandleProtectPageSignal(int signo, siginfo_t* info, void* context);

private:
    // 退出信号集合
    static std::vector<int> s_exitSignals;
};

} // namespace sylar

#endif // SYLAR_SIGNAL_H