#pragma once

#include <vector>
#include <string>

namespace sylar
{
namespace http2
{

    /**
     * @brief HTTP/2 动态表实现类
     * @details 用于存储最近使用的头部字段，优化 HPACK 压缩效率
     */
    class DynamicTable
    {
    public:
        /**
         * @brief 构造函数
         */
        DynamicTable();
        
        /**
         * @brief 更新动态表，添加新的头部字段
         * @param name 头部字段名称
         * @param value 头部字段值
         * @return 添加的字节数，失败返回负数
         */
        int32_t update(const std::string &name, const std::string &value);
        
        /**
         * @brief 根据名称查找头部字段在表中的索引
         * @param name 头部字段名称
         * @return 找到的索引值，未找到返回负数
         */
        int32_t findIndex(const std::string &name) const;
        
        /**
         * @brief 查找指定名称和值的头部字段对
         * @param name 头部字段名称
         * @param value 头部字段值
         * @return 找到的索引值和是否完全匹配
         */
        std::pair<int32_t, bool> findPair(const std::string &name, const std::string &value) const;
        
        /**
         * @brief 根据索引获取头部字段对
         * @param idx 索引值
         * @return 头部字段名称和值的对
         */
        std::pair<std::string, std::string> getPair(uint32_t idx) const;
        
        /**
         * @brief 根据索引获取头部字段名称
         * @param idx 索引值
         * @return 头部字段名称
         */
        std::string getName(uint32_t idx) const;
        
        /**
         * @brief 将动态表内容转换为字符串表示
         * @return 动态表的字符串表示
         */
        std::string toString() const;

        /**
         * @brief 设置动态表的最大数据大小
         * @param v 最大数据大小
         */
        void setMaxDataSize(int32_t v) { m_maxDataSize = v; }

    public:
        /**
         * @brief 静态方法：根据索引获取静态头部表中的头部字段
         * @param idx 索引值
         * @return 静态头部字段名称和值的对
         */
        static std::pair<std::string, std::string> GetStaticHeaders(uint32_t idx);
        
        /**
         * @brief 静态方法：根据名称查找静态头部表中的索引
         * @param name 头部字段名称
         * @return 找到的索引值，未找到返回负数
         */
        static int32_t GetStaticHeadersIndex(const std::string &name);
        
        /**
         * @brief 静态方法：查找静态头部表中指定名称和值的头部字段对
         * @param name 头部字段名称
         * @param val 头部字段值
         * @return 找到的索引值和是否完全匹配
         */
        static std::pair<int32_t, bool> GetStaticHeadersPair(const std::string &name,
                                                             const std::string &val);

    private:
        int32_t m_maxDataSize; ///< 动态表的最大数据大小
        int32_t m_dataSize;    ///< 动态表当前的数据大小
        std::vector<std::pair<std::string, std::string> > m_datas; ///< 存储头部字段对的向量
    };

} // namespace http2
} // namespace sylar 