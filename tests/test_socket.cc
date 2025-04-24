#include "sylar/sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_socket(){
    sylar::IPAddress::ptr addr = sylar::Address::LookupAnyIPAddress("www.baidu.com");
    if(addr){
        SYLAR_LOG_INFO(g_logger) << "get address:" << addr->toString();
    }else{
        SYLAR_LOG_INFO(g_logger) << "get address failed";
        return;
    }
    sylar::Socket::ptr socket = sylar::Socket::CreateTCPSocket();
    addr->setPort(80);
    SYLAR_LOG_INFO(g_logger) << "addr = " << addr->toString();

    if(!socket->connect(addr)){
        SYLAR_LOG_ERROR(g_logger) << "connect " << addr->toString() << " fail";
        return;
    }else{
        SYLAR_LOG_INFO(g_logger) << "connect " << addr->toString() << " connected";
    }

    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int rt = socket->send(buff, sizeof(buff));
    if(rt <= 0){
        SYLAR_LOG_INFO(g_logger) << "send fail rt = " << rt;
        return;
    }

    std::string buf;
    buf.resize(4096);
    rt = socket->recv(&buf[0], buf.size());
    if(rt <= 0){
        SYLAR_LOG_INFO(g_logger) << "recv fail rt = " << rt;
        return;
    }

    buf.resize(rt);
    SYLAR_LOG_INFO(g_logger) << buf;

    socket->close();
}


int main(){

    sylar::IOManager iom;

    iom.schedule(&test_socket);

    return 0;
}