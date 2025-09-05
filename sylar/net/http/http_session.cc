#include "http_session.h"

#include "http_parser.h"
#include "sylar/core/memory/memorypool.h"

namespace sylar{
namespace http{

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner){
}

HttpRequest::ptr HttpSession::recvRequest(){
    HttpRequestParser::ptr parser(new HttpRequestParser);
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    std::shared_ptr<char> buffer(
        (char*)SYLAR_THREAD_MALLOC(buff_size), [=](char* ptr){
            SYLAR_THREAD_FREE(ptr, buff_size);
        }
    );

    char* data = buffer.get();

    int offset = 0;
    do{
        int len = read(data + offset, buff_size - offset);
        if(len <= 0){
            close();
            return nullptr;
        }
        len += offset;
        // execute 会 对 data 进行移位
        size_t nparse = parser->execute(data, len);
        if(parser->hasError()){
            close();
            return nullptr;
        }
        offset = len - nparse;  /// 未解析的部分
        if(offset == (int)buff_size){ // 没解析
            close();
            return nullptr;
        }
        if(parser->isFinished()){
            break;
        }
    }while(true);

    parser->getData()->init();
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp){
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return m_socket->send(data.c_str(), data.size());
}

}
}