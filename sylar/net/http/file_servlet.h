#ifndef __SYLAR_HTTP_FILE_SERVLET_H__
#define __SYLAR_HTTP_FILE_SERVLET_H__

#include <memory>
#include <string>
#include <vector>
#include "servlet.h"

namespace sylar {
namespace http {

/**
 * @brief 静态文件服务Servlet
 */
class FileServlet : public Servlet {
public:
    typedef std::shared_ptr<FileServlet> ptr;

    /**
     * @brief 构造函数
     * @param[in] root_path 根目录路径
     * @param[in] enable_directory_listing 是否启用目录列表
     */
    FileServlet(const std::string& root_path, bool enable_directory_listing = false);

    /**
     * @brief 构造函数
     * @param[in] root_path 根目录路径
     * @param[in] path_prefix 路径前缀，用于从请求路径中去除
     * @param[in] enable_directory_listing 是否启用目录列表
     */
    FileServlet(const std::string& root_path, const std::string& path_prefix, bool enable_directory_listing = false);

    /**
     * @brief 处理HTTP请求
     */
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                          sylar::http::HttpResponse::ptr response,
                          sylar::SocketStream::ptr session) override;

    /**
     * @brief 设置根目录
     */
    void setRootPath(const std::string& root_path) { m_root_path = root_path; }

    /**
     * @brief 获取根目录
     */
    const std::string& getRootPath() const { return m_root_path; }

    /**
     * @brief 设置是否启用目录列表
     */
    void setEnableDirectoryListing(bool enable) { m_enable_directory_listing = enable; }

    /**
     * @brief 是否启用目录列表
     */
    bool isEnableDirectoryListing() const { return m_enable_directory_listing; }

    /**
     * @brief 添加默认索引文件
     */
    void addIndexFile(const std::string& filename);

    /**
     * @brief 设置默认索引文件列表
     */
    void setIndexFiles(const std::vector<std::string>& files) { m_index_files = files; }

private:
    /**
     * @brief 处理Range请求
     */
    bool handleRangeRequest(HttpRequest::ptr request, HttpResponse::ptr response, 
                           const std::string& file_path);

    /**
     * @brief 生成目录列表HTML
     */
    std::string generateDirectoryListing(const std::string& dir_path, const std::string& uri_path);

    /**
     * @brief 检查文件是否安全（防止路径遍历攻击）
     */
    bool isPathSafe(const std::string& path);

    /**
     * @brief 查找索引文件
     */
    std::string findIndexFile(const std::string& dir_path);

private:
    std::string m_root_path;                    // 根目录路径
    std::string m_path_prefix;                  // 路径前缀
    bool m_enable_directory_listing;            // 是否启用目录列表
    std::vector<std::string> m_index_files;     // 默认索引文件列表
};

/**
 * @brief 文件下载Servlet
 */
class FileDownloadServlet : public Servlet {
public:
    typedef std::shared_ptr<FileDownloadServlet> ptr;

    /**
     * @brief 构造函数
     * @param[in] root_path 根目录路径
     */
    FileDownloadServlet(const std::string& root_path);

    /**
     * @brief 处理HTTP请求
     */
    virtual int32_t handle(sylar::http::HttpRequest::ptr request,
                          sylar::http::HttpResponse::ptr response,
                          sylar::SocketStream::ptr session) override;

private:
    std::string m_root_path;    // 根目录路径
};

} // namespace http
} // namespace sylar

#endif