#include "hpack.h"
#include "sylar/core/log/log.h"
#include "sylar/net/bytearray.h"
#include "huffman.h"

namespace sylar::http2
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 索引类型字符串映射表，用于将索引类型枚举转换为可读字符串
static std::vector<std::string> s_index_type_strings = {"INDEXED",
                                                        "WITH_INDEXING_INDEXED_NAME",
                                                        "WITH_INDEXING_NEW_NAME",
                                                        "WITHOUT_INDEXING_INDEXED_NAME",
                                                        "WITHOUT_INDEXING_NEW_NAME",
                                                        "NERVER_INDEXED_INDEXED_NAME",
                                                        "NERVER_INDEXED_NEW_NAME",
                                                        "ERROR"};

/**
 * @brief 将索引类型枚举转换为可读字符串
 * @param type 索引类型枚举值
 * @return 对应索引类型的字符串表示
 */
std::string IndexTypeToString(IndexType type)
{
    uint8_t v = (uint8_t)type;
    if (v <= 8) {
        return s_index_type_strings[v];
    }
    return "UNKNOW(" + std::to_string((uint32_t)v) + ")";
}

/**
 * @brief HeaderField类的字符串表示方法
 * @return HeaderField对象的可读字符串表示
 */
std::string HeaderField::toString() const
{
    std::stringstream ss;
    ss << "[header type=" << IndexTypeToString(type) << " h_name=" << h_name
       << " h_value=" << h_value << " index=" << index << " name=" << name << " value=" << value
       << "]";
    return ss.str();
}

/**
 * @brief HPack类的构造函数
 * @param table 动态表引用，用于存储和查找头部字段
 */
HPack::HPack(DynamicTable &table) : m_table(table)
{
}

/**
 * @brief 写入可变长度整数
 * @param ba 字节数组指针，用于存储数据
 * @param prefix 前缀长度，表示第一个字节中用于存储值的位数
 * @param value 要写入的整数值
 * @param flags 第一个字节中的标志位
 * @return 写入的字节数
 */
int HPack::WriteVarInt(ByteArray::ptr ba, int32_t prefix, uint64_t value, uint8_t flags)
{
    size_t pos = ba->getPosition();
    // 计算前缀能表示的最大值
    uint64_t v = (1 << prefix) - 1;
    // 如果值小于前缀能表示的最大值，直接在第一个字节中存储
    if (value < v) {
        ba->writeFuint8(value | flags);
        return 1;
    }
    // 否则先写入前缀的最大值和标志位
    ba->writeFuint8(v | flags);
    value -= v;
    // 然后用扩展字节存储剩余部分，每个字节最高位为1表示后续还有字节
    while (value >= 128) {
        ba->writeFuint8((0x8 | (value & 0x7f)));
        value >>= 7;
    }
    // 最后一个字节最高位为0
    ba->writeFuint8(value);
    return ba->getPosition() - pos;
}

/**
 * @brief 读取可变长度整数（从字节数组中读取第一个字节）
 * @param ba 字节数组指针，从中读取数据
 * @param prefix 前缀长度，表示第一个字节中用于存储值的位数
 * @return 读取的整数值
 */
uint64_t HPack::ReadVarInt(ByteArray::ptr ba, int32_t prefix)
{
    // TODO check prefix in (1, 8)
    uint8_t b = ba->readFuint8();
    uint8_t v = (1 << prefix) - 1;
    // 提取前缀部分的值
    b &= v;
    // 如果值小于前缀能表示的最大值，直接返回
    if (b < v) {
        return b;
    }
    // 否则需要读取扩展字节
    uint64_t iv = b;
    int m = 0;
    do {
        b = ba->readFuint8();
        iv += ((uint64_t)(b & 0x7F)) << m;
        m += 7;
        // TODO check m >= 63
    } while (b & 0x80);  // 最高位为1表示后续还有字节
    return iv;
}

/**
 * @brief 读取可变长度整数（使用已读取的第一个字节）
 * @param ba 字节数组指针，从中读取数据
 * @param b 已读取的第一个字节
 * @param prefix 前缀长度，表示第一个字节中用于存储值的位数
 * @return 读取的整数值
 */
uint64_t HPack::ReadVarInt(ByteArray::ptr ba, uint8_t b, int32_t prefix)
{
    // TODO check prefix in (1, 8)
    uint8_t v = (1 << prefix) - 1;
    // 提取前缀部分的值
    b &= v;
    // 如果值小于前缀能表示的最大值，直接返回
    if (b < v) {
        return b;
    }
    // 否则需要读取扩展字节
    uint64_t iv = b;
    int m = 0;
    do {
        b = ba->readFuint8();
        iv += ((uint64_t)(b & 0x7F)) << m;
        m += 7;
        // TODO check m >= 63
    } while (b & 0x80);  // 最高位为1表示后续还有字节
    return iv;
}

/**
 * @brief 从字节数组中读取字符串（支持Huffman编码）
 * @param ba 字节数组指针，从中读取数据
 * @return 读取的字符串
 */
std::string HPack::ReadString(ByteArray::ptr ba)
{
    // 读取第一个字节，包含字符串长度信息和Huffman编码标志
    uint8_t type = ba->readFuint8();
    // 读取字符串长度
    int len = ReadVarInt(ba, type, 7);
    std::string data;
    if (len) {
        data.resize(len);
        // 读取字符串数据
        ba->read(&data[0], len);
        // 如果最高位为1，表示使用Huffman编码，需要解码
        if (type & 0x80) {
            std::string out;
            Huffman::DecodeString(data, out);
            return out;
        }
    }
    return data;
}

/**
 * @brief 将字符串写入字节数组（支持Huffman编码）
 * @param ba 字节数组指针，用于存储数据
 * @param str 要写入的字符串
 * @param h 是否使用Huffman编码
 * @return 写入的字节数
 */
int HPack::WriteString(ByteArray::ptr ba, const std::string &str, bool h)
{
    int pos = ba->getPosition();
    if (h) {
        // 使用Huffman编码
        std::string new_str;
        Huffman::EncodeString(str, new_str, 0);
        // 写入编码后的字符串长度，设置最高位为1表示使用Huffman编码
        WriteVarInt(ba, 7, new_str.length(), 0x80);
        ba->write(new_str.c_str(), new_str.length());
    } else {
        // 不使用Huffman编码，直接写入原始字符串
        WriteVarInt(ba, 7, str.length(), 0);
        ba->write(str.c_str(), str.length());
    }
    return ba->getPosition() - pos;
}

/**
 * @brief 解析HPack编码的头部字段（字符串版本）
 * @param data HPack编码的头部字段数据
 * @return 解析的字节数，负数表示解析失败
 */
int HPack::parse(std::string &data)
{
    ByteArray::ptr ba(new sylar::ByteArray(&data[0], data.size(), false));
    return parse(ba, data.size());
}

/**
 * @brief 解析HPack编码的头部字段（字节数组版本）
 * @param ba 字节数组指针，包含HPack编码的头部字段数据
 * @param length 要解析的数据长度
 * @return 解析的字节数，负数表示解析失败
 */
int HPack::parse(ByteArray::ptr ba, int length)
{
    int parsed = 0;
    int pos = ba->getPosition();
    // 循环解析直到所有数据解析完成
    while (parsed < length) {
        HeaderField header;
        // 读取头部类型字节
        uint8_t type = ba->readFuint8();
        if (type & 0x80) {
            // 索引字段类型 (Indexed Header Field)
            uint32_t idx = ReadVarInt(ba, type, 7);
            header.type = IndexType::INDEXED;
            header.index = idx;
        } else {
            if (type & 0x40) {
                // 带索引的头部字段，使用索引的名称 (Literal Header Field with Incremental Indexing - Indexed Name)
                uint32_t idx = ReadVarInt(ba, type, 6);
                // idx == 0  / idx > 0 两种格式
                header.type = idx > 0 ? IndexType::WITH_INDEXING_INDEXED_NAME
                                      : IndexType::WITH_INDEXING_NEW_NAME;
                header.index = idx;
            } else if ((type & 0xF0) == 0) {
                // 不带索引的头部字段，使用索引的名称 (Literal Header Field without Indexing - Indexed Name)
                uint32_t idx = ReadVarInt(ba, type, 4);
                header.type = idx > 0 ? IndexType::WITHOUT_INDEXING_INDEXED_NAME
                                      : IndexType::WITHOUT_INDEXING_NEW_NAME;
                header.index = idx;
            } else if (type & 0x10) {
                // 永不索引的头部字段，使用索引的名称 (Literal Header Field Never Indexed - Indexed Name)
                uint32_t idx = ReadVarInt(ba, type, 4);
                header.type = idx > 0 ? IndexType::NERVER_INDEXED_INDEXED_NAME
                                      : IndexType::NERVER_INDEXED_NEW_NAME;
                header.index = idx;
            } else {
                // 无法识别的头部类型
                return -1;
            }

            // 根据索引值读取头部字段的名称和值
            if (header.index > 0) {
                // 使用索引的名称，只需要读取值
                header.value = ReadString(ba);
            } else {
                // 新名称，需要读取名称和值
                header.name = ReadString(ba);
                header.value = ReadString(ba);
            }
        }
        // 根据头部类型，从动态表中获取完整的头部字段信息
        if (header.type == IndexType::INDEXED) {
            // 索引字段，从表中获取完整的名称和值
            auto p = m_table.getPair(header.index);
            header.name = p.first;
            header.value = p.second;
        } else if (header.index > 0) {
            // 使用索引的名称，从表中获取名称
            auto p = m_table.getPair(header.index);
            header.name = p.first;
        }
        // 根据头部类型，决定是否更新动态表
        if (header.type == IndexType::WITH_INDEXING_INDEXED_NAME) {
            // 带索引的头部字段，使用索引的名称，更新动态表
            m_table.update(m_table.getName(header.index), header.value);
        } else if (header.type == IndexType::WITH_INDEXING_NEW_NAME) {
            // 带索引的头部字段，使用新名称，更新动态表
            m_table.update(header.name, header.value);
        }
        // 将解析出的头部字段添加到结果列表
        m_headers.emplace_back(std::move(header));
        parsed = ba->getPosition() - pos;
    }
    // 以下代码被注释掉，原先是在解析完所有头部字段后再更新动态表
    // for(auto& header : m_headers) {
    //     if(header.type == IndexType::WITH_INDEXING_INDEXED_NAME) {
    //         m_table.update(m_table.getName(header.index), header.value);
    //     } else if(header.type == IndexType::WITH_INDEXING_NEW_NAME) {
    //         m_table.update(header.name, header.value);
    //     }
    // }
    return parsed;
}

/**
 * @brief 将单个头部字段打包为HPack格式（内部实现，不添加到头部列表）
 * @param header 头部字段指针
 * @param ba 字节数组指针，用于存储打包后的数据
 * @return 打包的字节数
 */
int HPack::Pack(HeaderField *header, ByteArray::ptr ba)
{
    int pos = ba->getPosition();

    if (header->type == IndexType::INDEXED) {
        // 索引字段，直接写入索引值
        WriteVarInt(ba, 7, header->index, 0x80);
    } else if (header->type == IndexType::WITH_INDEXING_INDEXED_NAME) {
        // 带索引的头部字段，使用索引的名称
        WriteVarInt(ba, 6, header->index, 0x40);
        WriteString(ba, header->value, header->h_value);
    } else if (header->type == IndexType::WITH_INDEXING_NEW_NAME) {
        // 带索引的头部字段，使用新名称
        WriteVarInt(ba, 6, header->index, 0x40);
        WriteString(ba, header->name, header->h_name);
        WriteString(ba, header->value, header->h_value);
    } else if (header->type == IndexType::WITHOUT_INDEXING_INDEXED_NAME) {
        // 不带索引的头部字段，使用索引的名称
        WriteVarInt(ba, 4, header->index, 0x00);
        WriteString(ba, header->value, header->h_value);
    } else if (header->type == IndexType::WITHOUT_INDEXING_NEW_NAME) {
        // 不带索引的头部字段，使用新名称
        WriteVarInt(ba, 4, header->index, 0x00);
        WriteString(ba, header->name, header->h_name);
        WriteString(ba, header->value, header->h_value);
    } else if (header->type == IndexType::NERVER_INDEXED_INDEXED_NAME) {
        // 永不索引的头部字段，使用索引的名称
        WriteVarInt(ba, 4, header->index, 0x10);
        WriteString(ba, header->value, header->h_value);
    } else if (header->type == IndexType::NERVER_INDEXED_NEW_NAME) {
        // 永不索引的头部字段，使用新名称
        WriteVarInt(ba, 4, header->index, 0x10);
        WriteString(ba, header->name, header->h_name);
        WriteString(ba, header->value, header->h_value);
    }
    return ba->getPosition() - pos;
}

/**
 * @brief 将单个头部字段打包为HPack格式并添加到头部列表
 * @param header 头部字段指针
 * @param ba 字节数组指针，用于存储打包后的数据
 * @return 打包的字节数
 */
int HPack::pack(HeaderField *header, ByteArray::ptr ba)
{
    // 将头部字段添加到头部列表
    m_headers.push_back(*header);
    // 调用内部打包函数
    return Pack(header, ba);
}

/**
 * @brief 将头部字段列表打包为HPack格式（字符串版本）
 * @param headers 头部字段列表
 * @param out 输出字符串，用于存储打包后的数据
 * @return 打包的字节数
 */
int HPack::pack(const std::vector<std::pair<std::string, std::string> > &headers, std::string &out)
{
    ByteArray::ptr ba(new ByteArray);
    int rt = pack(headers, ba);
    ba->setPosition(0);
    ba->toString().swap(out);
    return rt;
}

/**
 * @brief 将头部字段列表打包为HPack格式（字节数组版本）
 * @param headers 头部字段列表
 * @param ba 字节数组指针，用于存储打包后的数据
 * @return 打包的字节数
 */
int HPack::pack(const std::vector<std::pair<std::string, std::string> > &headers, ByteArray::ptr ba)
{
    int rt = 0;
    // 遍历所有头部字段
    for (auto &i : headers) {
        HeaderField h;
        // 在动态表中查找头部字段
        auto p = m_table.findPair(i.first, i.second);
        if (p.second) {
            // 头部字段完全匹配，使用索引表示
            h.type = IndexType::INDEXED;
            h.index = p.first;
        } else if (p.first > 0) {
            // 头部字段名称匹配，使用索引的名称
            h.type = IndexType::WITH_INDEXING_INDEXED_NAME;
            h.index = p.first;
            h.h_value = Huffman::ShouldEncode(i.second);
            h.name = i.first;
            h.value = i.second;
            // 更新动态表
            m_table.update(h.name, h.value);
        } else {
            // 头部字段在表中不存在，使用新名称
            h.type = IndexType::WITH_INDEXING_NEW_NAME;
            h.index = 0;
            h.h_name = Huffman::ShouldEncode(i.first);
            h.name = i.first;
            h.h_value = Huffman::ShouldEncode(i.second);
            h.value = i.second;
            // 更新动态表
            m_table.update(h.name, h.value);
        }

        // 打包头部字段
        rt += pack(&h, ba);
    }
    return rt;
}

/**
 * @brief HPack对象的字符串表示方法
 * @return HPack对象的可读字符串表示
 */
std::string HPack::toString() const
{
    std::stringstream ss;
    ss << "[HPack size=" << m_headers.size() << "]" << std::endl;
    // 输出所有头部字段信息
    for (size_t i = 0; i < m_headers.size(); ++i) {
        ss << "\t" << i << "\t:\t" << m_headers[i].toString() << std::endl;
    }
    // 输出动态表信息
    ss << m_table.toString();
    return ss.str();
}

} // namespace sylar::http2