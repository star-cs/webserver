#include "sylar/sylar.h"
#include "sylar/net/http/http_server.h"
#include "sylar/net/http/file_servlet.h"
#include <iostream>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run() {
    sylar::http::HttpServer::ptr server(new sylar::http::HttpServer(true));
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:8020");
    
    while (!server->bind(addr)) {
        sleep(2);
    }
    
    auto sd = server->getServletDispatch();
    
    // 添加静态文件服务，支持目录列表
    sylar::http::FileServlet::ptr file_servlet(new sylar::http::FileServlet("./www", "/static", true));
    file_servlet->addIndexFile("index.html");
    file_servlet->addIndexFile("index.htm");
    sd->addGlobServlet("/static/*", file_servlet);
    
    // 添加文件下载服务
    sylar::http::FileDownloadServlet::ptr download_servlet(new sylar::http::FileDownloadServlet("./downloads"));
    sd->addGlobServlet("/download/*", download_servlet);
    
    // 添加根路径的文件服务 - 放在最后作为默认处理
    sylar::http::FileServlet::ptr root_servlet(new sylar::http::FileServlet("./www", true));
    sd->addGlobServlet("/*", root_servlet);
    
    // 添加测试页面
    sd->addServlet("/test", [](sylar::http::HttpRequest::ptr request,
                              sylar::http::HttpResponse::ptr response,
                              sylar::SocketStream::ptr session) {
        response->setBody(R"(
<!DOCTYPE html>
<html>
<head>
    <title>HTTP File Server Test</title>
    <meta charset="utf-8">
</head>
<body>
    <h1>HTTP File Server Test Page</h1>
    <h2>Features:</h2>
    <ul>
        <li><a href="/static/">Static File Service (with directory listing)</a></li>
        <li><a href="/download/">File Download Service</a></li>
        <li>HTTP Range Request Support (for resume downloads)</li>
        <li>Efficient file transfer using sendfile()</li>
    </ul>
    
    <h2>Test Files:</h2>
    <p>Create some test files in the following directories:</p>
    <ul>
        <li><code>./www/</code> - for static file service</li>
        <li><code>./downloads/</code> - for download service</li>
    </ul>
    
    <h2>Range Request Test:</h2>
    <p>Use curl to test range requests:</p>
    <pre>
# Download first 1024 bytes
curl -H "Range: bytes=0-1023" http://localhost:8020/static/test.txt

# Resume download from byte 1024
curl -H "Range: bytes=1024-" http://localhost:8020/static/test.txt
    </pre>
</body>
</html>
        )");
        response->setHeader("Content-Type", "text/html; charset=utf-8");
        return 0;
    });
    
    SYLAR_LOG_INFO(g_logger) << "HTTP File Server starting on " << *addr;
    server->start();
}

int main(int argc, char** argv) {
    sylar::IOManager iom(2);
    iom.schedule(run);
    return 0;
}