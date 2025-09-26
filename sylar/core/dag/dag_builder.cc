#include "dag_builder.h"
#include "sylar/core/common/macro.h"

namespace sylar::dag
{

DAGBuilder::DAGBuilder(const std::string &name)
{
    m_dag.reset(new DAG(name));
}

DAGBuilder &DAGBuilder::addTask(const std::string &name, const FunctionTask::FunctionType &func)
{
    if (name.empty() || !func) {
        return *this;
    }

    // 检查任务是否已存在
    if (m_dag->getTask(name)) {
        return *this;
    }

    // 创建并添加任务
    auto task = std::make_shared<FunctionTask>(name, func);
    m_dag->addTask(task);

    return *this;
}

DAGBuilder &DAGBuilder::addTask(const TaskPtr &task)
{
    if (!task) {
        return *this;
    }

    // 添加任务到DAG
    m_dag->addTask(task);

    return *this;
}

DAGBuilder &DAGBuilder::dependOn(const std::string &predecessor_name,
                                 const std::string &successor_name)
{
    if (predecessor_name.empty() || successor_name.empty()) {
        return *this;
    }

    // 建立依赖关系
    m_dag->addDependency(predecessor_name, successor_name);

    return *this;
}

DAG::ptr DAGBuilder::build()
{
    // 检查DAG是否有环
    if (m_dag->hasCycle()) {
        return nullptr;
    }

    // 返回构建好的DAG
    return m_dag;
}

DAGBuilder &DAGBuilder::reset()
{
    // 重置DAG
    m_dag.reset(new DAG(m_dag->getName()));

    return *this;
}

bool DAGBuilder::hasCycle() const
{
    return m_dag->hasCycle();
}

size_t DAGBuilder::getTaskCount() const
{
    return m_dag->getTaskCount();
}

} // namespace sylar::dag
