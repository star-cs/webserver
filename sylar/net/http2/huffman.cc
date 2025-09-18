#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "huffman.h"
#include "huffman_table.h"

#include <iostream>

/**
 * @brief 将十六进制值转换为Huffman编码
 */
#define HEX_TO_HF_CODE(hex) huffman_codes[hex]

/**
 * @brief 将十六进制值转换为Huffman编码长度
 */
#define HEX_TO_HF_CODE_LEN(hex) huffman_code_len[hex]

/**
 * @brief 计算Huffman编码的最大缓冲区长度
 * @param l 输入字符串的长度
 * @return 所需的最大缓冲区长度（字节）
 */
#define SYLAR_MAX_HUFFMAN_BUFF_LEN(l) (l * 8 + 1)

namespace sylar
{
namespace http2
{

    /**
     * @brief Huffman操作返回值枚举
     */
    enum HM_RETURN {
        HM_RETURN_SUCCESS = 0,        // 操作成功
        HM_RETURN_UNIMPLEMENT = -100, // 未实现的功能
    };

    /**
     * @brief Huffman树节点结构
     */
    struct node {
        struct node *children[256]; // 子节点数组
        unsigned char sym;          // 节点表示的字符
        unsigned int code;          // 节点的Huffman编码
        int code_len;               // 编码长度（位数）
        int size;                   // 子节点数量，0表示叶子节点
    };

    typedef struct node NODE;      // 节点类型定义
    extern NODE *ROOT;             // 声明Huffman树的根节点

    // 函数声明
    int hf_init(NODE **h_node);    // 初始化Huffman树
    void hf_finish(NODE *h_node);  // 销毁Huffman树
    int hf_byte_encode(unsigned char ch, int remain, unsigned char *buff); // 编码单个字节
    int hf_integer_encode(unsigned int enc_binary, int nprefix, unsigned char *buff); // 编码整数
    int hf_integer_decode(const char *enc_buff, int nprefix, char *dec_buff); // 解码整数
    int hf_string_encode(const char *buff_in, int size, int prefix, unsigned char *buff_out, int *size_out); // 编码字符串
    int hf_string_decode(NODE *h_node, unsigned char *enc, int enc_sz, char *out_buff, int out_sz); // 解码字符串
    void hf_print_hex(unsigned char *buff, int size); // 打印十六进制数据
    int hf_string_encode_len(unsigned char *enc, int enc_sz); // 计算编码后的长度

    /**
     * @brief 创建一个新的Huffman树节点
     * @return 指向新创建节点的指针，失败返回NULL
     */
    static NODE *node_create()
    {
        NODE *nnode = (NODE *)malloc(1 * sizeof(NODE));
        memset(nnode, 0, sizeof(NODE));
        return nnode;
    }

    /**
     * @brief 向Huffman树添加一个节点
     * @param h_node Huffman树的根节点
     * @param sym 字符值
     * @param code Huffman编码值
     * @param code_len 编码长度（位数）
     * @return 操作结果，0表示成功
     */
    static int _hf_add_node(NODE *h_node, unsigned char sym, int code, int code_len)
    {
        NODE *cur = h_node;
        unsigned char i = 0;
        int j = 0;
        int shift = 0;
        int start = 0;
        int end = 0;

        // 处理编码长度大于8位的情况，逐层创建节点
        for (; code_len > 8;) {
            code_len -= 8;
            i = (unsigned char)(code >> code_len);
            if (cur->children[i] == NULL) {
                cur->children[i] = node_create();
            }
            cur = cur->children[i];
        }

        // 处理剩余的编码位
        shift = (8 - code_len);
        start = (unsigned char)(code << shift);
        end = (1 << shift);

        // 创建叶子节点
        for (j = start; j < start + end; j++) {
            if (cur->children[j] == NULL) {
                cur->children[j] = node_create();
            }
            cur->children[j]->code = code;
            cur->children[j]->sym = sym;
            cur->children[j]->code_len = code_len;
            cur->size++;
        }

        return 0;
    }

    /**
     * @brief 递归删除Huffman树节点
     * @param h_node 要删除的节点
     * @return 操作结果，0表示成功
     */
    static int _hf_del_node(NODE *h_node)
    {
        if (h_node) {
            for (int i = 0; i < 256; i++) {
                if (h_node->children[i]) {
                    _hf_del_node(h_node->children[i]);
                }
            }
            free(h_node);
        }
        return 0;
    }

    /**
     * @brief 初始化Huffman树
     * @param h_node 输出参数，指向初始化后的根节点
     * @return 操作结果，0表示成功
     */
    int hf_init(NODE **h_node)
    {
        int i = 0;
        *h_node = node_create();
        // 根据预定义的编码表创建Huffman树
        for (i = 0; i < 256; i++) {
            _hf_add_node(*h_node, i, huffman_codes[i], huffman_code_len[i]);
        }
        return 0;
    }

    /**
     * @brief 销毁Huffman树，释放内存
     * @param h_node 要销毁的根节点
     */
    void hf_finish(NODE *h_node)
    {
        int i = 0;
        for (i = 0; i < 256; i++) {
            _hf_del_node(h_node->children[i]);
        }

        free(h_node);
    }

    /**
     * @brief 编码单个字节到Huffman编码
     * @param ch 要编码的字节
     * @param remain 当前缓冲区中剩余的可用位数
     * @param buff 输出缓冲区
     * @return 编码后剩余的可用位数
     */
    int hf_byte_encode(unsigned char ch, int remain, unsigned char *buff)
    {
        unsigned char t = 0;
        int i = 0;
        int codes = HEX_TO_HF_CODE(ch);
        int nbits = HEX_TO_HF_CODE_LEN(ch);
        // printf("'%c'|codes(%d)|len(%d)\n", ch, codes, nbits );
        for (;;) {
            if (remain > nbits) {
                t = (unsigned char)(codes << (remain - nbits));
                buff[i++] |= t;
                return remain - nbits;
            } else {
                t = (unsigned char)(codes >> (nbits - remain));
                buff[i++] |= t;
                nbits -= remain;
                remain = 8;
            }
            buff[i] = 0;
            if (nbits == 0) {
                return remain;
            }
        }
    }

    /**
     * @brief 编码字符串到Huffman编码
     * @param buff_in 输入字符串
     * @param size 输入字符串长度
     * @param prefix 前缀位数
     * @param buff_out 输出缓冲区
     * @param size_out 输出参数，编码后的字节数
     * @return 操作结果，0表示成功
     */
    int hf_string_encode(const char *buff_in, int size, int prefix, unsigned char *buff_out, int *size_out)
    {
        int i = 0;
        int remain = (8 - prefix);
        int j = 0; // j是当前buff_out的索引，也是编码完成后的大小
        int nbytes = 0;

        for (i = 0; i < size; i++) {

            // 计算需要的字节数
            if (remain > HEX_TO_HF_CODE_LEN((uint8_t)buff_in[i])) {
                nbytes = (remain - HEX_TO_HF_CODE_LEN((uint8_t)buff_in[i])) / 8;
            } else {
                nbytes = ((HEX_TO_HF_CODE_LEN((uint8_t)buff_in[i]) - remain) / 8) + 1;
            }
            remain = hf_byte_encode(buff_in[i], remain, &buff_out[j]);
            j += nbytes;
        }

        // 添加特殊的EOS符号（End Of String）
        if (remain < 8) {
            unsigned int codes = 0x3fffffff;
            int nbits = (char)30;
            buff_out[j++] |= (unsigned char)(codes >> (nbits - remain));
        }

        *size_out = j;
        return 0;
    }

    /**
     * @brief 解码Huffman编码的字符串
     * @param h_node Huffman树的根节点
     * @param enc 编码的数据
     * @param enc_sz 编码数据的大小
     * @param out_buff 输出缓冲区
     * @param out_sz 输出缓冲区的大小
     * @return 解码后的字符串长度，负数表示错误
     */
    int hf_string_decode(NODE *h_node, unsigned char *enc, int enc_sz, char *out_buff, int out_sz)
    {
        NODE *n = h_node;
        unsigned int cur = 0;
        int nbits = 0;
        int i = 0;
        int idx = 0;
        int at = 0;
        for (i = 0; i < enc_sz; i++) {
            cur = (cur << 8) | enc[i];
            nbits += 8;
            for (; nbits >= 8;) {
                idx = (unsigned char)(cur >> (nbits - 8));
                n = n->children[idx];
                if (n == NULL) {
                    printf("invalid huffmand code\n");
                    return -1; // 无效的Huffman编码
                }
                // printf("n->sym : %c , n->size = %d\n", n->sym, n->size);
                // if( n->children == NULL){
                if (n->size == 0) {
                    if (out_sz > 0 && at > out_sz) {
                        printf("out of length\n");
                        return -2; // 输出缓冲区溢出
                    }
                    out_buff[at++] = (char)n->sym;
                    nbits -= n->code_len;
                    n = h_node;
                } else {
                    nbits -= 8;
                }
            }
        }

        // 处理剩余的位
        for (; nbits > 0;) {
            n = n->children[(unsigned char)(cur << (8 - nbits))];
            if (n->size != 0 || n->code_len > nbits) {
                break;
            }

            out_buff[at++] = (char)n->sym;
            nbits -= n->code_len;
            n = h_node;
        }

        return at;
    }

    /**
     * @brief 编码整数为HPACK整数表示形式
     * @param enc_binary 要编码的整数
     * @param nprefix 前缀位数
     * @param buff 输出缓冲区
     * @return 编码后的字节数
     */
    int hf_integer_encode(unsigned int enc_binary, int nprefix, unsigned char *buff)
    {
        int i = 0;
        unsigned int ch = enc_binary;
        unsigned int ch2 = 0;
        unsigned int prefix = (1 << nprefix) - 1;

        if (ch < prefix && (ch < 0xff)) {
            buff[i++] = ch & prefix;
        } else {
            buff[i++] = prefix;
            ch -= prefix;
            while (ch > 128) {
                ch2 = (ch % 128);
                ch2 += 128;
                buff[i++] = ch2;
                ch = ch / 128;
            }
            buff[i++] = ch;
        }
        return i;
    }

    /**
     * @brief 解码HPACK整数表示形式为整数
     * @param enc_buff 编码的数据
     * @param nprefix 前缀位数
     * @param dec_buff 解码后的整数
     * @return 解码消耗的字节数
     */
    int hf_integer_decode(const char *enc_buff, int nprefix, char *dec_buff)
    {
        int i = 0;
        int j = 0;
        unsigned int M = 0;
        unsigned int B = 0;
        unsigned int ch = enc_buff[i++];
        unsigned int prefix = (1 << nprefix) - 1;

        if (ch < prefix) {
            dec_buff[j++] = ch;
        } else {
            M = 0;
            do {
                B = enc_buff[i++];
                ch = ch + ((B & 127) * (1 << M));
                M = M + 7;
            } while (B & 128);
            dec_buff[j] = ch;
        }
        return i;
    }

    /**
     * @brief 计算字符串编码后的字节长度
     * @param enc 输入字符串
     * @param enc_sz 输入字符串长度
     * @return 编码后的字节长度
     */
    int hf_string_encode_len(unsigned char *enc, int enc_sz)
    {
        int i = 0;
        int len = 0;
        for (i = 0; i < enc_sz; i++) {
            len += huffman_code_len[(int)enc[i]];
        }

        return (len + 7) / 8; // 向上取整到字节
    }

    /**
     * @brief 打印十六进制数据
     * @param buff 要打印的数据
     * @param size 数据大小
     */
    void hf_print_hex(unsigned char *buff, int size)
    {
        static char hex[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        int i = 0;
        for (i = 0; i < size; i++) {
            unsigned char ch = buff[i];
            printf("(%u)%c", ch, hex[(ch >> 4)]);
            printf("%c ", hex[(ch & 0x0f)]);
        }
        printf("\n");
    }

    /**
     * @brief Huffman::EncodeString方法实现
     * @param in 输入字符串
     * @param out 输出缓冲区
     * @param prefix 前缀位数
     * @return 操作结果，0表示成功
     */
    int Huffman::EncodeString(const std::string &in, std::string &out, int prefix)
    {
        return EncodeString(in.c_str(), in.length(), out, prefix);
    }

    /**
     * @brief Huffman::EncodeString方法实现（重载版本）
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @param out 输出缓冲区
     * @param prefix 前缀位数
     * @return 操作结果，0表示成功
     */
    int Huffman::EncodeString(const char *in, int in_len, std::string &out, int prefix)
    {
        int len = SYLAR_MAX_HUFFMAN_BUFF_LEN(in_len);
        out.resize(len);
        int rt = hf_string_encode(in, in_len, prefix, (unsigned char *)&out[0], &len);
        out.resize(len);
        return rt;
    }

    /**
     * @brief Huffman::DecodeString方法实现
     * @param in 输入的编码数据
     * @param out 输出缓冲区
     * @return 解码后的字符串长度，负数表示错误
     */
    int Huffman::DecodeString(const std::string &in, std::string &out)
    {
        return DecodeString(in.c_str(), in.length(), out);
    }

    /**
     * @brief Huffman::DecodeString方法实现（重载版本）
     * @param in 输入的编码数据
     * @param in_len 输入数据长度
     * @param out 输出缓冲区
     * @return 解码后的字符串长度，负数表示错误
     */
    int Huffman::DecodeString(const char *in, int in_len, std::string &out)
    {
        NODE *h_node;
        hf_init(&h_node);
        int len = SYLAR_MAX_HUFFMAN_BUFF_LEN(in_len);
        out.resize(len);
        int rt = hf_string_decode(h_node, (unsigned char *)in, in_len, &out[0], len);
        hf_finish(h_node);
        out.resize(rt);
        return rt;
    }

    /**
     * @brief Huffman::EncodeLen方法实现
     * @param in 输入字符串
     * @return 编码后的字节长度
     */
    int Huffman::EncodeLen(const std::string &in)
    {
        return EncodeLen(in.c_str(), in.length());
    }

    /**
     * @brief Huffman::EncodeLen方法实现（重载版本）
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @return 编码后的字节长度
     */
    int Huffman::EncodeLen(const char *in, int in_len)
    {
        return hf_string_encode_len((uint8_t *)in, in_len);
    }

    /**
     * @brief Huffman::ShouldEncode方法实现
     * @param in 输入字符串
     * @return true表示编码后会变小，应该进行编码；false表示不应该进行编码
     */
    bool Huffman::ShouldEncode(const std::string &in)
    {
        return ShouldEncode(in.c_str(), in.length());
    }

    /**
     * @brief Huffman::ShouldEncode方法实现（重载版本）
     * @param in 输入字符串
     * @param in_len 输入字符串长度
     * @return true表示编码后会变小，应该进行编码；false表示不应该进行编码
     */
    bool Huffman::ShouldEncode(const char *in, int in_len)
    {
        return EncodeLen(in, in_len) < in_len;
    }

    // void testHuffman() {
    //     std::string str = "hello huffman,你好,世界";
    //     std::string out;
    //     Huffman::EncodeString(str, out, 0);
    //     std::cout << "str.size=" << str.size()
    //               << " out.size=" << out.size()
    //               << std::endl;
    //     hf_print_hex((unsigned char*)out.c_str(), out.length());
    //     std::string str2;
    //     Huffman::DecodeString(out, str2);
    //     std::cout << str2 << std::endl;
    //     std::cout << "str.size=" << str.size()
    //               << " out.size=" << out.size() << std::endl;
    //
    //     std::cout << "hf_string_encode_len: " << hf_string_encode_len((uint8_t*)str.c_str(),
    //     str.size()) << std::endl;
    // }

} // namespace http2
} // namespace sylar
