#include "dag_executor.h"
#include "sylar/core/common/macro.h"
#include "sylar/core/log/log.h"

namespace sylar::dag
{

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

DAGExecutor::DAGExecutor(IOManager::ptr worker) : m_worker(worker), m_own_worker(false)
{
    // 如果没有提供调度器，创建一个默认的调度器
    if (!m_worker) {
        m_worker.reset(new IOManager(1, true, "DAGExecutorWorker"));
        m_own_worker = true;
    }
}

DAGExecutor::~DAGExecutor()
{
    // 停止执行器
    stop();

    // 如果拥有调度器的所有权，则停止并销毁调度器
    if (m_own_worker && m_worker) {
        m_worker->stop();
        m_worker = nullptr;
    }
}

void DAGExecutor::setDAG(DAG::ptr dag)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // 如果当前正在运行，不允许更改DAG
    if (isRunning()) {
        SYLAR_LOG_WARN(g_logger) << "Cannot set DAG while executor is running";
        return;
    }

    m_dag = dag;
    reset();
}

void DAGExecutor::setWorker(IOManager::ptr worker)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // 如果当前正在运行，不允许更改调度器
    if (isRunning()) {
        SYLAR_LOG_WARN(g_logger) << "Cannot set worker while executor is running";
        return;
    }

    // 如果拥有旧调度器的所有权，则停止并销毁它
    if (m_own_worker && m_worker) {
        m_worker->stop();
        m_own_worker = false;
    }

    m_worker = worker;

    // 如果没有提供调度器，创建一个默认的调度器
    if (!m_worker) {
        m_worker.reset(new IOManager(4, true, "DAGExecutorWorker"));
        m_own_worker = true;
    }
}

bool DAGExecutor::start()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // 检查DAG是否为空
    if (!m_dag || m_dag->isEmpty()) {
        SYLAR_LOG_WARN(g_logger) << "Cannot start executor with empty DAG";
        return false;
    }

    // 检查DAG是否有环
    if (m_dag->hasCycle()) {
        SYLAR_LOG_WARN(g_logger) << "Cannot start executor with cyclic DAG";
        return false;
    }

    // 检查执行器状态
    if (isRunning()) {
        SYLAR_LOG_WARN(g_logger) << "Executor is already running";
        return true;
    }

    if (isCompleted() || isFailed() || isStopped()) {
        reset();
    }

    // 设置状态为运行中
    setState(State::RUNNING);

    // 启动调度器（如果需要）
    // if (m_own_worker && m_worker) {
    //     m_worker->start();
    // }

    // 获取所有入度为0的任务（没有依赖的任务）并开始执行
    std::vector<TaskPtr> entry_tasks = m_dag->getEntryTasks();
    for (const auto &task : entry_tasks) {
        executeTask(task);
    }

    return true;
}

void DAGExecutor::pause()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (isRunning()) {
        setState(State::PAUSED);
        SYLAR_LOG_INFO(g_logger) << "DAG executor paused";
    }
}

void DAGExecutor::resume()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (isPaused()) {
        setState(State::RUNNING);
        SYLAR_LOG_INFO(g_logger) << "DAG executor resumed";

        // 唤醒等待条件变量的线程
        m_cv.notify_all();
    }
}

void DAGExecutor::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (!isCompleted() && !isStopped()) {
        setState(State::STOPPED);
        SYLAR_LOG_INFO(g_logger) << "DAG executor stopped";

        // 唤醒等待条件变量的线程
        m_cv.notify_all();
    }
}

bool DAGExecutor::waitForCompletion(uint64_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    auto predicate = [this]() { return isCompleted() || isFailed() || isStopped(); };

    if (timeout_ms == 0) {
        // 无限等待
        m_cv.wait(lock, predicate);
        return isCompleted();
    } else {
        // 有超时限制的等待
        auto status = m_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
        return status && isCompleted();
    }
}

void DAGExecutor::executeTask(const TaskPtr &task)
{
    if (!task || isStopped()) {
        return;
    }

    // 检查是否所有前置任务都已完成
    if (!task->areAllPredecessorsCompleted()) {
        return;
    }

    // 创建任务执行函数
    auto execute_func = [this, task]() {
        // 检查执行器状态
        if (isPaused()) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return !isPaused() || isStopped(); });

            // 如果执行器已停止，不再执行任务
            if (isStopped()) {
                return;
            }
        }

        if (isStopped()) {
            return;
        }

        // 执行任务
        task->run();

        // 处理任务完成
        handleTaskCompletion(task);
    };

    // 将任务提交到调度器
    if (m_worker) {
        m_worker->schedule(execute_func);
    }
}

void DAGExecutor::handleTaskCompletion(const TaskPtr &task)
{
    if (!task) {
        return;
    }

    std::unique_lock<std::mutex> lock(m_mutex);

    // 更新任务计数
    if (task->getState() == TaskState::FINISHED) {
        m_completed_tasks.fetch_add(1);
    } else if (task->getState() == TaskState::FAILED) {
        m_failed_tasks.fetch_add(1);

        // 如果有任务失败，可以选择停止整个DAG的执行
        // 这里我们选择继续执行其他不依赖于失败任务的任务
        SYLAR_LOG_WARN(g_logger) << "Task " << task->getName()
                                 << " failed: " << task->getResult()->error_msg;
    }

    // 执行所有依赖于当前任务的后置任务
    for (const auto &successor : task->getSuccessors()) {
        if (successor->areAllPredecessorsCompleted()) {
            executeTask(successor);
        }
    }

    // 检查DAG是否执行完成
    checkCompletion();
}

void DAGExecutor::checkCompletion()
{
    if (!m_dag || isCompleted() || isFailed() || isStopped()) {
        return;
    }

    // 检查是否所有任务都已完成或失败
    if (m_completed_tasks.load() + m_failed_tasks.load() == m_dag->getTaskCount()) {
        if (m_failed_tasks.load() > 0) {
            setState(State::FAILED);
        } else {
            setState(State::COMPLETED);
        }

        // 通知所有等待的线程
        m_cv.notify_all();
    }
}

void DAGExecutor::reset()
{
    m_completed_tasks.store(0);
    m_failed_tasks.store(0);
    setState(State::IDLE);

    // 重置所有任务的状态
    if (m_dag) {
        for (const auto &task : m_dag->getTasks()) {
            task->setState(TaskState::READY);
        }
    }
}

void DAGExecutor::setState(State state)
{
    m_state.store(state);
}

} // namespace sylar::dag
