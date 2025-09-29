#ifndef __SYLAR_ORM_COLUMN_H__
#define __SYLAR_ORM_COLUMN_H__

#include <memory>
#include <string>
#include "tinyxml2.h"

namespace sylar {
namespace orm {

class Table;
class Column {
friend class Table;
public:
    typedef std::shared_ptr<Column> ptr;
    enum Type {
        TYPE_NULL = 0,
        TYPE_INT8,
        TYPE_UINT8,
        TYPE_INT16,
        TYPE_UINT16,
        TYPE_INT32,
        TYPE_UINT32,
        TYPE_FLOAT,
        TYPE_INT64,
        TYPE_UINT64,
        TYPE_DOUBLE,
        TYPE_STRING,
        TYPE_TEXT,
        TYPE_BLOB,
        TYPE_TIMESTAMP,
        TYPE_VECTOR  // 新增向量类型，用于pgvector扩展
    };

    const std::string& getName() const { return m_name;}
    const std::string& getType() const { return m_type;}
    const std::string& getDesc() const { return m_desc;}
    const std::string& getDefault() const { return m_default;}

    std::string getDefaultValueString();
    std::string getSQLite3Default();

    bool isAutoIncrement() const { return m_autoIncrement;}
    Type getDType() const { return m_dtype;}

    bool init(const tinyxml2::XMLElement& node);

    std::string getMemberDefine() const;
    std::string getGetFunDefine() const;
    std::string getSetFunDefine() const;
    std::string getSetFunImpl(const std::string& class_name, int idx) const;
    int getIndex() const { return m_index;}

    static Type ParseType(const std::string& v);
    static std::string TypeToString(Type type);

    std::string getDTypeString() { return TypeToString(m_dtype);}
    std::string getSQLite3TypeString();
    std::string getMySQLTypeString();
    std::string getPostgreSQLTypeString();  // 新增PostgreSQL类型字符串方法

    std::string getBindString();
    std::string getGetString();
    std::string getPostgreSQLDefault();  // 新增PostgreSQL默认值方法
    const std::string& getUpdate() const { return m_update;}
private:
    std::string m_name;
    std::string m_type;
    std::string m_default;
    std::string m_update;
    std::string m_desc;
    int m_index;

    bool m_autoIncrement;
    Type m_dtype;
    int m_length;
};

}
}

#endif
