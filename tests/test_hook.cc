#include "sylar/hook.h"
#include "sylar/sylar.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_sleep(){
    SYLAR_LOG_INFO(g_logger) << "test sleep begin";
    sylar::IOManager iom;
    iom.schedule([]{
        SYLAR_LOG_INFO(g_logger) << "begin sleep 2";
        sleep(2);
        SYLAR_LOG_INFO(g_logger) << "end sleep 2";
    });

    iom.schedule([]{
        SYLAR_LOG_INFO(g_logger) << "begin sleep 3";
        sleep(3);
        SYLAR_LOG_INFO(g_logger) << "end sleep 3";
    });

    SYLAR_LOG_INFO(g_logger) << "test sleep end";
}

void test_socket(){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "39.156.66.10", &addr.sin_addr.s_addr);

    SYLAR_LOG_INFO(g_logger) << "begin connect";

    int rt = connect(sock, (const sockaddr*)&addr, sizeof(addr));

    SYLAR_LOG_INFO(g_logger) << "connect rt = " << rt << "errno = " << errno;

    if(rt){
        return;
    }

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);
    SYLAR_LOG_INFO(g_logger) << "send rt = " << rt << " errno = " << errno;

    if(rt <= 0){
        return;
    }

    std::string buff;
    buff.resize(4096);

    rt = recv(sock, &buff[0], buff.size(), 0);
    
    SYLAR_LOG_INFO(g_logger) << "recv rt = " << rt << " errno = " << errno;

    if(rt <= 0){
        return;
    }

    buff.resize(rt);
    SYLAR_LOG_INFO(g_logger) << buff;
}

int main(){
    // test_sleep();

    sylar::IOManager iom;

    iom.schedule(test_socket);

    SYLAR_LOG_INFO(g_logger) << "main end";
    return 0;
}