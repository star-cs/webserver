#pragma once

#include "sylar/net/bytearray.h"
#include "dynamic_table.h"

namespace sylar
{
namespace http2
{

    /**
     * @brief HPACK 头部字段索引类型枚举
     * @details 定义了 HTTP/2 中头部字段的不同编码方式和索引策略
     */
    enum class IndexType {
        INDEXED = 0,                       ///< 索引字段 - 使用静态/动态表中的索引
        WITH_INDEXING_INDEXED_NAME = 1,    ///< 带索引的索引名称字段
        WITH_INDEXING_NEW_NAME = 2,        ///< 带索引的新名称字段
        WITHOUT_INDEXING_INDEXED_NAME = 3, ///< 不带索引的索引名称字段
        WITHOUT_INDEXING_NEW_NAME = 4,     ///< 不带索引的新名称字段
        NERVER_INDEXED_INDEXED_NAME = 5,   ///< 永不索引的索引名称字段
        NERVER_INDEXED_NEW_NAME = 6,       ///< 永不索引的新名称字段
        ERROR = 7                          ///< 错误类型
    };

    /**
     * @brief 将 IndexType 枚举转换为字符串
     * @param type 索引类型枚举值
     * @return 对应的字符串表示
     */
    std::string IndexTypeToString(IndexType type);

    /**
     * @brief 字符串头部结构
     * @details 表示 HPACK 编码中的字符串头部信息，包含长度和 Huffman 编码标志
     */
    struct StringHeader {
        union {
            struct {
                uint8_t len : 7; ///< 字符串长度（低7位）
                uint8_t h : 1;   ///< Huffman 编码标志位（高1位）
            };
            uint8_t h_len; ///< 组合的头部字节
        };
    };

    /**
     * @brief 字段头部结构
     * @details 表示 HPACK 编码中的字段头部信息，根据不同类型有不同的位分配
     */
    struct FieldHeader {
        union {
            struct {
                uint8_t index : 7; ///< 索引值（低7位）
                uint8_t code : 1;  ///< 编码类型标志（高1位）
            } indexed;
            struct {
                uint8_t index : 6; ///< 索引值（低6位）
                uint8_t code : 2;  ///< 编码类型标志（高2位）
            } with_indexing;
            struct {
                uint8_t index : 4; ///< 索引值（低4位）
                uint8_t code : 4;  ///< 编码类型标志（高4位）
            } other;
            uint8_t type = 0; ///< 完整的类型字节
        };
    };

    /**
     * @brief HTTP 头部字段结构
     * @details 表示一个完整的 HTTP 头部字段，包含类型、名称、值等信息
     */
    struct HeaderField {
        IndexType type = IndexType::ERROR; ///< 头部字段类型
        bool h_name = 0;                   ///< 名称是否使用 Huffman 编码
        bool h_value = 0;                  ///< 值是否使用 Huffman 编码
        uint32_t index = 0;                ///< 索引值
        std::string name;                  ///< 头部字段名
        std::string value;                 ///< 头部字段值

        /**
         * @brief 转换为字符串表示
         * @return 头部字段的字符串表示
         */
        std::string toString() const;
    };

    /**
     * @brief HPACK 压缩算法实现类
     * @details 提供 HTTP/2 头部压缩和解压缩功能，处理头部字段的编码和解码
     */
    class HPack
    {
    public:
        typedef std::shared_ptr<HPack> ptr; ///< 智能指针类型定义

        /**
         * @brief 构造函数
         * @param table 动态表引用，用于存储和检索头部字段
         */
        HPack(DynamicTable &table);

        /**
         * @brief 解析 HPACK 压缩的头部数据
         * @param ba 字节数组指针，包含要解析的数据
         * @param length 要解析的数据长度
         * @return 解析的字节数，失败返回负数
         */
        int parse(ByteArray::ptr ba, int length);

        /**
         * @brief 解析 HPACK 压缩的头部数据（字符串版本）
         * @param data 包含要解析的数据的字符串
         * @return 解析的字节数，失败返回负数
         */
        int parse(std::string &data);

        /**
         * @brief 将单个头部字段打包为 HPACK 格式
         * @param header 要打包的头部字段指针
         * @param ba 输出字节数组指针
         * @return 打包的字节数，失败返回负数
         */
        int pack(HeaderField *header, ByteArray::ptr ba);

        /**
         * @brief 将头部字段列表打包为 HPACK 格式
         * @param headers 要打包的头部字段列表
         * @param ba 输出字节数组指针
         * @return 打包的字节数，失败返回负数
         */
        int pack(const std::vector<std::pair<std::string, std::string> > &headers,
                 ByteArray::ptr ba);

        /**
         * @brief 将头部字段列表打包为 HPACK 格式（字符串版本）
         * @param headers 要打包的头部字段列表
         * @param out 输出字符串引用
         * @return 打包的字节数，失败返回负数
         */
        int pack(const std::vector<std::pair<std::string, std::string> > &headers,
                 std::string &out);

        /**
         * @brief 获取解析出的头部字段列表
         * @return 头部字段列表的引用
         */
        std::vector<HeaderField> &getHeaders() { return m_headers; }

        /**
         * @brief 静态方法：打包单个头部字段
         * @param header 要打包的头部字段指针
         * @param ba 输出字节数组指针
         * @return 打包的字节数，失败返回负数
         */
        static int Pack(HeaderField *header, ByteArray::ptr ba);

        /**
         * @brief 转换为字符串表示
         * @return HPack 对象的字符串表示
         */
        std::string toString() const;

    public:
        /**
         * @brief 写入可变长度整数
         * @param ba 输出字节数组指针
         * @param prefix 前缀长度（位数）
         * @param value 要写入的值
         * @param flags 标志位
         * @return 写入的字节数，失败返回负数
         */
        static int WriteVarInt(ByteArray::ptr ba, int32_t prefix, uint64_t value, uint8_t flags);

        /**
         * @brief 读取可变长度整数
         * @param ba 输入字节数组指针
         * @param prefix 前缀长度（位数）
         * @return 读取的整数值
         */
        static uint64_t ReadVarInt(ByteArray::ptr ba, int32_t prefix);

        /**
         * @brief 读取可变长度整数（带初始字节）
         * @param ba 输入字节数组指针
         * @param b 初始字节
         * @param prefix 前缀长度（位数）
         * @return 读取的整数值
         */
        static uint64_t ReadVarInt(ByteArray::ptr ba, uint8_t b, int32_t prefix);

        /**
         * @brief 读取字符串
         * @param ba 输入字节数组指针
         * @return 读取的字符串
         */
        static std::string ReadString(ByteArray::ptr ba);

        /**
         * @brief 写入字符串
         * @param ba 输出字节数组指针
         * @param str 要写入的字符串
         * @param h 是否使用 Huffman 编码
         * @return 写入的字节数，失败返回负数
         */
        static int WriteString(ByteArray::ptr ba, const std::string &str, bool h);

    private:
        std::vector<HeaderField> m_headers; ///< 解析出的头部字段列表
        DynamicTable &m_table; ///< 动态表引用，用于头部字段的存储和检索
    };

} // namespace http2
} // namespace sylar
