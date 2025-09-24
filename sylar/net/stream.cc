#include "stream.h"
#include "sylar/core/log/log.h"
#include "sylar/core/config/config.h"

namespace sylar
{
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static sylar::ConfigVar<int32_t>::ptr g_socket_buff_size =
    sylar::Config::Lookup("socket.buff_size", (int32_t)(1024 * 16), "socket buff size");

int Stream::readFixSize(void *buffer, size_t length)
{
    size_t offset = 0;
    int64_t left = length;
    static const int64_t MAX_LEN = g_socket_buff_size->getValue();
    while (left > 0) {
        int64_t len = read((char *)buffer + offset, std::min(left, MAX_LEN));
        if (len <= 0) {
            // 区分不同类型的错误
            if (len == 0) {
                // 对端关闭连接
                SYLAR_LOG_DEBUG(g_logger)
                    << "readFixSize connection closed by peer length=" << length;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 资源暂时不可用，非致命错误
                SYLAR_LOG_DEBUG(g_logger)
                    << "readFixSize would block length=" << length << " errno=" << errno;
            } else if (errno == EBADF) {
                // 文件描述符无效，严重错误
                SYLAR_LOG_ERROR(g_logger) << "readFixSize bad file descriptor length=" << length
                                          << " errno=" << errno << " errstr=" << strerror(errno);
            } else {
                // 其他错误
                SYLAR_LOG_ERROR(g_logger) << "readFixSize fail length=" << length << " len=" << len
                                          << " errno=" << errno << " errstr=" << strerror(errno);
            }
            return len;
        }
        offset += len;
        left -= len;
    }
    return length;
}

int Stream::readFixSize(ByteArray::ptr ba, size_t length)
{
    int64_t left = length;
    static const int64_t MAX_LEN = g_socket_buff_size->getValue();
    while (left > 0) {
        int64_t len = read(ba, std::min(left, MAX_LEN));
        if (len <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "readFixSize fail length=" << length << " len=" << len
                                      << " errno=" << errno << " errstr=" << strerror(errno);
            return len;
        }
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(const void *buffer, size_t length)
{
    size_t offset = 0;
    int64_t left = length;
    static const int64_t MAX_LEN = g_socket_buff_size->getValue();
    while (left > 0) {
        int64_t len = write((const char *)buffer + offset, std::min(left, MAX_LEN));
        // int64_t len = write((const char*)buffer + offset, left);
        if (len <= 0) {
            // 区分不同类型的错误
            if (len == 0) {
                // 对端关闭连接
                SYLAR_LOG_DEBUG(g_logger)
                    << "writeFixSize connection closed by peer length=" << length
                    << " left=" << left;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 资源暂时不可用，非致命错误
                SYLAR_LOG_DEBUG(g_logger) << "writeFixSize would block length=" << length
                                          << " left=" << left << " errno=" << errno;
            } else if (errno == EBADF) {
                // 文件描述符无效，严重错误
                SYLAR_LOG_ERROR(g_logger)
                    << "writeFixSize bad file descriptor length=" << length << " left=" << left
                    << " errno=" << errno << " errstr=" << strerror(errno);
            } else if (errno == EPIPE) {
                // 管道破裂，对端关闭连接
                SYLAR_LOG_DEBUG(g_logger) << "writeFixSize broken pipe length=" << length
                                          << " left=" << left << " errno=" << errno;
            } else {
                // 其他错误
                SYLAR_LOG_ERROR(g_logger)
                    << "writeFixSize fail length=" << length << " len=" << len << " left=" << left
                    << " errno=" << errno << ", " << strerror(errno);
            }
            return len;
        }
        offset += len;
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(ByteArray::ptr ba, size_t length)
{
    int64_t left = length;
    while (left > 0) {
        static const int64_t MAX_LEN = g_socket_buff_size->getValue();
        int64_t len = write(ba, std::min(left, MAX_LEN));
        if (len <= 0) {
            SYLAR_LOG_ERROR(g_logger) << "writeFixSize fail length=" << length << " len=" << len
                                      << " errno=" << errno << ", " << strerror(errno);
            return len;
        }
        left -= len;
    }
    return length;
}

} // namespace sylar