#ifndef __SYLAR_DAG_DAG_EXECUTOR_H__
#define __SYLAR_DAG_DAG_EXECUTOR_H__

#include "dag.h"
#include "sylar/core/iomanager.h"
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace sylar
{
namespace dag
{

    class DAGExecutor
    {
    public:
        using ptr = std::shared_ptr<DAGExecutor>;

        /**
         * @brief 执行器状态
         */
        enum class State {
            IDLE,      // 空闲状态
            RUNNING,   // 正在运行
            PAUSED,    // 已暂停
            COMPLETED, // 执行完成
            FAILED,    // 执行失败
            STOPPED    // 已停止
        };

        /**
         * @brief 构造函数
         * @param worker 协程调度器
         */
        explicit DAGExecutor(IOManager::ptr worker = nullptr);

        /**
         * @brief 析构函数
         */
        virtual ~DAGExecutor();

        /**
         * @brief 设置要执行的DAG
         * @param dag DAG对象
         */
        void setDAG(DAG::ptr dag);

        /**
         * @brief 获取当前执行的DAG
         */
        DAG::ptr getDAG() const { return m_dag; }

        /**
         * @brief 设置协程调度器
         * @param scheduler 协程调度器
         */
        void setScheduler(Scheduler::ptr scheduler);

        /**
         * @brief 获取协程调度器
         */
        IOManager::ptr getWorker() const { return m_worker; }

        /**
         * @brief 设置协程调度器
         * @param worker 协程调度器
         */
        void setWorker(IOManager::ptr worker);

        /**
         * @brief 开始执行DAG
         * @return 是否开始成功
         */
        bool start();

        /**
         * @brief 暂停执行DAG
         */
        void pause();

        /**
         * @brief 恢复执行DAG
         */
        void resume();

        /**
         * @brief 停止执行DAG
         */
        void stop();

        /**
         * @brief 获取执行器状态
         */
        State getState() const { return m_state.load(); }

        /**
         * @brief 等待DAG执行完成
         * @param timeout_ms 超时时间（毫秒），0表示无限等待
         * @return 是否在超时前完成
         */
        bool waitForCompletion(uint64_t timeout_ms = 0);

        /**
         * @brief 获取已完成的任务数量
         */
        size_t getCompletedTaskCount() const { return m_completed_tasks.load(); }

        /**
         * @brief 获取失败的任务数量
         */
        size_t getFailedTaskCount() const { return m_failed_tasks.load(); }

        /**
         * @brief 检查DAG是否执行完成
         */
        bool isCompleted() const { return m_state.load() == State::COMPLETED; }

        /**
         * @brief 检查DAG是否执行失败
         */
        bool isFailed() const { return m_state.load() == State::FAILED; }

        /**
         * @brief 检查DAG是否正在执行
         */
        bool isRunning() const { return m_state.load() == State::RUNNING; }

        /**
         * @brief 检查DAG是否已暂停
         */
        bool isPaused() const { return m_state.load() == State::PAUSED; }

        /**
         * @brief 检查DAG是否已停止
         */
        bool isStopped() const { return m_state.load() == State::STOPPED; }

    protected:
        /**
         * @brief 执行任务
         * @param task 要执行的任务
         */
        void executeTask(const TaskPtr &task);

        /**
         * @brief 处理任务完成
         * @param task 已完成的任务
         */
        void handleTaskCompletion(const TaskPtr &task);

        /**
         * @brief 检查DAG是否执行完成
         */
        void checkCompletion();

        /**
         * @brief 重置执行器状态
         */
        void reset();

        /**
         * @brief 设置执行器状态
         */
        void setState(State state);

    private:
        DAG::ptr m_dag;                           // 要执行的DAG
        IOManager::ptr m_worker;                  // 协程调度器
        std::atomic<State> m_state{State::IDLE};  // 执行器状态
        std::atomic<size_t> m_completed_tasks{0}; // 已完成的任务数量
        std::atomic<size_t> m_failed_tasks{0};    // 失败的任务数量
        std::mutex m_mutex;                       // 互斥锁
        std::condition_variable m_cv;             // 条件变量
        bool m_own_worker{false};                 // 是否拥有调度器的所有权
    };

} // namespace dag
} // namespace sylar

#endif // __SYLAR_DAG_DAG_EXECUTOR_H__