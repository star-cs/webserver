#include "sylar/sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

class MyTcpServer : public sylar::TcpServer{
protected:
    virtual void handleClient(sylar::Socket::ptr client) override{
        SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
        // static sylar::Buffer buf(4096);
        // client->recv((void*)buf.Begin(), buf.writeableSize());
        // SYLAR_LOG_INFO(g_logger) << "recv: " << buf.dump();
        // client->send((void*)buf.Begin(), buf.readableSize());
        // client->close();
        
        // static std::string buf;
        // buf.resize(4096);
        // client->recv(&buf[0], buf.length()); // 这里有读超时，由tcp_server.read_timeout配置项进行配置，默认120秒
        // SYLAR_LOG_INFO(g_logger) << "recv: " << buf;
        // client->send(&buf[0], buf.length());
        // client->close();

        sylar::ByteArray::ptr ba(new sylar::ByteArray);
        ba->clear();
        std::vector<iovec> iovs;
        ba->getWriteBuffers(iovs, 1024);
        
        int rt = client->recv(&iovs[0], iovs.size());
        if(rt > 0){
            
            ba->setPosition(ba->getPosition() + rt);
            ba->setPosition(0);
            
            SYLAR_LOG_INFO(g_logger) << "recv: " << ba->toString();
            client->send(&iovs[0], iovs.size());
        }
        client->close();
    }
};

void run(){
    sylar::TcpServer::ptr server(new MyTcpServer);
    auto addr = sylar::Address::LookupAny("0.0.0.0:12345");
    SYLAR_ASSERT(addr);

    std::vector<sylar::Address::ptr> addrs;
    addrs.push_back(addr);

    std::vector<sylar::Address::ptr> fails;
    while(!server->bind(addrs, fails)){
        sleep(2);
    }

    SYLAR_LOG_INFO(g_logger) << "bind success, " << server->toString();
    server->start();
}

int main(){

    // sylar::pageCache::GetInstance();
    // sylar::centralCache::GetInstance();
    
    sylar::IOManager iom(2);

    iom.schedule(run);

    return 0;
}