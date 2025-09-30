#include "file_servlet.h"
#include "http.h"
#include "http_session.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace sylar {
namespace http {

FileServlet::FileServlet(const std::string& root_path, bool enable_directory_listing)
    : Servlet("FileServlet")
    , m_root_path(root_path)
    , m_path_prefix("")
    , m_enable_directory_listing(enable_directory_listing) {
    // 添加默认索引文件
    m_index_files.push_back("index.html");
    m_index_files.push_back("index.htm");
    m_index_files.push_back("default.html");
}

FileServlet::FileServlet(const std::string& root_path, const std::string& path_prefix, bool enable_directory_listing)
    : Servlet("FileServlet")
    , m_root_path(root_path)
    , m_path_prefix(path_prefix)
    , m_enable_directory_listing(enable_directory_listing) {
    // 添加默认索引文件
    m_index_files.push_back("index.html");
    m_index_files.push_back("index.htm");
    m_index_files.push_back("default.html");
}

int32_t FileServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, SocketStream::ptr session) {
    std::string uri = request->getPath();
    
    // 如果设置了路径前缀，从URI中去除前缀
    if (!m_path_prefix.empty()) {
        if (uri.find(m_path_prefix) == 0) {
            uri = uri.substr(m_path_prefix.length());
            // 确保路径以/开头
            if (uri.empty() || uri[0] != '/') {
                uri = "/" + uri;
            }
        } else {
            // 路径不匹配前缀，返回404
            response->setStatus(HttpStatus::NOT_FOUND);
            response->setBody("Not Found");
            return 0;
        }
    }
    
    // 防止路径遍历攻击
    if (uri.find("..") != std::string::npos) {
        response->setStatus(HttpStatus::BAD_REQUEST);
        response->setBody("Bad Request: Invalid path");
        return 0;
    }
    
    // 构建完整文件路径
    std::string file_path = m_root_path;
    if (file_path.back() != '/' && uri.front() != '/') {
        file_path += "/";
    }
    file_path += uri;
    
    // 检查路径安全性
    if (!isPathSafe(file_path)) {
        response->setStatus(HttpStatus::FORBIDDEN);
        response->setBody("Forbidden: Access denied");
        return 0;
    }
    
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("Not Found");
        return 0;
    }
    
    // 如果是目录
    if (S_ISDIR(file_stat.st_mode)) {
        // 查找索引文件
        std::string index_file = findIndexFile(file_path);
        if (!index_file.empty()) {
            file_path = index_file;
            if (stat(file_path.c_str(), &file_stat) != 0) {
                response->setStatus(HttpStatus::NOT_FOUND);
                response->setBody("Not Found");
                return 0;
            }
        } else if (m_enable_directory_listing) {
            // 生成目录列表
            std::string listing = generateDirectoryListing(file_path, uri);
            response->setStatus(HttpStatus::OK);
            response->setHeader("Content-Type", "text/html; charset=utf-8");
            response->setBody(listing);
            return 0;
        } else {
            response->setStatus(HttpStatus::FORBIDDEN);
            response->setBody("Forbidden: Directory listing disabled");
            return 0;
        }
    }
    
    // 检查是否为Range请求
    if (request->hasHeader("Range")) {
        if (handleRangeRequest(request, response, file_path)) {
            return 0;
        }
    }
    
    // 发送完整文件
    response->setFile(file_path);
    response->setStatus(HttpStatus::OK);
    
    return 0;
}

bool FileServlet::handleRangeRequest(HttpRequest::ptr request, HttpResponse::ptr response, 
                                   const std::string& file_path) {
    std::string range_header = request->getHeader("Range");
    if (range_header.empty() || range_header.substr(0, 6) != "bytes=") {
        return false;
    }
    
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        return false;
    }
    
    size_t file_size = file_stat.st_size;
    std::string range_spec = range_header.substr(6); // 去掉 "bytes="
    
    size_t dash_pos = range_spec.find('-');
    if (dash_pos == std::string::npos) {
        return false;
    }
    
    size_t start = 0, end = file_size - 1;
    
    std::string start_str = range_spec.substr(0, dash_pos);
    std::string end_str = range_spec.substr(dash_pos + 1);
    
    if (!start_str.empty()) {
        start = std::stoull(start_str);
    }
    if (!end_str.empty()) {
        end = std::stoull(end_str);
    }
    
    if (start >= file_size || end >= file_size || start > end) {
        response->setStatus(HttpStatus::RANGE_NOT_SATISFIABLE);
        response->setHeader("Content-Range", "bytes */" + std::to_string(file_size));
        return true;
    }
    
    response->setFileRange(file_path, start, end);
    response->setStatus(HttpStatus::PARTIAL_CONTENT);
    
    return true;
}

std::string FileServlet::generateDirectoryListing(const std::string& dir_path, const std::string& uri_path) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n"
        << "<html><head><title>Directory listing for " << uri_path << "</title></head>\n"
        << "<body><h1>Directory listing for " << uri_path << "</h1>\n"
        << "<hr><ul>\n";
    
    if (uri_path != "/") {
        oss << "<li><a href=\"../\">../</a></li>\n";
    }
    
    DIR* dir = opendir(dir_path.c_str());
    if (dir) {
        struct dirent* entry;
        std::vector<std::string> files, dirs;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            
            std::string full_path = dir_path + "/" + name;
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) == 0) {
                if (S_ISDIR(file_stat.st_mode)) {
                    dirs.push_back(name);
                } else {
                    files.push_back(name);
                }
            }
        }
        closedir(dir);
        
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        
        for (const auto& d : dirs) {
            oss << "<li><a href=\"" << d << "/\">" << d << "/</a></li>\n";
        }
        for (const auto& f : files) {
            oss << "<li><a href=\"" << f << "\">" << f << "</a></li>\n";
        }
    }
    
    oss << "</ul><hr></body></html>\n";
    return oss.str();
}

bool FileServlet::isPathSafe(const std::string& path) {
    // 简单的路径安全检查，防止路径遍历
    std::string canonical_root = m_root_path;
    std::string canonical_path = path;
    
    // 这里应该使用realpath进行更严格的检查，但为了简化先用字符串检查
    return canonical_path.find(canonical_root) == 0;
}

std::string FileServlet::findIndexFile(const std::string& dir_path) {
    for (const auto& index_file : m_index_files) {
        std::string full_path = dir_path;
        if (full_path.back() != '/') {
            full_path += "/";
        }
        full_path += index_file;
        
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            return full_path;
        }
    }
    return "";
}

void FileServlet::addIndexFile(const std::string& filename) {
    m_index_files.push_back(filename);
}

// FileDownloadServlet 实现
FileDownloadServlet::FileDownloadServlet(const std::string& root_path)
    : Servlet("FileDownloadServlet")
    , m_root_path(root_path) {
}

int32_t FileDownloadServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response, SocketStream::ptr session) {
    std::string uri = request->getPath();
    
    // 防止路径遍历攻击
    if (uri.find("..") != std::string::npos) {
        response->setStatus(HttpStatus::BAD_REQUEST);
        response->setBody("Bad Request: Invalid path");
        return 0;
    }
    
    // 构建完整文件路径
    std::string file_path = m_root_path;
    if (file_path.back() != '/' && uri.front() != '/') {
        file_path += "/";
    }
    file_path += uri;
    
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("File Not Found");
        return 0;
    }
    
    // 设置为下载模式
    response->setFileDownload(file_path);
    response->setStatus(HttpStatus::OK);
    
    return 0;
}

} // namespace http
} // namespace sylar