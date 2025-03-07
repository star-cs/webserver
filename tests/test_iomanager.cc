#include "sylar/sylar.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sockfd;


void do_io_write(){

}

void do_io_read(){

}

void test_io(){
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    SYLAR_ASSERT(sockfd > 0);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    
    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9527);
    inet_pton(AF_INET, "192.168.1.1", &servaddr.sin_addr.s_addr);

    int rt = connect(sockfd, (const sockaddr*)&servaddr, sizeof(servaddr));
    if(rt != 0){
        if(errno == EINPROGRESS) {  //代表连接还在进行
            SYLAR_LOG_INFO(g_logger) << "EINPROGRESS";

            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::WRITE, do_io_write);

            sylar::IOManager::GetThis()->addEvent(sockfd, sylar::IOManager::READ, do_io_read);
        
        }else{
            SYLAR_LOG_ERROR(g_logger) << "connect error, errno:" << errno << ", errstr:" << strerror(errno);
        }

    }else{
        SYLAR_LOG_ERROR(g_logger) << "else, errno:" << errno << ", errstr:" << strerror(errno);
    }
}


void test_iomanager(){
    sylar::IOManager iom(1, false);
    
    iom.schedule(test_io);
}

int main(){
    
    test_iomanager();

    return 0;
}