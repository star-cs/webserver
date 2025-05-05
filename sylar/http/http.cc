#include "http.h"


namespace sylar{
namespace http{


HttpMethod StringToHttpMethod(const std::string& m){
#define XX(num, name, string) \
    if(strcmp(#string, m.c_str()) == 0){ \
        return HttpMethod::name; \
    }

    HTTP_METHOD_MAP(XX);
#undef XX
    return HttpMethod::INVALID_METHOD;
}

HttpMethod CharsToHttpMethod(const char* m) {
#define XX(num, name, string) \
    if(strncmp(#string, m, strlen(#string)) == 0) { \
        return HttpMethod::name; \
    }
    HTTP_METHOD_MAP(XX);
#undef XX
    return HttpMethod::INVALID_METHOD;
}


const char* HttpMethodToString(const HttpMethod& m){
    switch(m){
#define XX(num, name, string) \
        case HttpMethod::name: \
            return #string;

    HTTP_METHOD_MAP(XX)
#undef XX
        default:
            return "<unknown>";
    }
}

const char* HttpStatusToString(const HttpStatus& s){
    switch(s){
#define XX(code, name, desc) \
        case HttpStatus::name: \
            return #desc;
    HTTP_STATUS_MAP(XX)
#undef XX
        default:
            return "<unknown>";
    }
}

bool CaseInsensitiveLess::operator()(const std::string& lhs, const std::string& rhs) const{
    //strcasecmp 在比较字符串时忽略大小写
    return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
}


/// ************************** HttpRequest **************************

HttpRequest::HttpRequest(uint8_t version, bool close)
    :m_method(HttpMethod::GET)
    ,m_version(version)
    ,m_close(close)
    ,m_websocket(false)
    ,m_parserParamFlag(0)
    ,m_path("/") {
}

std::shared_ptr<HttpResponse> HttpRequest::createResponse(){
    HttpResponse::ptr rsp(new HttpResponse(getVersion(), isClose()));
    return rsp;
}

std::string HttpRequest::getHeader(const std::string& key, const std::string& def) const{
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

std::string HttpRequest::getParam(const std::string& key, const std::string& def){
    initQueryParam();
    initBodyParam();
    auto it = m_params.find(key);
    return it == m_params.end() ? def : it->second;
}

std::string HttpRequest::getCookie(const std::string& key, const std::string& def){
    initCookies();
    auto it = m_cookies.find(key);
    return it == m_cookies.end() ? def : it->second;
}

void HttpRequest::setHeader(const std::string& key, const std::string& val){
    m_headers[key] = val;
}

void HttpRequest::setParam(const std::string& key, const std::string& val){
    m_params[key] = val;
}

void HttpRequest::setCookie(const std::string& key, const std::string& val){
    m_cookies[key] = val;
}

void HttpRequest::delHeader(const std::string& key){
    m_headers.erase(key);
}

void HttpRequest::delParam(const std::string& key){
    m_params.erase(key);
}

void HttpRequest::delCookie(const std::string& key){
    m_cookies.erase(key);
}

bool HttpRequest::hasHeader(const std::string& key, std::string* val){
    auto it = m_headers.find(key);
    if(it == m_headers.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasParam(const std::string& key, std::string* val){
    initQueryParam();
    initBodyParam();
    auto it = m_params.find(key);
    if(it == m_params.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

bool HttpRequest::hasCookie(const std::string& key, std::string* val){
    initCookies();
    auto it = m_cookies.find(key);
    if(it == m_cookies.end()) {
        return false;
    }
    if(val) {
        *val = it->second;
    }
    return true;
}

std::ostream& HttpRequest::dump(std::ostream& os) const{
    /*
    GET /path?query#fragment HTTP/1.1
    Host: www.baidu.com

    */

    os  << HttpMethodToString(m_method) << " "
        << m_path 
        << (m_query.empty() ? "" : "?")
        << m_query
        << (m_fragment.empty() ? "" : "#")
        << m_fragment
        << " HTTP/"
        << ((uint32_t)(m_version >> 4))
        << "."
        << ((uint32_t)(m_version & 0x0F))
        << "\r\n";
    
    if(!m_websocket){
        os << "connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }

    for(auto& i : m_headers) {
        if(!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }

    if(!m_body.empty()) {
        os << "content-length: " << m_body.size() << "\r\n\r\n"
           << m_body;
    } else {
        os << "\r\n";
    }
    return os;

}

std::string HttpRequest::toString() const{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

void HttpRequest::initQueryParam(){
    
}

void HttpRequest::initBodyParam(){

}

void HttpRequest::initCookies(){

}

void HttpRequest::init(){
    
}

/// ************************** HttpResponse **************************


HttpResponse::HttpResponse(uint8_t version, bool close)
    :m_status(HttpStatus::OK)
    ,m_version(version)
    ,m_close(close)
    ,m_websocket(false){

}

std::string HttpResponse::getHeader(const std::string& key, const std::string& def) const{
    auto it = m_headers.find(key);
    return it == m_headers.end() ? def : it->second;
}

void HttpResponse::setHeader(const std::string& key, const std::string& val){
    m_headers[key] = val;
}

void HttpResponse::delHeader(const std::string& key){
    m_headers.erase(key);
}

std::ostream& HttpResponse::dump(std::ostream& os) const{
    os  << "HTTP/" 
        << ((uint32_t)(m_version >> 4))
        << "."
        << ((uint32_t)(m_version & 0x4F))
        << " "
        << (uint32_t)m_status
        << " "
        << (m_reason.empty() ? HttpStatusToString(m_status) : m_reason)
        << "\r\n";

    for (auto &i : m_headers) {
        if (!m_websocket && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        os << i.first << ": " << i.second << "\r\n";
    }

    for (auto &i : m_cookies) {
        os << "Set-Cookie: " << i << "\r\n";
    }
    if (!m_websocket) {
        os << "Connection: " << (m_close ? "close" : "keep-alive") << "\r\n";
    }
    if (!m_body.empty()) {
        os << "Content-Length: " << m_body.size() << "\r\n\r\n"
           << m_body;
    } else {
        os << "\r\n";
    }
    return os;
}


std::string HttpResponse::toString() const{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

void HttpResponse::setRedirect(const std::string& uri){

}

void HttpResponse::setCookie(const std::string& key, const std::string& val,
                        time_t expired, const std::string& path,
                        const std::string& domain, bool secure)
{

}

std::ostream& operator<<(std::ostream& os, const HttpRequest& req){
    return req.dump(os);
}

std::ostream& operator<<(std::ostream& os, const HttpResponse& rsp){
    return rsp.dump(os);
}

}
}