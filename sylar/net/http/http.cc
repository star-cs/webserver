#include "http.h"
#include "sylar/core/util/util.h"
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

namespace sylar
{
namespace http
{

    HttpMethod StringToHttpMethod(const std::string &m)
    {
#define XX(num, name, string)                                                                      \
    if (strcmp(#string, m.c_str()) == 0) {                                                         \
        return HttpMethod::name;                                                                   \
    }
        HTTP_METHOD_MAP(XX);
#undef XX
        return HttpMethod::INVALID_METHOD;
    }

    HttpMethod CharsToHttpMethod(const char *m)
    {
#define XX(num, name, string)                                                                      \
    if (strncmp(#string, m, strlen(#string)) == 0) {                                               \
        return HttpMethod::name;                                                                   \
    }
        HTTP_METHOD_MAP(XX);
#undef XX
        return HttpMethod::INVALID_METHOD;
    }

    static const char *s_method_string[] = {
#define XX(num, name, string) #string,
        HTTP_METHOD_MAP(XX)
#undef XX
    };

    const char *HttpMethodToString(const HttpMethod &m)
    {
        uint32_t idx = (uint32_t)m;
        if (idx >= (sizeof(s_method_string) / sizeof(s_method_string[0]))) {
            return "<unknown>";
        }
        return s_method_string[idx];
    }

    const char *HttpStatusToString(const HttpStatus &s)
    {
        switch (s) {
#define XX(code, name, msg)                                                                        \
    case HttpStatus::name:                                                                         \
        return #msg;
            HTTP_STATUS_MAP(XX);
#undef XX
            default:
                return "<unknown>";
        }
    }

    bool CaseInsensitiveLess::operator()(const std::string &lhs, const std::string &rhs) const
    {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
    }

    HttpRequest::HttpRequest(uint8_t version, bool close)
        : m_method(HttpMethod::GET), m_version(version), m_close(close), m_websocket(false),
          m_parserParamFlag(0), m_streamId(0), m_path("/")
    {
    }

    std::string HttpRequest::getHeader(const std::string &key, const std::string &def) const
    {
        auto it = m_headers.find(key);
        return it == m_headers.end() ? def : it->second;
    }

    void HttpRequest::setUri(const std::string &uri)
    {
        auto pos = uri.find('?');
        if (pos == std::string::npos) {
            auto pos2 = uri.find('#');
            if (pos2 == std::string::npos) {
                m_path = uri;
            } else {
                m_path = uri.substr(0, pos2);
                m_fragment = uri.substr(pos2 + 1);
            }
        } else {
            m_path = uri.substr(0, pos);

            auto pos2 = uri.find('#', pos + 1);
            if (pos2 == std::string::npos) {
                m_query = uri.substr(pos + 1);
            } else {
                m_query = uri.substr(pos + 1, pos2 - pos - 1);
                m_fragment = uri.substr(pos2 + 1);
            }
        }
    }

    std::string HttpRequest::getUri()
    {
        return m_path + (m_query.empty() ? "" : "?" + m_query)
               + (m_fragment.empty() ? "" : "#" + m_fragment);
    }

    std::shared_ptr<HttpResponse> HttpRequest::createResponse()
    {
        return std::make_shared<HttpResponse>(getVersion(), isClose());
    }

    std::string HttpRequest::getParam(const std::string &key, const std::string &def)
    {
        initQueryParam();
        initBodyParam();
        auto it = m_params.find(key);
        return it == m_params.end() ? def : it->second;
    }

    std::string HttpRequest::getCookie(const std::string &key, const std::string &def)
    {
        initCookies();
        auto it = m_cookies.find(key);
        return it == m_cookies.end() ? def : it->second;
    }

    void HttpRequest::setHeader(const std::string &key, const std::string &val)
    {
        m_headers[key] = val;
    }

    void HttpRequest::setParam(const std::string &key, const std::string &val)
    {
        m_params[key] = val;
    }

    void HttpRequest::setCookie(const std::string &key, const std::string &val)
    {
        m_cookies[key] = val;
    }

    void HttpRequest::delHeader(const std::string &key)
    {
        m_headers.erase(key);
    }

    void HttpRequest::delParam(const std::string &key)
    {
        m_params.erase(key);
    }

    void HttpRequest::delCookie(const std::string &key)
    {
        m_cookies.erase(key);
    }

    bool HttpRequest::hasHeader(const std::string &key, std::string *val)
    {
        auto it = m_headers.find(key);
        if (it == m_headers.end()) {
            return false;
        }
        if (val) {
            *val = it->second;
        }
        return true;
    }

    bool HttpRequest::hasParam(const std::string &key, std::string *val)
    {
        initQueryParam();
        initBodyParam();
        auto it = m_params.find(key);
        if (it == m_params.end()) {
            return false;
        }
        if (val) {
            *val = it->second;
        }
        return true;
    }

    bool HttpRequest::hasCookie(const std::string &key, std::string *val)
    {
        initCookies();
        auto it = m_cookies.find(key);
        if (it == m_cookies.end()) {
            return false;
        }
        if (val) {
            *val = it->second;
        }
        return true;
    }

    std::string HttpRequest::toString() const
    {
        std::stringstream ss;
        dump(ss);
        return ss.str();
    }

    std::ostream &HttpRequest::dump(std::ostream &os) const
    {
        // GET /uri HTTP/1.1
        // Host: wwww.sylar.top
        //
        //
        os << HttpMethodToString(m_method) << " " << m_path << (m_query.empty() ? "" : "?")
           << m_query << (m_fragment.empty() ? "" : "#") << m_fragment << " HTTP/"
           << ((uint32_t)(m_version >> 4)) << "." << ((uint32_t)(m_version & 0x0F)) << "\r\n";
        if (!m_websocket) {
            os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
        }
        for (auto &i : m_headers) {
            if (!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
                continue;
            }
            if (!strcasecmp(i.first.c_str(), "content-length")) {
                continue;
            }
            os << i.first << ": " << i.second << "\r\n";
        }

        if (!m_body.empty()) {
            os << "content-length: " << m_body.size() << "\r\n\r\n" << m_body;
        } else {
            os << "\r\n";
        }
        return os;
    }

    void HttpRequest::init()
    {
        std::string conn = getHeader("connection");
        if (!conn.empty()) {
            if (strcasecmp(conn.c_str(), "keep-alive") == 0) {
                m_close = false;
            } else {
                m_close = true;
            }
        }
    }

    void HttpRequest::initParam()
    {
        initQueryParam();
        initBodyParam();
        initCookies();
    }

    void HttpRequest::initQueryParam()
    {
        if (m_parserParamFlag & 0x1) {
            return;
        }

#define PARSE_PARAM(str, m, flag, trim)                                                            \
    size_t pos = 0;                                                                                \
    do {                                                                                           \
        size_t last = pos;                                                                         \
        pos = str.find('=', pos);                                                                  \
        if (pos == std::string::npos) {                                                            \
            break;                                                                                 \
        }                                                                                          \
        size_t key = pos;                                                                          \
        pos = str.find(flag, pos);                                                                 \
        m.insert(                                                                                  \
            std::make_pair(trim(str.substr(last, key - last)),                                     \
                           sylar::StringUtil::UrlDecode(str.substr(key + 1, pos - key - 1))));     \
        if (pos == std::string::npos) {                                                            \
            break;                                                                                 \
        }                                                                                          \
        ++pos;                                                                                     \
    } while (true);

        PARSE_PARAM(m_query, m_params, '&', );
        m_parserParamFlag |= 0x1;
    }

    void HttpRequest::initBodyParam()
    {
        if (m_parserParamFlag & 0x2) {
            return;
        }
        std::string content_type = getHeader("content-type");
        if (strcasestr(content_type.c_str(), "application/x-www-form-urlencoded") == nullptr) {
            m_parserParamFlag |= 0x2;
            return;
        }
        PARSE_PARAM(m_body, m_params, '&', );
        m_parserParamFlag |= 0x2;
    }

    void HttpRequest::initCookies()
    {
        if (m_parserParamFlag & 0x4) {
            return;
        }
        std::string cookie = getHeader("cookie");
        if (cookie.empty()) {
            m_parserParamFlag |= 0x4;
            return;
        }
        PARSE_PARAM(cookie, m_cookies, ';', sylar::StringUtil::Trim);
        m_parserParamFlag |= 0x4;
    }

    void HttpRequest::paramToQuery()
    {
        m_query = sylar::MapJoin(m_params.begin(), m_params.end());
    }

    HttpResponse::HttpResponse(uint8_t version, bool close)
        : m_status(HttpStatus::OK), m_version(version), m_close(close), m_websocket(false),
          m_file_size(0), m_range_start(0), m_range_end(-1)
    {
    }

    void HttpResponse::initConnection()
    {
        std::string conn = getHeader("connection");
        if (!conn.empty()) {
            if (strcasecmp(conn.c_str(), "keep-alive") == 0) {
                m_close = false;
            } else {
                m_close = m_version == 0x10;
            }
        }
    }

    std::string HttpResponse::getHeader(const std::string &key, const std::string &def) const
    {
        auto it = m_headers.find(key);
        return it == m_headers.end() ? def : it->second;
    }

    void HttpResponse::setHeader(const std::string &key, const std::string &val)
    {
        m_headers[key] = val;
    }

    void HttpResponse::delHeader(const std::string &key)
    {
        m_headers.erase(key);
    }

    void HttpResponse::setRedirect(const std::string &uri)
    {
        m_status = HttpStatus::FOUND;
        setHeader("Location", uri);
    }

    void HttpResponse::setCookie(const std::string &key, const std::string &val, time_t expired,
                                 const std::string &path, const std::string &domain, bool secure)
    {
        std::stringstream ss;
        ss << key << "=" << val;
        if (expired > 0) {
            ss << ";expires=" << sylar::Time2Str(expired, "%a, %d %b %Y %H:%M:%S") << " GMT";
        }
        if (!domain.empty()) {
            ss << ";domain=" << domain;
        }
        if (!path.empty()) {
            ss << ";path=" << path;
        }
        if (secure) {
            ss << ";secure";
        }
        m_cookies.push_back(ss.str());
    }

    std::string HttpResponse::toString() const
    {
        std::stringstream ss;
        dump(ss);
        return ss.str();
    }

    std::ostream &HttpResponse::dump(std::ostream &os) const
    {
        os << "HTTP/" << ((uint32_t)(m_version >> 4)) << "." << ((uint32_t)(m_version & 0x0F))
           << " " << (uint32_t)m_status << " "
           << (m_reason.empty() ? HttpStatusToString(m_status) : m_reason) << "\r\n";

        bool has_content_length = false;
        for (auto &i : m_headers) {
            if (!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
                continue;
            }
            if (!has_content_length && strcasecmp(i.first.c_str(), "content-length") == 0) {
                has_content_length = true;
            }
            os << i.first << ": " << i.second << "\r\n";
        }
        for (auto &i : m_cookies) {
            os << "Set-Cookie: " << i << "\r\n";
        }
        if (!m_websocket) {
            os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
        }
        if (!m_body.empty()) {
            if (!has_content_length) {
                os << "content-length: " << m_body.size() << "\r\n\r\n" << m_body;
            } else {
                os << "\r\n" << m_body;
            }
        } else {
            os << "\r\n";
        }
        return os;
    }

    std::ostream &operator<<(std::ostream &os, const HttpRequest &req)
    {
        return req.dump(os);
    }

    std::ostream &operator<<(std::ostream &os, const HttpResponse &rsp)
    {
        return rsp.dump(os);
    }

    // 辅助函数：根据文件扩展名推断MIME类型
    static std::string getMimeType(const std::string& file_path) {
        size_t pos = file_path.find_last_of('.');
        if (pos == std::string::npos) {
            return "application/octet-stream";
        }
        
        std::string ext = file_path.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        static std::map<std::string, std::string> mime_types = {
            {"html", "text/html"},
            {"htm", "text/html"},
            {"css", "text/css"},
            {"js", "application/javascript"},
            {"json", "application/json"},
            {"xml", "application/xml"},
            {"txt", "text/plain"},
            {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"png", "image/png"},
            {"gif", "image/gif"},
            {"svg", "image/svg+xml"},
            {"pdf", "application/pdf"},
            {"zip", "application/zip"},
            {"mp4", "video/mp4"},
            {"mp3", "audio/mpeg"},
            {"wav", "audio/wav"}
        };
        
        auto it = mime_types.find(ext);
        return it != mime_types.end() ? it->second : "application/octet-stream";
    }

    bool HttpResponse::setFile(const std::string& file_path, const std::string& content_type) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            return false;  // 文件不存在或无法访问
        }
        
        if (!S_ISREG(file_stat.st_mode)) {
            return false;  // 不是普通文件
        }
        
        m_file_path = file_path;
        m_file_size = file_stat.st_size;
        m_range_start = 0;
        m_range_end = m_file_size - 1;
        
        // 设置Content-Type
        std::string mime_type = content_type.empty() ? getMimeType(file_path) : content_type;
        setHeader("Content-Type", mime_type);
        setHeader("Content-Length", std::to_string(m_file_size));
        
        // 清空body，因为我们将使用sendfile发送文件内容
        m_body.clear();
        
        return true;
    }

    bool HttpResponse::setFileDownload(const std::string& file_path, const std::string& download_name) {
        if (!setFile(file_path)) {
            return false;
        }
        
        std::string filename = download_name;
        if (filename.empty()) {
            size_t pos = file_path.find_last_of('/');
            filename = (pos != std::string::npos) ? file_path.substr(pos + 1) : file_path;
        }
        
        setHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        return true;
    }

    bool HttpResponse::setFileRange(const std::string& file_path, off_t range_start, 
                                   off_t range_end, const std::string& content_type) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            return false;
        }
        
        if (!S_ISREG(file_stat.st_mode)) {
            return false;
        }
        
        m_file_path = file_path;
        m_file_size = file_stat.st_size;
        m_range_start = range_start;
        m_range_end = (range_end == -1) ? m_file_size - 1 : range_end;
        
        // 验证范围
        if (m_range_start < 0 || m_range_start >= m_file_size || 
            m_range_end < m_range_start || m_range_end >= m_file_size) {
            return false;
        }
        
        off_t content_length = m_range_end - m_range_start + 1;
        
        // 设置HTTP状态为206 Partial Content
        setStatus(HttpStatus::PARTIAL_CONTENT);
        
        // 设置Content-Type
        std::string mime_type = content_type.empty() ? getMimeType(file_path) : content_type;
        setHeader("Content-Type", mime_type);
        setHeader("Content-Length", std::to_string(content_length));
        setHeader("Content-Range", "bytes " + std::to_string(m_range_start) + "-" + 
                  std::to_string(m_range_end) + "/" + std::to_string(m_file_size));
        setHeader("Accept-Ranges", "bytes");
        
        // 清空body
        m_body.clear();
        
        return true;
    }

} // namespace http
} // namespace sylar
