#include "module.h"
#include "sylar/core/config/config.h"
#include "sylar/core/env.h"
#include "sylar/core/library.h"
#include "sylar/core/util/util.h"
#include "sylar/core/log/log.h"
#include "sylar/core/application.h"

namespace sylar
{

// 模块路径配置变量
static sylar::ConfigVar<std::string>::ptr g_module_path =
    Config::Lookup("module.path", std::string("module"), "module path");

// 系统日志记录器
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * @brief 构造函数
 * @param name 模块名称
 * @param version 模块版本
 * @param filename 模块文件名
 * @param type 模块类型
 */
Module::Module(const std::string &name, const std::string &version, const std::string &filename,
               uint32_t type)
    : m_name(name), m_version(version), m_filename(filename), m_id(name + "/" + version),
      m_type(type)
{
}

/**
 * @brief 在命令行参数解析前的回调函数
 * @param argc 参数个数
 * @param argv 参数数组
 */
void Module::onBeforeArgsParse(int argc, char **argv)
{
}

/**
 * @brief 在命令行参数解析后的回调函数
 * @param argc 参数个数
 * @param argv 参数数组
 */
void Module::onAfterArgsParse(int argc, char **argv)
{
}

/**
 * @brief 处理请求消息
 * @param req 请求消息
 * @param rsp 响应消息
 * @param stream 数据流
 * @return 处理结果
 */
bool Module::handleRequest(sylar::Message::ptr req, sylar::Message::ptr rsp,
                           sylar::Stream::ptr stream)
{
    SYLAR_LOG_DEBUG(g_logger) << "handleRequest req=" << req->toString()
                              << " rsp=" << rsp->toString() << " stream=" << stream;
    return true;
}

/**
 * @brief 处理通知消息
 * @param notify 通知消息
 * @param stream 数据流
 * @return 处理结果
 */
bool Module::handleNotify(sylar::Message::ptr notify, sylar::Stream::ptr stream)
{
    SYLAR_LOG_DEBUG(g_logger) << "handleNotify nty=" << notify->toString() << " stream=" << stream;
    return true;
}

/**
 * @brief 模块加载时的回调函数
 * @return 加载结果
 */
bool Module::onLoad()
{
    return true;
}

/**
 * @brief 模块卸载时的回调函数
 * @return 卸载结果
 */
bool Module::onUnload()
{
    return true;
}

/**
 * @brief 连接建立时的回调函数
 * @param stream 数据流
 * @return 处理结果
 */
bool Module::onConnect(sylar::Stream::ptr stream)
{
    return true;
}

/**
 * @brief 连接断开时的回调函数
 * @param stream 数据流
 * @return 处理结果
 */
bool Module::onDisconnect(sylar::Stream::ptr stream)
{
    return true;
}

/**
 * @brief 服务器准备就绪时的回调函数
 * @return 处理结果
 */
bool Module::onServerReady()
{
    return true;
}

/**
 * @brief 服务器启动时的回调函数
 * @return 处理结果
 */
bool Module::onServerUp()
{
    return true;
}

/**
 * @brief 获取模块状态字符串
 * @return 状态信息字符串
 */
std::string Module::statusString()
{
    std::stringstream ss;
    ss << "Module name=" << getName() << " version=" << getVersion()
       << " filename=" << getFilename() << std::endl;
    return ss.str();
}

/**
 * @brief 模块管理器构造函数
 */
ModuleManager::ModuleManager()
{
}

/**
 * @brief 根据名称获取模块
 * @param name 模块名称
 * @return 模块指针
 */
Module::ptr ModuleManager::get(const std::string &name)
{
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_modules.find(name);
    return it == m_modules.end() ? nullptr : it->second;
}

/**
 * @brief 添加模块
 * @param m 模块指针
 */
void ModuleManager::add(Module::ptr m)
{
    del(m->getId());
    RWMutexType::WriteLock lock(m_mutex);
    m_modules[m->getId()] = m;
    m_type2Modules[m->getType()][m->getId()] = m;
}

/**
 * @brief 删除模块
 * @param name 模块ID
 */
void ModuleManager::del(const std::string &name)
{
    Module::ptr module;
    RWMutexType::WriteLock lock(m_mutex);
    auto it = m_modules.find(name);
    if (it == m_modules.end()) {
        return;
    }
    module = it->second;
    m_modules.erase(it);
    m_type2Modules[module->getType()].erase(module->getId());
    if (m_type2Modules[module->getType()].empty()) {
        m_type2Modules.erase(module->getType());
    }
    lock.unlock();
    module->onUnload();
}

/**
 * @brief 删除所有模块
 */
void ModuleManager::delAll()
{
    RWMutexType::ReadLock lock(m_mutex);
    auto tmp = m_modules;
    lock.unlock();

    for (auto &i : tmp) {
        del(i.first);
    }
}

/**
 * @brief 初始化模块管理器
 */
void ModuleManager::init()
{
    auto path = EnvMgr::GetInstance()->getAbsolutePath(g_module_path->getValue());

    std::vector<std::string> files;
    sylar::FSUtil::ListAllFile(files, path, ".so");

    std::sort(files.begin(), files.end());
    for (auto &i : files) {
        initModule(i);
    }
}

/**
 * @brief 根据类型列出模块
 * @param type 模块类型
 * @param ms 模块指针向量
 */
void ModuleManager::listByType(uint32_t type, std::vector<Module::ptr> &ms)
{
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_type2Modules.find(type);
    if (it == m_type2Modules.end()) {
        return;
    }
    for (auto &i : it->second) {
        ms.push_back(i.second);
    }
}

/**
 * @brief 遍历指定类型的模块
 * @param type 模块类型
 * @param cb 回调函数
 */
void ModuleManager::foreach (uint32_t type, std::function<void(Module::ptr)> cb)
{
    std::vector<Module::ptr> ms;
    listByType(type, ms);
    for (auto &i : ms) {
        cb(i);
    }
}

/**
 * @brief 连接建立时的回调函数
 * @param stream 数据流
 */
void ModuleManager::onConnect(Stream::ptr stream)
{
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto &m : ms) {
        m->onConnect(stream);
    }
}

/**
 * @brief 连接断开时的回调函数
 * @param stream 数据流
 */
void ModuleManager::onDisconnect(Stream::ptr stream)
{
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto &m : ms) {
        m->onDisconnect(stream);
    }
}

/**
 * @brief 列出所有模块
 * @param ms 模块指针向量
 */
void ModuleManager::listAll(std::vector<Module::ptr> &ms)
{
    RWMutexType::ReadLock lock(m_mutex);
    for (auto &i : m_modules) {
        ms.push_back(i.second);
    }
}

/**
 * @brief 初始化指定路径的模块
 * @param path 模块文件路径
 */
void ModuleManager::initModule(const std::string &path)
{
    Module::ptr m = Library::GetModule(path);
    if (m) {
        add(m);
    }
}

} // namespace sylar