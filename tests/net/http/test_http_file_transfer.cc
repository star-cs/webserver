#include "sylar/sylar.h"
#include "sylar/net/http/file_servlet.h"
#include "sylar/net/http/http.h"
#include "sylar/net/http/http_session.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sys/stat.h>
#include <stdlib.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

class HttpFileTransferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 创建测试目录
        int ret1 = std::system("mkdir -p /tmp/test_www");
        int ret2 = std::system("mkdir -p /tmp/test_downloads");
        (void)ret1; (void)ret2; // 忽略返回值警告

        // 创建测试文件
        createTestFile("/tmp/test_www/test.txt",
                       "Hello, World! This is a test file for HTTP file transfer.");
        createTestFile("/tmp/test_www/large.txt", generateLargeContent(10240)); // 10KB
        createTestFile("/tmp/test_downloads/download.txt", "This is a download test file.");

        // 创建索引文件
        createTestFile("/tmp/test_www/index.html", "<html><body><h1>Index Page</h1></body></html>");
    }

    void TearDown() override
    {
        // 清理测试文件
        int ret1 = std::system("rm -rf /tmp/test_www");
        int ret2 = std::system("rm -rf /tmp/test_downloads");
        (void)ret1; (void)ret2; // 忽略返回值警告
    }

    void createTestFile(const std::string &path, const std::string &content)
    {
        std::ofstream file(path);
        file << content;
        file.close();
    }

    std::string generateLargeContent(size_t size)
    {
        std::string content;
        content.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            content += static_cast<char>('A' + (i % 26));
        }
        return content;
    }
};

TEST_F(HttpFileTransferTest, FileServletBasicTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/test.txt");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::OK);
    EXPECT_TRUE(response->isFileResponse());
    EXPECT_EQ(response->getFilePath(), "/tmp/test_www/test.txt");
}

TEST_F(HttpFileTransferTest, FileServletDirectoryListingTest)
{
    // 创建一个没有索引文件的目录来测试目录列表功能
    int ret1 = system("mkdir -p /tmp/test_listing");
    (void)ret1; // 忽略返回值警告
    createTestFile("/tmp/test_listing/file1.txt", "content1");
    createTestFile("/tmp/test_listing/file2.txt", "content2");
    
    sylar::http::FileServlet servlet("/tmp/test_listing", true);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::OK);
    EXPECT_FALSE(response->isFileResponse());

    std::string body = response->getBody();
    EXPECT_TRUE(body.find("file1.txt") != std::string::npos);
    EXPECT_TRUE(body.find("file2.txt") != std::string::npos);
    
    // 清理测试目录
    int ret2 = system("rm -rf /tmp/test_listing");
    (void)ret2; // 忽略返回值警告
}

TEST_F(HttpFileTransferTest, FileServletIndexFileTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::OK);
    EXPECT_TRUE(response->isFileResponse());
    EXPECT_EQ(response->getFilePath(), "/tmp/test_www/index.html");
}

TEST_F(HttpFileTransferTest, FileServletRangeRequestTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/test.txt");
    request->setHeader("Range", "bytes=0-10");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::PARTIAL_CONTENT);
    EXPECT_TRUE(response->isFileResponse());
    EXPECT_EQ(response->getRangeStart(), 0);
    EXPECT_EQ(response->getRangeEnd(), 10);
}

TEST_F(HttpFileTransferTest, FileServletInvalidRangeTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/test.txt");
    request->setHeader("Range", "bytes=1000-2000"); // 超出文件大小

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::RANGE_NOT_SATISFIABLE);
}

TEST_F(HttpFileTransferTest, FileServletNotFoundTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/nonexistent.txt");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::NOT_FOUND);
}

TEST_F(HttpFileTransferTest, FileServletPathTraversalTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/../../../etc/passwd");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::BAD_REQUEST);
}

TEST_F(HttpFileTransferTest, FileDownloadServletTest)
{
    sylar::http::FileDownloadServlet servlet("/tmp/test_downloads");

    auto request = std::make_shared<sylar::http::HttpRequest>();
    auto response = std::make_shared<sylar::http::HttpResponse>();

    request->setPath("/download.txt");

    int result = servlet.handle(request, response, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::OK);
    EXPECT_TRUE(response->isFileResponse());
    EXPECT_EQ(response->getFilePath(), "/tmp/test_downloads/download.txt");

    // 检查是否设置了下载头
    std::string disposition = response->getHeader("Content-Disposition");
    EXPECT_TRUE(disposition.find("attachment") != std::string::npos);
}

TEST_F(HttpFileTransferTest, HttpResponseFileMethodsTest)
{
    auto response = std::make_shared<sylar::http::HttpResponse>();

    // 测试setFile方法
    response->setFile("/tmp/test_www/test.txt");
    EXPECT_TRUE(response->isFileResponse());
    EXPECT_EQ(response->getFilePath(), "/tmp/test_www/test.txt");
    EXPECT_GT(response->getFileSize(), 0);

    // 测试setFileRange方法
    response->setFileRange("/tmp/test_www/test.txt", 5, 15);
    EXPECT_EQ(response->getRangeStart(), 5);
    EXPECT_EQ(response->getRangeEnd(), 15);

    // 测试setFileDownload方法
    response->setFileDownload("/tmp/test_www/test.txt");
    std::string disposition = response->getHeader("Content-Disposition");
    EXPECT_TRUE(disposition.find("attachment") != std::string::npos);
}

// 性能测试
TEST_F(HttpFileTransferTest, PerformanceTest)
{
    sylar::http::FileServlet servlet("/tmp/test_www", false);

    auto start = std::chrono::high_resolution_clock::now();

    // 处理100个请求
    for (int i = 0; i < 100; ++i) {
        auto request = std::make_shared<sylar::http::HttpRequest>();
        auto response = std::make_shared<sylar::http::HttpResponse>();

        request->setPath("/large.txt");
        servlet.handle(request, response, nullptr);

        EXPECT_EQ(response->getStatus(), sylar::http::HttpStatus::OK);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    SYLAR_LOG_INFO(g_logger) << "Processed 100 file requests in " << duration.count() << "ms";

    // 性能要求：100个请求应该在1秒内完成
    EXPECT_LT(duration.count(), 1000);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}