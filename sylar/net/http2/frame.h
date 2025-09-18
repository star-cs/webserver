#ifndef __SYLAR_HTTP2_FRAME_H__
#define __SYLAR_HTTP2_FRAME_H__

#include "sylar/net/bytearray.h"
#include "sylar/net/stream.h"
#include "hpack.h"

namespace sylar
{
namespace http2
{

#pragma pack(push)
#pragma pack(1)

    /**
     * @brief HTTP/2 帧类型枚举
     * @details 定义了 HTTP/2 协议支持的各种帧类型
     */
    enum class FrameType {
        // 数据帧
        DATA = 0x0,     ///< 数据帧
        HEADERS = 0x1,  ///< 头部帧
        PRIORITY = 0x2, ///< 优先级帧

        // 控制帧
        RST_STREAM = 0x3,    ///< 流重置帧
        SETTINGS = 0x4,      ///< 设置帧
        PUSH_PROMISE = 0x5,  ///< 推送承诺帧
        PING = 0x6,          ///< 心跳帧
        GOAWAY = 0x7,        ///< 断开连接帧
        WINDOW_UPDATE = 0x8, ///< 窗口更新帧
        CONTINUATION = 0x9,  ///< 连续帧
    };

    /**
     * @brief DATA 帧的标志枚举
     * @details 定义了 DATA 帧支持的标志位
     */
    enum class FrameFlagData {
        END_STREAM = 0x1, ///< 表示此帧是流的最后一个帧
        PADDED = 0x8      ///< 表示帧包含填充数据
    };

    /**
     * @brief HEADERS 帧的标志枚举
     * @details 定义了 HEADERS 帧支持的标志位
     */
    enum class FrameFlagHeaders {
        END_STREAM = 0x1,  ///< 表示此帧是流的最后一个帧
        END_HEADERS = 0x4, ///< 表示此帧包含头部块的结束
        PADDED = 0x8,      ///< 表示帧包含填充数据
        PRIORITY = 0x20    ///< 表示帧包含优先级信息
    };

    /**
     * @brief SETTINGS 帧的标志枚举
     * @details 定义了 SETTINGS 帧支持的标志位
     */
    enum class FrameFlagSettings {
        ACK = 0x1 ///< 表示这是对 SETTINGS 帧的确认
    };

    /**
     * @brief PING 帧的标志枚举
     * @details 定义了 PING 帧支持的标志位
     */
    enum class FrameFlagPing {
        ACK = 0x1 ///< 表示这是对 PING 帧的响应
    };

    /**
     * @brief CONTINUATION 帧的标志枚举
     * @details 定义了 CONTINUATION 帧支持的标志位
     */
    enum class FrameFlagContinuation {
        END_HEADERS = 0x4 ///< 表示此帧包含头部块的结束
    };

    /**
     * @brief PUSH_PROMISE 帧的标志枚举
     * @details 定义了 PUSH_PROMISE 帧支持的标志位
     */
    enum class FrameFlagPromise {
        END_HEADERS = 0x4, ///< 表示此帧包含头部块的结束
        PADDED = 0x8       ///< 表示帧包含填充数据
    };

    /**
     * @brief 保留位标志枚举
     * @details 定义了帧头部保留位的状态
     */
    enum class FrameR {
        UNSET = 0x0, ///< 未设置
        SET = 0x1,   ///< 已设置
    };

    /*
HTTP2 frame 格式
+-----------------------------------------------+
|                 Length (24)                   |
+---------------+---------------+---------------+
|   Type (8)    |   Flags (8)   |
+-+-------------+---------------+-------------------------------+
|R|                 Stream Identifier (31)                      |
+=+=============================================================+
|                   Frame Payload (0...)                      ...
+---------------------------------------------------------------+
*/

    /**
     * @brief HTTP/2 帧头部结构
     * @details 所有 HTTP/2 帧都以相同的 9 字节头部开始
     */
    struct FrameHeader {
        static const uint32_t SIZE = 9;           ///< 帧头部大小（字节）
        typedef std::shared_ptr<FrameHeader> ptr; ///< 智能指针类型定义
        union {
            struct {
                uint8_t type;         ///< 帧类型（8位）
                uint32_t length : 24; ///< 帧负载长度（24位）
            };
            uint32_t len_type = 0; ///< 长度和类型的联合表示
        };
        uint8_t flags = 0; ///< 帧标志（8位）
        union {
            struct {
                uint32_t identifier : 31; ///< 流标识符（31位）
                uint32_t r : 1;           ///< 保留位（1位）
            };
            uint32_t r_id = 0; ///< 保留位和流ID的联合表示
        };

        std::string toString() const;     ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba); ///< 从字节数组读取
    };

    /**
     * @brief 帧接口类
     * @details 所有 HTTP/2 帧类型的基类
     */
    class IFrame
    {
    public:
        typedef std::shared_ptr<IFrame> ptr; ///< 智能指针类型定义

        virtual ~IFrame() {}                      ///< 虚析构函数
        virtual std::string toString() const = 0; ///< 转换为字符串（纯虚函数）
        virtual bool writeTo(ByteArray::ptr ba,
                             const FrameHeader &header) = 0; ///< 写入到字节数组（纯虚函数）
        virtual bool readFrom(ByteArray::ptr ba,
                              const FrameHeader &header) = 0; ///< 从字节数组读取（纯虚函数）
    };

    /**
     * @brief HTTP/2 帧结构
     * @details 包含帧头部和帧数据
     */
    struct Frame {
        typedef std::shared_ptr<Frame> ptr; ///< 智能指针类型定义
        FrameHeader header;                 ///< 帧头部
        IFrame::ptr data;                   ///< 帧数据（具体类型由帧类型决定）

        std::string toString() const; ///< 转换为字符串
    };

    /*
 +---------------+
 |Pad Length? (8)|
 +---------------+-----------------------------------------------+
 |                            Data (*)                         ...
 +---------------------------------------------------------------+
 |                           Padding (*)                       ...
 +---------------------------------------------------------------+
*/

    /**
     * @brief DATA 帧结构
     * @details 用于传输 HTTP 消息体数据
     */
    struct DataFrame : public IFrame {
        typedef std::shared_ptr<DataFrame> ptr; ///< 智能指针类型定义
        uint8_t pad = 0;                        ///< 填充长度（如果设置了 PADDED 标志）
        std::string data;                       ///< 实际数据内容
        std::string padding;                    ///< 填充数据

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +-+-------------------------------------------------------------+
 |E|                  Stream Dependency (31)                     |
 +-+-------------+-----------------------------------------------+
 |   Weight (8)  |
 +-+-------------+
*/

    /**
     * @brief PRIORITY 帧结构
     * @details 用于指定流的优先级和依赖关系
     */
    struct PriorityFrame : public IFrame {
        typedef std::shared_ptr<PriorityFrame> ptr; ///< 智能指针类型定义
        static const uint32_t SIZE = 5;             ///< 帧大小（字节）
        union {
            struct {
                uint32_t stream_dep : 31; ///< 依赖的流ID（31位）
                uint32_t exclusive : 1;   ///< 是否独占依赖（1位）
            };
            uint32_t e_stream_dep = 0; ///< 依赖信息的联合表示
        };
        uint8_t weight = 0; ///< 流权重（0-255）

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +---------------+
 |Pad Length? (8)|
 +-+-------------+-----------------------------------------------+
 |E|                 Stream Dependency? (31)                     |
 +-+-------------+-----------------------------------------------+
 |  Weight? (8)  |
 +-+-------------+-----------------------------------------------+
 |                   Header Block Fragment (*)                 ...
 +---------------------------------------------------------------+
 |                           Padding (*)                       ...
 +---------------------------------------------------------------+
 */

    /**
     * @brief HEADERS 帧结构
     * @details 用于传输 HTTP 头部字段
     */
    struct HeadersFrame : public IFrame {
        typedef std::shared_ptr<HeadersFrame> ptr; ///< 智能指针类型定义
        uint8_t pad = 0;        ///< 填充长度（如果设置了 PADDED 标志）
        PriorityFrame priority; ///< 优先级信息（如果设置了 PRIORITY 标志）
        std::string data;       ///< 头部块片段数据
        std::string padding;    ///< 填充数据
        HPack::ptr hpack;       ///< HPack 编解码器指针
        std::vector<std::pair<std::string, std::string> > kvs; ///< 解析后的键值对列表

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +---------------------------------------------------------------+
 |                        Error Code (32)                        |
 +---------------------------------------------------------------+
*/

    /**
     * @brief RST_STREAM 帧结构
     * @details 用于立即终止一个流
     */
    struct RstStreamFrame : public IFrame {
        typedef std::shared_ptr<RstStreamFrame> ptr; ///< 智能指针类型定义
        static const uint32_t SIZE = 4;              ///< 帧大小（字节）
        uint32_t error_code = 0;                     ///< 错误码

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +-------------------------------+
 |       Identifier (16)         |
 +-------------------------------+-------------------------------+
 |                        Value (32)                             |
 +---------------------------------------------------------------+
*/

    /**
     * @brief SETTINGS 项结构，每个设置
     * @details 表示 SETTINGS 帧中的一个配置项
     */
    struct SettingsItem {
        SettingsItem(uint16_t id = 0, uint32_t v = 0) : identifier(id), value(v) {} ///< 构造函数
        uint16_t identifier = 0; ///< 设置项标识符
        uint32_t value = 0;      ///< 设置项值

        std::string toString() const;     ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba); ///< 从字节数组读取
    };

    /**
     * @brief SETTINGS 帧结构，首个业务帧
     * @details 用于配置连接级参数
     */
    struct SettingsFrame : public IFrame {
        typedef std::shared_ptr<SettingsFrame> ptr; ///< 智能指针类型定义
        enum class Settings {
            HEADER_TABLE_SIZE = 0x1,      ///< 头部表大小
            ENABLE_PUSH = 0x2,            ///< 启用推送
            MAX_CONCURRENT_STREAMS = 0x3, ///< 最大并发流数量
            INITIAL_WINDOW_SIZE = 0x4,    ///< 初始窗口大小
            MAX_FRAME_SIZE = 0x5,         ///< 最大帧大小
            MAX_HEADER_LIST_SIZE = 0x6    ///< 最大头部列表大小
        };

        static std::string SettingsToString(Settings s); ///< 设置项转换为字符串
        std::string toString() const;                    ///< 转换为字符串

        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取

        std::vector<SettingsItem> items; ///< 设置项列表
    };

    /*
 +---------------+
 |Pad Length? (8)|
 +-+-------------+-----------------------------------------------+
 |R|                  Promised Stream ID (31)                    |
 +-+-----------------------------+-------------------------------+
 |                   Header Block Fragment (*)                 ...
 +---------------------------------------------------------------+
 |                           Padding (*)                       ...
 +---------------------------------------------------------------+
*/

    /**
     * @brief PUSH_PROMISE 帧结构
     * @details 用于服务器推送资源
     */
    struct PushPromisedFrame : public IFrame {
        typedef std::shared_ptr<PushPromisedFrame> ptr; ///< 智能指针类型定义
        uint8_t pad = 0; ///< 填充长度（如果设置了 PADDED 标志）
        union {
            struct {
                uint32_t stream_id : 31; ///< 承诺的流ID（31位）
                uint32_t r : 1;          ///< 保留位（1位）
            };
            uint32_t r_stream_id = 0; ///< 流ID的联合表示
        };
        std::string data;    ///< 头部数据
        std::string padding; ///< 填充数据

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +---------------------------------------------------------------+
 |                                                               |
 |                      Opaque Data (64)                         |
 |                                                               |
 +---------------------------------------------------------------+
*/

    /**
     * @brief PING 帧结构
     * @details 用于测试连接可用性和往返时间
     */
    struct PingFrame : public IFrame {
        typedef std::shared_ptr<PingFrame> ptr; ///< 智能指针类型定义
        static const uint32_t SIZE = 8;         ///< 帧大小（字节）
        union {
            uint8_t data[8];     ///< 8字节的不透明数据
            uint64_t uint64 = 0; ///< 64位整数表示
        };

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*
 +-+-------------------------------------------------------------+
 |R|                  Last-Stream-ID (31)                        |
 +-+-------------------------------------------------------------+
 |                      Error Code (32)                          |
 +---------------------------------------------------------------+
 |                  Additional Debug Data (*)                    |
 +---------------------------------------------------------------+
*/

    /**
     * @brief GOAWAY 帧结构
     * @details 用于发起关闭连接的过程，或者表示严重错误
     */
    struct GoAwayFrame : public IFrame {
        typedef std::shared_ptr<GoAwayFrame> ptr; ///< 智能指针类型定义
        union {
            struct {
                uint32_t last_stream_id : 31; ///< 最后处理的流ID（31位）
                uint32_t r : 1;               ///< 保留位（1位）
            };
            uint32_t r_last_stream_id = 0; ///< 流ID的联合表示
        };
        uint32_t error_code = 0; ///< 错误码
        std::string data;        ///< 附加调试数据

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /*

 +-+-------------------------------------------------------------+
 |R|              Window Size Increment (31)                     |
 +-+-------------------------------------------------------------+
*/

    /**
     * @brief WINDOW_UPDATE 帧结构
     * @details 用于实现流量控制
     */
    struct WindowUpdateFrame : public IFrame {
        typedef std::shared_ptr<WindowUpdateFrame> ptr; ///< 智能指针类型定义
        static const uint32_t SIZE = 4;                 ///< 帧大小（字节）
        union {
            struct {
                uint32_t increment : 31; ///< 窗口大小增量（31位）
                uint32_t r : 1;          ///< 保留位（1位）
            };
            uint32_t r_increment = 0; ///< 增量的联合表示
        };

        std::string toString() const;                                ///< 转换为字符串
        bool writeTo(ByteArray::ptr ba, const FrameHeader &header);  ///< 写入到字节数组
        bool readFrom(ByteArray::ptr ba, const FrameHeader &header); ///< 从字节数组读取
    };

    /**
     * @brief 帧编解码器类
     * @details 提供 HTTP/2 帧的编码和解码功能
     */
    class FrameCodec
    {
    public:
        typedef std::shared_ptr<FrameCodec> ptr; ///< 智能指针类型定义

        /**
         * @brief 从流中解析帧
         * @param stream 数据流指针
         * @return 解析出的帧指针
         */
        Frame::ptr parseFrom(Stream::ptr stream);

        /**
         * @brief 将帧序列化到流中
         * @param stream 数据流指针
         * @param frame 要序列化的帧指针
         * @return 序列化的字节数，<0 表示失败
         */
        int32_t serializeTo(Stream::ptr stream, Frame::ptr frame);
    };

    /**
     * @brief 将帧类型转换为字符串
     * @param type 帧类型
     * @return 帧类型的字符串表示
     */
    std::string FrameTypeToString(FrameType type);

    /**
     * @brief 将 DATA 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagDataToString(FrameFlagData flag);

    /**
     * @brief 将 HEADERS 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagHeadersToString(FrameFlagHeaders flag);

    /**
     * @brief 将 SETTINGS 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagSettingsToString(FrameFlagSettings flag);

    /**
     * @brief 将 PING 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagPingToString(FrameFlagPing flag);

    /**
     * @brief 将 CONTINUATION 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagContinuationToString(FrameFlagContinuation flag);

    /**
     * @brief 将 PUSH_PROMISE 帧标志转换为字符串
     * @param flag 帧标志
     * @return 帧标志的字符串表示
     */
    std::string FrameFlagPromiseToString(FrameFlagContinuation flag);

    /**
     * @brief 将帧类型和标志转换为字符串
     * @param type 帧类型
     * @param flag 帧标志
     * @return 帧类型和标志的字符串表示
     */
    std::string FrameFlagToString(uint8_t type, uint8_t flag);

    /**
     * @brief 将保留位状态转换为字符串
     * @param r 保留位状态
     * @return 保留位状态的字符串表示
     */
    std::string FrameRToString(FrameR r);

#pragma pack(pop)

} // namespace http2
} // namespace sylar

#endif