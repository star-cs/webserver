#include "library.h"

#include <dlfcn.h>
#include "sylar/core/config/config.h"
#include "sylar/core/env.h"
#include "sylar/core/log/log.h"

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

typedef Module *(*create_module)();
typedef void (*destory_module)(Module *);

class ModuleCloser
{
public:
    ModuleCloser(void *handle, destory_module d) : m_handle(handle), m_destory(d) {}

    void operator()(Module *module)
    {
        std::string name = module->getName();
        std::string version = module->getVersion();
        std::string path = module->getFilename();
        m_destory(module);
        int rt = dlclose(m_handle);
        if (rt) {
            SYLAR_LOG_ERROR(g_logger)
                << "dlclose handle fail handle=" << m_handle << " name=" << name
                << " version=" << version << " path=" << path << " error=" << dlerror();
        } else {
            SYLAR_LOG_INFO(g_logger) << "destory module=" << name << " version=" << version
                                     << " path=" << path << " handle=" << m_handle << " success";
        }
    }

private:
    void *m_handle;
    destory_module m_destory;
};

/**
 * @brief 加载指定路径的动态库模块
 * @param[in] path 动态库文件的路径
 * @return 返回加载的模块智能指针，加载失败返回nullptr
 * 
 * 该函数通过dlopen加载指定路径的动态库，并获取其中的CreateModule和DestoryModule符号，
 * 用于创建和销毁模块实例。加载成功后还会加载配置文件。
 */
Module::ptr Library::GetModule(const std::string &path)
{
    // 加载动态库
    void *handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        SYLAR_LOG_ERROR(g_logger) << "cannot load library path=" << path << " error=" << dlerror();
        return nullptr;
    }

    // 获取模块创建函数符号
    create_module create = (create_module)dlsym(handle, "CreateModule");
    if (!create) {
        SYLAR_LOG_ERROR(g_logger) << "cannot load symbol CreateModule in " << path
                                  << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }

    // 获取模块销毁函数符号
    destory_module destory = (destory_module)dlsym(handle, "DestoryModule");
    if (!destory) {
        SYLAR_LOG_ERROR(g_logger) << "cannot load symbol DestoryModule in " << path
                                  << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }

    // 创建模块实例并设置相关信息
    // 智能指针 构造函数接收 两个参数: 1. 原始指针 2. 自定义删除器
    Module::ptr module(create(), ModuleCloser(handle, destory));
    module->setFilename(path);
    SYLAR_LOG_INFO(g_logger) << "load module name=" << module->getName()
                             << " version=" << module->getVersion()
                             << " path=" << module->getFilename() << " success";
    Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath(), true);
    return module;
}

} // namespace sylar
