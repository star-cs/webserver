#include "task.h"
#include "sylar/core/common/macro.h"
#include "sylar/core/scheduler.h"

namespace sylar::dag
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Task::Task(const std::string &name) : m_name(name)
{
    // 初始化结果指针
    m_result = std::make_shared<TaskResult>();
}

void Task::addPredecessor(const TaskPtr &task)
{
    if (task && task != shared_from_this()) {
        m_predecessors.push_back(task);
        task->addSuccessor(shared_from_this());
    }
}

void Task::addSuccessor(const TaskPtr &task)
{
    if (task && task != shared_from_this()) {
        m_successors.push_back(task);
    }
}

bool Task::areAllPredecessorsCompleted() const
{
    return m_completed_predecessors.load() == m_predecessors.size();
}

void Task::waitForCompletion()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() {
        return m_state.load() == TaskState::FINISHED || m_state.load() == TaskState::FAILED;
    });
}

void Task::run()
{
    // 检查任务是否可以运行（所有前置任务都已完成）
    if (!areAllPredecessorsCompleted()) {
        SYLAR_LOG_WARN(g_logger) << "Task " << m_name
                                 << " cannot run, not all predecessors are completed";
        m_state.store(TaskState::FAILED);
        m_result->success = false;
        m_result->error_msg = "Not all predecessors are completed";
        return;
    }

    // 设置任务状态为运行中
    m_state.store(TaskState::RUNNING);

    try {
        // 执行任务
        auto result = execute();
        setResult(result);

        // 设置任务状态
        if (result->success) {
            m_state.store(TaskState::FINISHED);
        } else {
            m_state.store(TaskState::FAILED);
        }
    } catch (const std::exception &e) {
        m_state.store(TaskState::FAILED);
        auto result = std::make_shared<TaskResult>();
        result->success = false;
        result->error_msg = std::string("Exception: ") + e.what();
        setResult(result);
        SYLAR_LOG_ERROR(g_logger) << "Task " << m_name << " failed: " << e.what();
    } catch (...) {
        m_state.store(TaskState::FAILED);
        auto result = std::make_shared<TaskResult>();
        result->success = false;
        result->error_msg = "Unknown exception";
        setResult(result);
        SYLAR_LOG_ERROR(g_logger) << "Task " << m_name << " failed with unknown exception";
    }

    // 通知所有等待此任务完成的线程
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.notify_all();
    }

    // 通知所有后置任务，此任务已完成
    for (const auto &successor : m_successors) {
        successor->incrementCompletedPredecessors();
    }
}

FunctionTask::FunctionTask(const std::string &name, const FunctionType &func)
    : Task(name), m_func(func)
{
    SYLAR_ASSERT2(func, "Task function cannot be nullptr");
}

TaskResult::ptr FunctionTask::execute()
{
    if (m_func) {
        return m_func();
    }
    auto result = std::make_shared<TaskResult>();
    result->success = false;
    result->error_msg = "Task function is nullptr";
    return result;
}

TaskPtr createFunctionTask(const std::string &name, const FunctionTask::FunctionType &func)
{
    return std::make_shared<FunctionTask>(name, func);
}

} // namespace sylar::dag
