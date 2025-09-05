/**
 * 命令行参数解析应该用getopt系列接口实现，以支持选项合并和--开头的长选项
 */
#include "env.h"
#include "sylar/core/log/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "sylar/core/config/config.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * @brief 初始化环境变量
 * 
 * 通过解析程序的命令行参数来初始化环境变量对象。该函数首先确定程序的执行路径，
 * 然后解析命令行参数，将它们存储为环境变量的键值对形式。
 * 
 * @param argc 命令行参数的数量
 * @param argv 命令行参数的数组
 * @return true 如果初始化成功
 * @return false 如果解析参数过程中遇到无效参数
 */
bool Env::init(int argc, char **argv) {
    // 用于存储路径的字符数组
    char link[1024] = {0};
    char path[1024] = {0};
    // 构造/proc/self/exe的路径，用于获取程序的执行路径
    sprintf(link, "/proc/%d/exe", getpid());
    // 读取符号链接的实际路径
    auto len = readlink(link, path, sizeof(path));
    if (len == -1) {
        // 处理错误，例如打印错误信息
        perror("readlink failed");
        // 根据需要进行错误处理，如返回或退出
        return -1;
    }
    // /path/xxx/exe
    m_exe = path;

    // 找到最后一个斜杠的位置，用于获取程序的工作目录
    auto pos = m_exe.find_last_of("/");
    m_cwd    = m_exe.substr(0, pos) + "/";

    // 程序名
    m_program = argv[0];
    // -config /path/to/config -file xxxx -d
    const char *now_key = nullptr;
    // 遍历命令行参数，解析键值对
    for (int i = 1; i < argc; ++i) {
        // 遇到以'-'开头的参数，表示这是一个键
        if (argv[i][0] == '-') {
            if (strlen(argv[i]) > 1) {
                if (now_key) {
                    // 如果当前有键，添加到环境变量中
                    add(now_key, "");
                }
                // 更新当前键
                now_key = argv[i] + 1;
            } else {
                // 如果键无效，记录错误日志并返回false
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        } else {
            // 遇到不以'-'开头的参数，表示这是一个值
            if (now_key) {
                // 如果当前有键，将键和值添加到环境变量中，并重置当前键
                add(now_key, argv[i]);
                now_key = nullptr;
            } else {
                // 如果值无效，记录错误日志并返回false
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                                          << " val=" << argv[i];
                return false;
            }
        }
    }
    // 如果最后还有键未添加，添加到环境变量中
    if (now_key) {
        add(now_key, "");
    }
    // 初始化成功
    return true;
}

void Env::add(const std::string &key, const std::string &val) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

bool Env::has(const std::string &key) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Env::del(const std::string &key) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

std::string Env::get(const std::string &key, const std::string &default_value) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

void Env::addHelp(const std::string &key, const std::string &desc) {
    removeHelp(key);
    RWMutexType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

void Env::removeHelp(const std::string &key) {
    RWMutexType::WriteLock lock(m_mutex);
    for (auto it = m_helps.begin();
         it != m_helps.end();) {
        if (it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

void Env::printHelp() {
    RWMutexType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for (auto &i : m_helps) {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

bool Env::setEnv(const std::string &key, const std::string &val) {
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Env::getEnv(const std::string &key, const std::string &default_value) {
    const char *v = getenv(key.c_str());
    if (v == nullptr) {
        return default_value;
    }
    return v;
}

std::string Env::getAbsolutePath(const std::string &path) const {
    if (path.empty()) {
        return "/";
    }
    if (path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

std::string Env::getAbsoluteWorkPath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    static sylar::ConfigVar<std::string>::ptr g_server_work_path = sylar::Config::Lookup<std::string>("server.work_path", std::string("."), "server work path");
    return g_server_work_path->getValue() + "/" + path;
}

std::string Env::getConfigPath() {
    return getAbsolutePath(get("c", "conf"));
}

} // namespace sylar