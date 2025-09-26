#include "dag.h"
#include "sylar/core/common/macro.h"

namespace sylar
{
namespace dag
{

    DAG::DAG(const std::string &name) : m_name(name)
    {
    }

    bool DAG::addTask(const TaskPtr &task)
    {
        if (!task) {
            return false;
        }

        // 检查任务是否已存在
        auto it = m_task_map.find(task->getName());
        if (it != m_task_map.end()) {
            return false;
        }

        // 添加任务
        m_task_map[task->getName()] = task;
        m_tasks.push_back(task);
        return true;
    }

    bool DAG::removeTask(const std::string &task_name)
    {
        auto it = m_task_map.find(task_name);
        if (it == m_task_map.end()) {
            return false;
        }

        // 移除任务的依赖关系
        TaskPtr task = it->second;

        // 移除所有前置任务对当前任务的依赖
        for (const auto &predecessor : task->getPredecessors()) {
            predecessor->m_successors.erase(std::remove(predecessor->m_successors.begin(),
                                                        predecessor->m_successors.end(), task),
                                            predecessor->m_successors.end());
        }

        // 移除所有后置任务对当前任务的依赖
        for (const auto &successor : task->getSuccessors()) {
            successor->m_predecessors.erase(std::remove(successor->m_predecessors.begin(),
                                                        successor->m_predecessors.end(), task),
                                            successor->m_predecessors.end());
        }

        // 从任务列表和映射中移除
        m_task_map.erase(it);
        m_tasks.erase(std::remove(m_tasks.begin(), m_tasks.end(), task), m_tasks.end());

        return true;
    }

    TaskPtr DAG::getTask(const std::string &task_name) const
    {
        auto it = m_task_map.find(task_name);
        if (it != m_task_map.end()) {
            return it->second;
        }
        return nullptr;
    }

    bool DAG::addDependency(const std::string &predecessor_name, const std::string &successor_name)
    {
        // 检查两个任务是否存在
        auto predecessor_it = m_task_map.find(predecessor_name);
        auto successor_it = m_task_map.find(successor_name);

        if (predecessor_it == m_task_map.end() || successor_it == m_task_map.end()) {
            return false;
        }

        TaskPtr predecessor = predecessor_it->second;
        TaskPtr successor = successor_it->second;

        // 检查是否添加自环
        if (predecessor == successor) {
            return false;
        }

        // 检查依赖是否已经存在
        for (const auto &pred : successor->getPredecessors()) {
            if (pred == predecessor) {
                return false;
            }
        }

        // 建立依赖关系
        successor->addPredecessor(predecessor);

        // 检查添加依赖后是否形成环
        if (hasCycle()) {
            // 移除刚添加的依赖
            successor->m_predecessors.erase(std::remove(successor->m_predecessors.begin(),
                                                        successor->m_predecessors.end(),
                                                        predecessor),
                                            successor->m_predecessors.end());
            predecessor->m_successors.erase(std::remove(predecessor->m_successors.begin(),
                                                        predecessor->m_successors.end(), successor),
                                            predecessor->m_successors.end());
            return false;
        }

        return true;
    }

    bool DAG::removeDependency(const std::string &predecessor_name,
                               const std::string &successor_name)
    {
        // 检查两个任务是否存在
        auto predecessor_it = m_task_map.find(predecessor_name);
        auto successor_it = m_task_map.find(successor_name);

        if (predecessor_it == m_task_map.end() || successor_it == m_task_map.end()) {
            return false;
        }

        TaskPtr predecessor = predecessor_it->second;
        TaskPtr successor = successor_it->second;

        // 移除依赖关系
        auto pred_it = std::remove(successor->m_predecessors.begin(),
                                   successor->m_predecessors.end(), predecessor);

        auto succ_it = std::remove(predecessor->m_successors.begin(),
                                   predecessor->m_successors.end(), successor);

        if (pred_it == successor->m_predecessors.end()
            || succ_it == predecessor->m_successors.end()) {
            return false;
        }

        successor->m_predecessors.erase(pred_it, successor->m_predecessors.end());
        predecessor->m_successors.erase(succ_it, predecessor->m_successors.end());

        return true;
    }

    bool DAG::hasCycle() const
    {
        std::unordered_map<std::string, bool> visited;
        std::unordered_map<std::string, bool> recursion_stack;

        // 初始化访问标记
        for (const auto &task : m_tasks) {
            visited[task->getName()] = false;
            recursion_stack[task->getName()] = false;
        }

        // 对每个未访问的任务进行DFS
        for (const auto &task : m_tasks) {
            if (!visited[task->getName()] && hasCycle(task, visited, recursion_stack)) {
                return true;
            }
        }

        return false;
    }

    bool DAG::hasCycle(const TaskPtr &task, std::unordered_map<std::string, bool> &visited,
                       std::unordered_map<std::string, bool> &recursion_stack) const
    {
        if (!visited[task->getName()]) {
            // 将当前任务标记为已访问并加入递归栈
            visited[task->getName()] = true;
            recursion_stack[task->getName()] = true;

            // 递归访问所有后置任务
            for (const auto &successor : task->getSuccessors()) {
                if (!visited[successor->getName()]
                    && hasCycle(successor, visited, recursion_stack)) {
                    return true;
                } else if (recursion_stack[successor->getName()]) {
                    // 如果在递归栈中发现当前任务，则存在环
                    return true;
                }
            }
        }

        // 回溯时从递归栈中移除当前任务
        recursion_stack[task->getName()] = false;
        return false;
    }

    std::vector<TaskPtr> DAG::getEntryTasks() const
    {
        std::vector<TaskPtr> entry_tasks;

        // 找出所有入度为0的任务
        for (const auto &task : m_tasks) {
            if (task->getPredecessors().empty()) {
                entry_tasks.push_back(task);
            }
        }

        return entry_tasks;
    }

    std::vector<TaskPtr> DAG::getExitTasks() const
    {
        std::vector<TaskPtr> exit_tasks;

        // 找出所有出度为0的任务
        for (const auto &task : m_tasks) {
            if (task->getSuccessors().empty()) {
                exit_tasks.push_back(task);
            }
        }

        return exit_tasks;
    }

    void DAG::clear()
    {
        // 先清除所有依赖关系
        for (const auto &task : m_tasks) {
            task->m_predecessors.clear();
            task->m_successors.clear();
        }

        // 清除任务列表和映射
        m_tasks.clear();
        m_task_map.clear();
    }

    std::vector<TaskPtr> DAG::topologicalSort() const
    {
        std::vector<TaskPtr> result;
        std::unordered_map<std::string, bool> visited;

        // 初始化访问标记
        for (const auto &task : m_tasks) {
            visited[task->getName()] = false;
        }

        // 对每个未访问的任务进行DFS
        for (const auto &task : m_tasks) {
            if (!visited[task->getName()]) {
                topologicalSort(task, visited, result);
            }
        }

        // 反转结果以获得正确的拓扑顺序
        std::reverse(result.begin(), result.end());

        return result;
    }

    void DAG::topologicalSort(const TaskPtr &task, std::unordered_map<std::string, bool> &visited,
                              std::vector<TaskPtr> &result) const
    {
        // 标记当前任务为已访问
        visited[task->getName()] = true;

        // 递归访问所有后置任务
        for (const auto &successor : task->getSuccessors()) {
            if (!visited[successor->getName()]) {
                topologicalSort(successor, visited, result);
            }
        }

        // 将当前任务加入结果列表（后序遍历）
        result.push_back(task);
    }

} // namespace dag
} // namespace sylar