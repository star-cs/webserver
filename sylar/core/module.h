#ifndef __SYLAR_MODULE_H__
#define __SYLAR_MODULE_H__

#include "sylar/net/stream.h"
#include "sylar/core/common/singleton.h"
#include "sylar/core/mutex.h"
// #include "sylar/net/rock/rock_stream.h"
#include <unordered_map>
#include "sylar/net/protocol.h"

namespace sylar
{
/**
 * @brief 模块基类，用于动态加载和管理模块
 * 
 * 模块需要实现以下C接口用于创建和销毁：
 * extern "C" {
 * Module* CreateModule() {
 *  return XX;
 * }
 * void DestoryModule(Module* ptr) {
 *  delete ptr;
 * }
 * }
 */
class Module
{
public:
    /**
     * @brief 模块类型枚举
     */
    enum Type {
        MODULE = 0,  /// 普通模块
        ROCK = 1,    /// Rock协议模块
    };
    
    typedef std::shared_ptr<Module> ptr;
    
    /**
     * @brief 构造函数
     * @param name 模块名称
     * @param version 模块版本
     * @param filename 模块文件名
     * @param type 模块类型
     */
    Module(const std::string &name, const std::string &version, const std::string &filename,
           uint32_t type = MODULE);
    virtual ~Module() {}

    /**
     * @brief 命令行参数解析前的处理
     * @param argc 参数个数
     * @param argv 参数数组
     * 
     * 此函数在命令行参数解析之前被调用，可用于预处理一些初始化工作
     */
    virtual void onBeforeArgsParse(int argc, char **argv);
    
    /**
     * @brief 命令行参数解析后的处理
     * @param argc 参数个数
     * @param argv 参数数组
     * 
     * 此函数在命令行参数解析之后被调用，可用于根据解析结果进行后续处理
     */
    virtual void onAfterArgsParse(int argc, char **argv);

    /**
     * @brief 模块加载处理
     * @return 加载成功返回true，否则返回false
     * 
     * 当模块被加载时调用此函数，用于执行模块的初始化逻辑
     */
    virtual bool onLoad();
    
    /**
     * @brief 模块卸载处理
     * @return 卸载成功返回true，否则返回false
     * 
     * 当模块被卸载时调用此函数，用于执行模块的清理逻辑
     */
    virtual bool onUnload();

    /**
     * @brief 连接建立时的处理
     * @param stream 连接流
     * @return 处理成功返回true，否则返回false
     * 
     * 当有新的网络连接建立时调用此函数，模块可在此处理连接相关的初始化
     */
    virtual bool onConnect(sylar::Stream::ptr stream);
    
    /**
     * @brief 连接断开时的处理
     * @param stream 连接流
     * @return 处理成功返回true，否则返回false
     * 
     * 当网络连接断开时调用此函数，模块可在此处理连接相关的清理工作
     */
    virtual bool onDisconnect(sylar::Stream::ptr stream);

    /**
     * @brief 服务器准备就绪时的处理
     * @return 处理成功返回true，否则返回false
     * 
     * 当服务器准备就绪但还未启动时调用此函数，模块可在此进行最后的准备工作
     */
    virtual bool onServerReady();
    
    /**
     * @brief 服务器启动完成时的处理
     * @return 处理成功返回true，否则返回false
     * 
     * 当服务器完全启动并开始运行时调用此函数，模块可在此执行启动后的操作
     */
    virtual bool onServerUp();

    /**
     * @brief 处理请求消息
     * @param req 请求消息
     * @param rsp 响应消息
     * @param stream 连接流
     * @return 处理成功返回true，否则返回false
     * 
     * 处理来自客户端的请求消息，并生成相应的响应消息
     */
    virtual bool handleRequest(sylar::Message::ptr req, sylar::Message::ptr rsp,
                               sylar::Stream::ptr stream);
    
    /**
     * @brief 处理通知消息
     * @param notify 通知消息
     * @param stream 连接流
     * @return 处理成功返回true，否则返回false
     * 
     * 处理来自客户端的通知消息，通知消息不需要响应
     */
    virtual bool handleNotify(sylar::Message::ptr notify, sylar::Stream::ptr stream);

    /**
     * @brief 获取模块状态字符串
     * @return 状态字符串
     * 
     * 返回模块的当前状态信息，用于调试和监控
     */
    virtual std::string statusString();

    /// 获取模块名称
    const std::string &getName() const { return m_name; }
    
    /// 获取模块版本
    const std::string &getVersion() const { return m_version; }
    
    /// 获取模块文件名
    const std::string &getFilename() const { return m_filename; }
    
    /// 获取模块ID（名称+版本）
    const std::string &getId() const { return m_id; }

    /// 设置模块文件名
    void setFilename(const std::string &v) { m_filename = v; }

    /// 获取模块类型
    uint32_t getType() const { return m_type; }

    /**
     * @brief 注册服务
     * @param server_type 服务器类型
     * @param domain 域名
     * @param service 服务名
     * 
     * 将模块提供的服务注册到服务发现系统中
     */
    void registerService(const std::string &server_type, const std::string &domain,
                         const std::string &service);

protected:
    std::string m_name;      /// 模块名称
    std::string m_version;   /// 模块版本
    std::string m_filename;  /// 模块文件名
    std::string m_id;        /// 模块ID（名称+版本）
    uint32_t m_type;         /// 模块类型
};

// class RockModule : public Module
// {
// public:
//     typedef std::shared_ptr<RockModule> ptr;
//     RockModule(const std::string &name, const std::string &version, const std::string &filename);

//     virtual bool handleRockRequest(sylar::RockRequest::ptr request,
//                                    sylar::RockResponse::ptr response,
//                                    sylar::RockStream::ptr stream) = 0;
//     virtual bool handleRockNotify(sylar::RockNotify::ptr notify, sylar::RockStream::ptr stream) = 0;

//     virtual bool handleRequest(sylar::Message::ptr req, sylar::Message::ptr rsp,
//                                sylar::Stream::ptr stream);
//     virtual bool handleNotify(sylar::Message::ptr notify, sylar::Stream::ptr stream);
// };

/**
 * @brief 模块管理器，负责模块的加载、卸载和管理
 * 
 * 模块管理器是单例模式，提供对所有模块的统一管理，包括模块的生命周期管理、
 * 按类型查找模块、遍历模块等功能
 */
class ModuleManager
{
public:
    typedef RWMutex RWMutexType;

    /**
     * @brief 构造函数
     */
    ModuleManager();

    /**
     * @brief 添加模块
     * @param m 模块指针
     * 
     * 将模块添加到管理器中，如果已存在同名模块则先删除旧模块
     */
    void add(Module::ptr m);
    
    /**
     * @brief 删除指定名称的模块
     * @param name 模块名称
     * 
     * 从管理器中删除指定模块，并调用模块的onUnload方法
     */
    void del(const std::string &name);
    
    /**
     * @brief 删除所有模块
     * 
     * 删除管理器中的所有模块，并调用各模块的onUnload方法
     */
    void delAll();

    /**
     * @brief 初始化模块管理器
     * 
     * 扫描模块路径下的所有.so文件并加载模块
     */
    void init();

    /**
     * @brief 根据名称获取模块
     * @param name 模块名称
     * @return 模块指针
     * 
     * 根据模块ID（name/version）查找模块，找不到返回nullptr
     */
    Module::ptr get(const std::string &name);

    /**
     * @brief 处理连接事件
     * @param stream 连接流
     * 
     * 当有新的连接建立时，通知所有模块
     */
    void onConnect(Stream::ptr stream);
    
    /**
     * @brief 处理断开连接事件
     * @param stream 连接流
     * 
     * 当连接断开时，通知所有模块
     */
    void onDisconnect(Stream::ptr stream);

    /**
     * @brief 获取所有模块列表
     * @param ms 模块列表
     * 
     * 将管理器中的所有模块添加到提供的向量中
     */
    void listAll(std::vector<Module::ptr> &ms);
    
    /**
     * @brief 根据类型获取模块列表
     * @param type 模块类型
     * @param ms 模块列表
     * 
     * 将指定类型的模块添加到提供的向量中
     */
    void listByType(uint32_t type, std::vector<Module::ptr> &ms);
    
    /**
     * @brief 遍历指定类型的模块
     * @param type 模块类型
     * @param cb 回调函数
     * 
     * 对指定类型的所有模块执行回调函数
     */
    void foreach (uint32_t type, std::function<void(Module::ptr)> cb);

private:
    /**
     * @brief 初始化指定路径的模块
     * @param path 模块路径
     * 
     * 加载指定路径的模块文件并添加到管理器中
     */
    void initModule(const std::string &path);

private:
    RWMutexType m_mutex;  /// 读写锁，保护模块容器的并发访问
    std::unordered_map<std::string, Module::ptr> m_modules;  /// 模块映射(name -> module)
    std::unordered_map<uint32_t, std::unordered_map<std::string, Module::ptr> > m_type2Modules;  /// 按类型分类的模块映射
};

/// 模块管理器单例
typedef sylar::Singleton<ModuleManager> ModuleMgr;

} // namespace sylar

#endif