#include "daemon.h"
#include "sylar/core/log/log.h"
#include "sylar/core/config/config.h"
#include <sys/wait.h>
#include <sys/types.h>

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static sylar::ConfigVar<uint32_t>::ptr g_daemon_restart_interval =
    sylar::Config::Lookup("daemon.restart_interval", (uint32_t)5, "daemon restart interval");

std::string ProcessInfo::toString() const
{
    std::stringstream ss;
    ss << "[ProcessInfo parent_id=" << parent_id << " main_id=" << main_id
       << " parent_start_time=" << sylar::Time2Str(parent_start_time)
       << " main_start_time=" << sylar::Time2Str(main_start_time)
       << " restart_count=" << restart_count << "]";
    return ss.str();
}

static int real_start(int argc, char **argv, std::function<int(int argc, char **argv)> main_cb)
{
    return main_cb(argc, argv);
}

static int real_daemon(int argc, char **argv, std::function<int(int argc, char **argv)> main_cb)
{
    auto ret = daemon(1, 0);
    if(ret == -1){
        SYLAR_LOG_ERROR(g_logger) << "daemon fail errno=" << errno << " errstr=" << strerror(errno);
    }
    ProcessInfoMgr::GetInstance()->parent_id = getpid();
    ProcessInfoMgr::GetInstance()->parent_start_time = time(0);
    while (true) {
        pid_t pid = fork();
        if (pid == 0) {
            //子进程返回
            ProcessInfoMgr::GetInstance()->main_id = getpid();
            ProcessInfoMgr::GetInstance()->main_start_time = time(0);
            SYLAR_LOG_INFO(g_logger) << "process start pid=" << getpid();
            return real_start(argc, argv, main_cb);
        } else if (pid < 0) {
            SYLAR_LOG_ERROR(g_logger) << "fork fail return=" << pid << " errno=" << errno
                                      << " errstr=" << strerror(errno);
            return -1;
        } else {
            //父进程返回
            /**
                低 8 位：如果子进程是被信号终止的，存储终止信号的编号；
                高 8 位：如果子进程正常退出（调用 exit 或 return），存储退出码（即 exit(code) 中的 code）；
                其他位：可能包含附加信息（如是否产生核心转储文件等）。
            */
            int status = 0;
            waitpid(pid, &status, 0);   // 等待子线程结束
            if (status) {
                SYLAR_LOG_ERROR(g_logger) << "child crash pid=" << pid << " status=" << status;
            } else {
                SYLAR_LOG_INFO(g_logger) << "child finished pid=" << pid;
                break;
            }
            ProcessInfoMgr::GetInstance()->restart_count += 1;
            sleep(g_daemon_restart_interval->getValue());
        }
    }
    return 0;
}

int start_daemon(int argc, char **argv, std::function<int(int argc, char **argv)> main_cb,
                 bool is_daemon)
{
    if (!is_daemon) {
        return real_start(argc, argv, main_cb);
    }
    return real_daemon(argc, argv, main_cb);
}
} // namespace sylar