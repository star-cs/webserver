#ifndef __SYLAR_HTTP2_HUFFMAN_H__
#define __SYLAR_HTTP2_HUFFMAN_H__

#include <string>

namespace sylar
{
namespace http2
{

/**
 * @brief HTTP/2 Huffman编码工具类
 * 
 * 实现HTTP/2协议中使用的HPACK压缩算法中的Huffman编解码功能
 * 用于HTTP头部字段的高效压缩传输
 */
class Huffman
{
public:
    /**
     * @brief 将输入字符串使用Huffman编码为二进制数据
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @param out 输出缓冲区，用于存储编码后的二进制数据
     * @param prefix 前缀位数，用于处理多字节编码时的位对齐
     * @return 操作结果，0表示成功
     */
    static int EncodeString(const char *in, int in_len, std::string &out, int prefix);
    
    /**
     * @brief 将输入字符串使用Huffman编码为二进制数据（重载版本）
     * @param in 输入字符串
     * @param out 输出缓冲区
     * @param prefix 前缀位数
     * @return 操作结果，0表示成功
     */
    static int EncodeString(const std::string &in, std::string &out, int prefix);
    
    /**
     * @brief 将Huffman编码的二进制数据解码为字符串
     * @param in 输入的编码数据
     * @param in_len 输入数据长度
     * @param out 输出缓冲区，用于存储解码后的字符串
     * @return 解码后的字符串长度，负数表示错误
     */
    static int DecodeString(const char *in, int in_len, std::string &out);
    
    /**
     * @brief 将Huffman编码的二进制数据解码为字符串（重载版本）
     * @param in 输入的编码数据
     * @param out 输出缓冲区
     * @return 解码后的字符串长度，负数表示错误
     */
    static int DecodeString(const std::string &in, std::string &out);
    
    /**
     * @brief 计算字符串经过Huffman编码后的字节长度
     * @param in 输入字符串
     * @return 编码后的字节长度
     */
    static int EncodeLen(const std::string &in);
    
    /**
     * @brief 计算字符串经过Huffman编码后的字节长度
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @return 编码后的字节长度
     */
    static int EncodeLen(const char *in, int in_len);

    /**
     * @brief 判断字符串是否值得进行Huffman编码（编码后是否会变小）
     * @param in 输入字符串
     * @return true表示编码后会变小，应该进行编码；false表示不应该进行编码
     */
    static bool ShouldEncode(const std::string &in);
    
    /**
     * @brief 判断字符串是否值得进行Huffman编码
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @return true表示编码后会变小，应该进行编码；false表示不应该进行编码
     */
    static bool ShouldEncode(const char *in, int in_len);
};

// void testHuffman();

} // namespace http2
} // namespace sylar

#endif
