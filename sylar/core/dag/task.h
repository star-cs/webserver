#ifndef __SYLAR_DAG_TASK_H__
#define __SYLAR_DAG_TASK_H__

#include <any>
#include <condition_variable>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include "sylar/core/fiber.h"
#include "sylar/core/log/log.h"

namespace sylar
{
namespace dag
{

    class Task;
    class DAGExecutor;

    using TaskPtr = std::shared_ptr<Task>;

    /**
     * @brief 任务执行结果
     */
    struct TaskResult {
        using ptr = std::shared_ptr<TaskResult>;
        bool success = true;
        std::string error_msg = "";
        std::shared_ptr<void> data;
    };

    /**
     * @brief 任务状态
     */
    enum class TaskState {
        READY,    // 就绪状态，等待执行
        RUNNING,  // 正在执行
        FINISHED, // 执行完成
        FAILED    // 执行失败
    };

    /**
     * @brief DAG任务基类
     */
    class Task : public std::enable_shared_from_this<Task>
    {
    public:
        friend class DAG;
        friend class DAGExecutor;

        /**
         * @brief 构造函数
         * @param name 任务名称
         */
        explicit Task(const std::string &name);

        virtual ~Task() = default;

        /**
         * @brief 获取任务名称
         */
        const std::string &getName() const { return m_name; }

        /**
         * @brief 获取任务状态
         */
        TaskState getState() const { return m_state.load(); }

        /**
         * @brief 获取任务结果
         */
        TaskResult::ptr getResult() const { return m_result; }

        /**
         * @brief 添加前置任务（依赖任务）
         * @param task 前置任务
         */
        void addPredecessor(const TaskPtr &task);

        /**
         * @brief 添加后置任务（被依赖任务）
         * @param task 后置任务
         */
        void addSuccessor(const TaskPtr &task);

        /**
         * @brief 获取前置任务列表
         */
        const std::vector<TaskPtr> &getPredecessors() const { return m_predecessors; }

        /**
         * @brief 获取后置任务列表
         */
        const std::vector<TaskPtr> &getSuccessors() const { return m_successors; }

        /**
         * @brief 设置任务执行上下文
         */
        void setContext(const std::any &context) { m_context = context; }

        /**
         * @brief 获取任务执行上下文
         */
        const std::any &getContext() const { return m_context; }

        /**
         * @brief 设置任务超时时间（毫秒）
         */
        void setTimeout(uint64_t timeout_ms) { m_timeout_ms = timeout_ms; }

        /**
         * @brief 获取任务超时时间
         */
        uint64_t getTimeout() const { return m_timeout_ms; }

        /**
         * @brief 等待任务完成
         */
        void waitForCompletion();

        /**
         * @brief 获取前置任务完成数量
         */
        size_t getCompletedPredecessorsCount() const { return m_completed_predecessors.load(); }

        /**
         * @brief 检查是否所有前置任务都已完成
         */
        bool areAllPredecessorsCompleted() const;

    protected:
        /**
         * @brief 任务执行函数
         * @return 任务执行结果
         */
        virtual TaskResult::ptr execute() = 0;

        /**
         * @brief 执行任务（由DAGExecutor调用）
         */
        void run();

        /**
         * @brief 设置任务状态
         */
        void setState(TaskState state) { m_state.store(state); }

        /**
         * @brief 设置任务结果
         */
        void setResult(TaskResult::ptr result) { m_result = result; }

        /**
         * @brief 增加已完成的前置任务计数
         */
        void incrementCompletedPredecessors() { m_completed_predecessors.fetch_add(1); }

    private:
        std::string m_name;                               // 任务名称
        std::atomic<TaskState> m_state{TaskState::READY}; // 任务状态
        TaskResult::ptr m_result;                         // 任务结果
        std::vector<TaskPtr> m_predecessors;              // 前置任务列表
        std::vector<TaskPtr> m_successors;                // 后置任务列表
        std::atomic<size_t> m_completed_predecessors{0};  // 已完成的前置任务数量
        std::any m_context;                               // 任务执行上下文
        uint64_t m_timeout_ms{0};                         // 任务超时时间，0表示不超时
        std::mutex m_mutex;                               // 互斥锁，用于线程同步
        std::condition_variable m_cv;                     // 条件变量，用于等待任务完成
    };

    /**
     * @brief 函数任务，封装普通函数为DAG任务
     */
    class FunctionTask : public Task
    {
    public:
        using FunctionType = std::function<TaskResult::ptr()>;

        /**
         * @brief 构造函数
         * @param name 任务名称
         * @param func 任务函数
         */
        FunctionTask(const std::string &name, const FunctionType &func);

    protected:
        TaskResult::ptr execute() override;

    private:
        FunctionType m_func; // 任务函数
    };

    /**
     * @brief 创建函数任务的工厂函数
     */
    TaskPtr createFunctionTask(const std::string &name, const FunctionTask::FunctionType &func);

} // namespace dag
} // namespace sylar

#endif // __SYLAR_DAG_TASK_H__