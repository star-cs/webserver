#include "sylar/sylar.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sockfd;
void watch_io_read();


void do_io_write(){
    SYLAR_LOG_INFO(g_logger) << "do_io_write()";

    int so_err;
    socklen_t len = size_t(so_err);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &len);
    if(so_err){
        SYLAR_LOG_INFO(g_logger) << "connect fail";
        return;
    }
    SYLAR_LOG_INFO(g_logger) << "connect success";
}

void do_io_read(){
    SYLAR_LOG_INFO(g_logger) << "do_io_read()";
    char buf[1024] = {0};
    int readlen = 0;
    readlen = read(sockfd, buf, sizeof(buf));
    if(readlen > 0){
        buf[readlen] = '\0';
        SYLAR_LOG_INFO(g_logger) << "read " << readlen << "bytes, read: " << buf; 
    }else if(readlen == 0){
        SYLAR_LOG_INFO(g_logger) << "peer closed";
        close(sockfd);
        return;
    }else{
        SYLAR_LOG_ERROR(g_logger) << "err, errno = " << errno << ", errstr=" << strerror(errno);
        close(sockfd);
        return;
    }
    // read之后重新添加读事件回调，这里不能直接调用addEvent，因为在当前位置fd的读事件上下文还是有效的，直接调用addEvent相当于重复添加相同事件
    sylar::IOManager::GetThis()->schedule(watch_io_read);
}

void watch_io_read() {
    SYLAR_LOG_INFO(g_logger) << "watch_io_read";
    sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, do_io_read);
}

void test_io(){
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SYLAR_ASSERT(sockfd > 0);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(1234);
    inet_pton(AF_INET, "110.242.68.66", &servaddr.sin_addr.s_addr);

    // sockfd是客户端
    int rt = connect(sockfd, (const sockaddr*)&servaddr, sizeof(servaddr));
    if(rt != 0){
        if(errno == EINPROGRESS) {  //代表连接还在进行
            SYLAR_LOG_INFO(g_logger) << "EINPROGRESS";
            // 注册写事件回调，只用于判断connect是否成功
            // 非阻塞的TCP套接字connect一般无法立即建立连接，要通过套接字可写来判断connect是否已经成功
            // sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::WRITE, do_io_write);
            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::WRITE, [](){
                SYLAR_LOG_INFO(g_logger) << "write callback";
                sylar::IOManager::GetThis()->cancelEvent(sockfd, sylar::IOManager::READ);
            });
            // 注册读事件回调，注意事件是一次性的
            // sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, do_io_read);
            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, [](){
                SYLAR_LOG_INFO(g_logger) << "read callback";
                close(sockfd);
            });
        
        }else{
            SYLAR_LOG_ERROR(g_logger) << "connect error, errno:" << errno << ", errstr:" << strerror(errno);
        }

    }else{
        SYLAR_LOG_ERROR(g_logger) << "else, errno:" << errno << ", errstr:" << strerror(errno);
    }
}


void test_iomanager(){
    sylar::IOManager iom;
    
    iom.schedule(test_io);
}

int main(){
    
    test_iomanager();

    return 0;
}