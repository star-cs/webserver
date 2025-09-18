#include "application.h"

#include <unistd.h>
#include <signal.h>

#include "sylar/net/http2/http2_server.h"
#include "sylar/net/tcp_server.h"
#include "sylar/core/daemon.h"
#include "sylar/core/config/config.h"
#include "sylar/core/env.h"
#include "sylar/core/log/log.h"
#include "sylar/core/module.h"
// #include "sylar/net/rock/rock_stream.h"
#include "sylar/core/worker.h"
// #include "sylar/net/http/ws_server.h"
// #include "sylar/net/rock/rock_server.h"
// #include "sylar/net/ns/name_server_module.h"
#include "sylar/core/fox_thread.h"
#include "sylar/io/db/redis.h"

namespace sylar
{

// 系统日志记录器
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 服务器工作路径配置变量
static sylar::ConfigVar<std::string>::ptr g_server_work_path =
    sylar::Config::Lookup("server.work_path", std::string("/apps/work/sylar"), "server work path");

// 服务器PID文件配置变量
static sylar::ConfigVar<std::string>::ptr g_server_pid_file =
    sylar::Config::Lookup("server.pid_file", std::string("sylar.pid"), "server pid file");

// static sylar::ConfigVar<std::string>::ptr g_service_discovery_zk =
//     sylar::Config::Lookup("service_discovery.zk", std::string(""), "service discovery
//     zookeeper");

// 服务器配置变量
static sylar::ConfigVar<std::vector<TcpServerConf> >::ptr g_servers_conf =
    sylar::Config::Lookup("servers", std::vector<TcpServerConf>(), "http server config");

// Application单例实例指针
Application *Application::s_instance = nullptr;

// 构造函数，初始化单例实例
Application::Application()
{
    s_instance = this;
}

// 应用初始化函数
bool Application::init(int argc, char **argv)
{
    // 保存命令行参数
    m_argc = argc;
    m_argv = argv;

    // 添加命令行帮助信息
    sylar::EnvMgr::GetInstance()->addHelp("s", "start with the terminal");
    sylar::EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    sylar::EnvMgr::GetInstance()->addHelp("c", "conf path default: ./conf");
    sylar::EnvMgr::GetInstance()->addHelp("p", "print help");

    bool is_print_help = false;
    // 初始化环境变量管理器
    if (!sylar::EnvMgr::GetInstance()->init(argc, argv)) {
        is_print_help = true;
    }

    // 检查是否需要打印帮助信息
    if (sylar::EnvMgr::GetInstance()->has("p")) {
        is_print_help = true;
    }

    // 获取配置文件路径并加载配置
    std::string conf_path = sylar::EnvMgr::GetInstance()->getConfigPath();
    SYLAR_LOG_INFO(g_logger) << "load conf path:" << conf_path;
    sylar::Config::LoadFromConfDir(conf_path);

    // 初始化模块管理器并获取所有模块
    ModuleMgr::GetInstance()->init();
    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);

    // 在参数解析前通知所有模块
    for (auto i : modules) {
        i->onBeforeArgsParse(argc, argv);
    }

    // 如果需要打印帮助信息，则打印并返回
    if (is_print_help) {
        sylar::EnvMgr::GetInstance()->printHelp();
        return false;
    }

    // 在参数解析后通知所有模块
    for (auto i : modules) {
        i->onAfterArgsParse(argc, argv);
    }
    modules.clear();

    // 确定运行模式：终端模式或守护进程模式
    int run_type = 0;
    if (sylar::EnvMgr::GetInstance()->has("s")) {
        run_type = 1;
    }
    if (sylar::EnvMgr::GetInstance()->has("d")) {
        run_type = 2;
    }

    // 如果没有指定运行模式，则打印帮助信息并返回
    if (run_type == 0) {
        sylar::EnvMgr::GetInstance()->printHelp();
        return false;
    }

    // 检查服务器是否已经在运行
    std::string pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    if (sylar::FSUtil::IsRunningPidfile(pidfile)) {
        SYLAR_LOG_ERROR(g_logger) << "server is running:" << pidfile;
        return false;
    }

    // 创建工作目录
    if (!sylar::FSUtil::Mkdir(g_server_work_path->getValue())) {
        SYLAR_LOG_FATAL(g_logger) << "create work path [" << g_server_work_path->getValue()
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

// 启动应用运行
bool Application::run()
{
    // 检查是否以守护进程模式运行
    bool is_daemon = sylar::EnvMgr::GetInstance()->has("d");
    // 调用守护进程启动函数
    return start_daemon(
        m_argc, m_argv,
        std::bind(&Application::main, this, std::placeholders::_1, std::placeholders::_2),
        is_daemon);
}

// 主函数入口
int Application::main(int argc, char **argv)
{
    // 忽略SIGPIPE信号，防止写入已关闭的socket导致进程退出
    signal(SIGPIPE, SIG_IGN);
    SYLAR_LOG_INFO(g_logger) << "main";

    // 重新加载配置文件
    std::string conf_path = sylar::EnvMgr::GetInstance()->getConfigPath();
    sylar::Config::LoadFromConfDir(conf_path, true);

    // 创建并写入PID文件
    {
        std::string pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
        std::ofstream ofs(pidfile);
        if (!ofs) {
            SYLAR_LOG_ERROR(g_logger) << "open pidfile " << pidfile << " failed";
            return false;
        }
        ofs << getpid();
    }

    // 创建主IO管理器
    m_mainIOManager.reset(new sylar::IOManager(1, true, "main"));
    // 调度运行fiber
    m_mainIOManager->schedule(std::bind(&Application::run_fiber, this));
    // 添加定时器（目前为空操作）
    m_mainIOManager->addTimer(
        2000,
        []() {
            // SYLAR_LOG_INFO(g_logger) << "hello";
        },
        true);
    // 停止主IO管理器
    m_mainIOManager->stop();
    return 0;
}

// 运行fiber函数，处理服务器启动逻辑
int Application::run_fiber()
{
    // 获取所有已注册的模块
    std::vector<Module::ptr> modules;
    ModuleMgr::GetInstance()->listAll(modules);

    // 检查所有模块是否能成功加载
    bool has_error = false;
    for (auto &i : modules) {
        if (!i->onLoad()) {
            SYLAR_LOG_ERROR(g_logger)
                << "module name=" << i->getName() << " version=" << i->getVersion()
                << " filename=" << i->getFilename();
            has_error = true;
        }
    }

    // 如果有任何模块加载失败，则退出程序
    if (has_error) {
        _exit(0);
    }

    // 初始化工作线程管理器、Fox线程管理器和Redis管理器
    sylar::WorkerMgr::GetInstance()->init();
    FoxThreadMgr::GetInstance()->init();
    FoxThreadMgr::GetInstance()->start();
    RedisMgr::GetInstance();

    // 获取服务器配置并创建相应的服务器实例
    auto http_confs = g_servers_conf->getValue();
    std::vector<TcpServer::ptr> svrs;
    for (auto &i : http_confs) {
        SYLAR_LOG_DEBUG(g_logger) << std::endl << LexicalCast<TcpServerConf, std::string>()(i);

        // 解析地址配置
        std::vector<Address::ptr> address;
        for (auto &a : i.address) {
            size_t pos = a.find(":");
            if (pos == std::string::npos) {
                // 如果没有端口号，则认为是Unix域套接字地址
                // SYLAR_LOG_ERROR(g_logger) << "invalid address: " << a;
                address.push_back(UnixAddress::ptr(new UnixAddress(a)));
                continue;
            }
            int32_t port = atoi(a.substr(pos + 1).c_str());
            // 解析IP地址和端口号
            auto addr = sylar::IPAddress::Create(a.substr(0, pos).c_str(), port);
            if (addr) {
                address.push_back(addr);
                continue;
            }

            // 尝试通过网络接口名称获取地址
            std::vector<std::pair<Address::ptr, uint32_t> > result;
            if (sylar::Address::GetInterfaceAddresses(result, a.substr(0, pos))) {
                for (auto &x : result) {
                    auto ipaddr = std::dynamic_pointer_cast<IPAddress>(x.first);
                    if (ipaddr) {
                        ipaddr->setPort(atoi(a.substr(pos + 1).c_str()));
                    }
                    address.push_back(ipaddr);
                }
                continue;
            }

            // 最后尝试通过域名解析获取地址
            auto aaddr = sylar::Address::LookupAny(a);
            if (aaddr) {
                address.push_back(aaddr);
                continue;
            }

            // 如果所有方式都失败，则记录错误并退出
            SYLAR_LOG_ERROR(g_logger) << "invalid address: " << a;
            _exit(0);
        }

        // 获取工作线程配置
        IOManager *accept_worker = sylar::IOManager::GetThis();
        IOManager *io_worker = sylar::IOManager::GetThis();
        IOManager *process_worker = sylar::IOManager::GetThis();
        if (!i.accept_worker.empty()) {
            accept_worker = sylar::WorkerMgr::GetInstance()->getAsIOManager(i.accept_worker).get();
            if (!accept_worker) {
                SYLAR_LOG_ERROR(g_logger) << "accept_worker: " << i.accept_worker << " not exists";
                _exit(0);
            }
        }
        if (!i.io_worker.empty()) {
            io_worker = sylar::WorkerMgr::GetInstance()->getAsIOManager(i.io_worker).get();
            if (!io_worker) {
                SYLAR_LOG_ERROR(g_logger) << "io_worker: " << i.io_worker << " not exists";
                _exit(0);
            }
        }
        if (!i.process_worker.empty()) {
            process_worker =
                sylar::WorkerMgr::GetInstance()->getAsIOManager(i.process_worker).get();
            if (!process_worker) {
                SYLAR_LOG_ERROR(g_logger)
                    << "process_worker: " << i.process_worker << " not exists";
                _exit(0);
            }
        }

        // 根据配置创建不同类型的服务器
        TcpServer::ptr server;
        if (i.type == "http") {
            // 创建HTTP服务器
            server.reset(
                new sylar::http::HttpServer(i.keepalive, process_worker, io_worker, accept_worker));
        } else if (i.type == "http2") {
            server = std::make_shared<sylar::http2::Http2Server>(process_worker, io_worker,
                                                                 accept_worker);
        } else if (i.type == "ws") {
            // server.reset(new sylar::http::WSServer(process_worker, io_worker, accept_worker));
        } else if (i.type == "rock") {
            // server.reset(new sylar::RockServer("rock", process_worker, io_worker,
            // accept_worker));
        } else if (i.type == "nameserver") {
            // server.reset(
            //     new sylar::RockServer("nameserver", process_worker, io_worker, accept_worker));
            // ModuleMgr::GetInstance()->add(std::make_shared<sylar::ns::NameServerModule>());
        } else {
            SYLAR_LOG_ERROR(g_logger)
                << "invalid server type=" << i.type << LexicalCast<TcpServerConf, std::string>()(i);
            _exit(0);
        }

        // 设置服务器名称
        if (!i.name.empty()) {
            server->setName(i.name);
        }

        // 绑定地址，如果失败则退出
        std::vector<Address::ptr> fails;
        if (!server->bind(address, fails, i.ssl)) {
            for (auto &x : fails) {
                SYLAR_LOG_ERROR(g_logger) << "bind address fail:" << *x;
            }
            _exit(0);
        }

        // 如果是SSL服务器，加载证书文件
        if (i.ssl) {
            if (!server->loadCertificates(i.cert_file, i.key_file)) {
                SYLAR_LOG_ERROR(g_logger) << "loadCertificates fail, cert_file=" << i.cert_file
                                          << " key_file=" << i.key_file;
            }
        }

        // 设置服务器配置并保存服务器实例
        server->setConf(i);
        m_servers[i.type].push_back(server);
        svrs.push_back(server);
    }

    // 通知所有模块服务器已准备就绪
    for (auto &i : modules) {
        i->onServerReady();
    }

    // 启动所有服务器
    for (auto &i : svrs) {
        i->start();
    }

    // 通知所有模块服务器已启动
    for (auto &i : modules) {
        i->onServerUp();
    }

    return 0;
}

// 根据类型获取服务器实例
bool Application::getServer(const std::string &type, std::vector<TcpServer::ptr> &svrs)
{
    auto it = m_servers.find(type);
    if (it == m_servers.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

// 获取所有服务器实例
void Application::listAllServer(std::map<std::string, std::vector<TcpServer::ptr> > &servers)
{
    servers = m_servers;
}

} // namespace sylar