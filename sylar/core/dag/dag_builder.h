#ifndef __SYLAR_DAG_DAG_BUILDER_H__
#define __SYLAR_DAG_DAG_BUILDER_H__

#include "dag.h"
#include "task.h"

namespace sylar::dag
{
/**
 * @brief DAG构建器，提供声明式API来定义任务依赖关系并构建DAG
 */
class DAGBuilder
{
public:
    using ptr = std::shared_ptr<DAGBuilder>;

    /**
     * @brief 构造函数
     * @param name DAG名称
     */
    explicit DAGBuilder(const std::string &name = "");

    /**
     * @brief 析构函数
     */
    virtual ~DAGBuilder() = default;

    /**
     * @brief 添加函数任务
     * @param name 任务名称
     * @param func 任务函数
     * @return 构建器自身，用于链式调用
     */
    DAGBuilder &addTask(const std::string &name, const FunctionTask::FunctionType &func);

    /**
     * @brief 添加自定义任务
     * @param task 任务对象
     * @return 构建器自身，用于链式调用
     */
    DAGBuilder &addTask(const TaskPtr &task);

    /**
     * @brief 建立任务依赖关系
     * @param predecessor_name 前置任务名称
     * @param successor_name 后置任务名称
     * @return 构建器自身，用于链式调用
     */
    DAGBuilder &dependOn(const std::string &predecessor_name, const std::string &successor_name);

    /**
     * @brief 构建并返回DAG
     * @return 构建好的DAG对象
     */
    DAG::ptr build();

    /**
     * @brief 重置构建器
     * @return 构建器自身，用于链式调用
     */
    DAGBuilder &reset();

    /**
     * @brief 检查当前构建的DAG是否有环
     * @return 是否有环
     */
    bool hasCycle() const;

    /**
     * @brief 获取当前构建的DAG中的任务数量
     * @return 任务数量
     */
    size_t getTaskCount() const;

private:
    DAG::ptr m_dag; // 正在构建的DAG
};

} // namespace sylar::dag

#endif // __SYLAR_DAG_DAG_BUILDER_H__