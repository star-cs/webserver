#ifndef __SYLAR_DAG_DAG_H__
#define __SYLAR_DAG_DAG_H__

#include "task.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace sylar
{
namespace dag
{

    class DAG
    {
    public:
        using ptr = std::shared_ptr<DAG>;

        /**
         * @brief 构造函数
         * @param name DAG名称
         */
        explicit DAG(const std::string &name = "");

        /**
         * @brief 析构函数
         */
        virtual ~DAG() = default;

        /**
         * @brief 获取DAG名称
         */
        const std::string &getName() const { return m_name; }

        /**
         * @brief 添加任务到DAG
         * @param task 任务对象
         * @return 是否添加成功
         */
        bool addTask(const TaskPtr &task);

        /**
         * @brief 从DAG中移除任务
         * @param task_name 任务名称
         * @return 是否移除成功
         */
        bool removeTask(const std::string &task_name);

        /**
         * @brief 获取任务
         * @param task_name 任务名称
         * @return 任务对象指针，如果不存在则返回nullptr
         */
        TaskPtr getTask(const std::string &task_name) const;

        /**
         * @brief 获取所有任务
         * @return 任务列表
         */
        const std::vector<TaskPtr> &getTasks() const { return m_tasks; }

        /**
         * @brief 建立任务依赖关系
         * @param predecessor_name 前置任务名称
         * @param successor_name 后置任务名称
         * @return 是否建立成功
         */
        bool addDependency(const std::string &predecessor_name, const std::string &successor_name);

        /**
         * @brief 移除任务依赖关系
         * @param predecessor_name 前置任务名称
         * @param successor_name 后置任务名称
         * @return 是否移除成功
         */
        bool removeDependency(const std::string &predecessor_name,
                              const std::string &successor_name);

        /**
         * @brief 检查DAG是否有环
         * @return 是否有环
         */
        bool hasCycle() const;

        /**
         * @brief 获取入度为0的任务（没有依赖的任务）
         * @return 入度为0的任务列表
         */
        std::vector<TaskPtr> getEntryTasks() const;

        /**
         * @brief 获取出度为0的任务（没有后续依赖的任务）
         * @return 出度为0的任务列表
         */
        std::vector<TaskPtr> getExitTasks() const;

        /**
         * @brief 检查DAG是否为空
         * @return DAG是否为空
         */
        bool isEmpty() const { return m_tasks.empty(); }

        /**
         * @brief 获取任务数量
         * @return 任务数量
         */
        size_t getTaskCount() const { return m_tasks.size(); }

        /**
         * @brief 清空DAG
         */
        void clear();

        /**
         * @brief 拓扑排序
         * @return 拓扑排序后的任务列表
         */
        std::vector<TaskPtr> topologicalSort() const;

    protected:
        /**
         * @brief 检查DAG是否有环（深度优先搜索实现）
         * @param task 当前任务
         * @param visited 访问标记
         * @param recursion_stack 递归栈
         * @return 是否有环
         */
        bool hasCycle(const TaskPtr &task, std::unordered_map<std::string, bool> &visited,
                      std::unordered_map<std::string, bool> &recursion_stack) const;

        /**
         * @brief 拓扑排序辅助函数
         * @param task 当前任务
         * @param visited 访问标记
         * @param result 结果列表
         */
        void topologicalSort(const TaskPtr &task, std::unordered_map<std::string, bool> &visited,
                             std::vector<TaskPtr> &result) const;

    private:
        std::string m_name;                                  // DAG名称
        std::vector<TaskPtr> m_tasks;                        // 任务列表
        std::unordered_map<std::string, TaskPtr> m_task_map; // 任务名称到任务对象的映射
    };

} // namespace dag
} // namespace sylar

#endif // __SYLAR_DAG_DAG_H__