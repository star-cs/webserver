#include "table.h"
#include "sylar/core/log/log.h"
#include "sylar/core/util/hash_util.h"
#include "util.h"
#include <set>

namespace sylar
{
namespace orm
{

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("orm");

    std::string Table::getFilename() const
    {
        return sylar::ToLower(m_name + m_subfix);
    }

    bool Table::init(const tinyxml2::XMLElement &node)
    {
        if (!node.Attribute("name")) {
            SYLAR_LOG_ERROR(g_logger) << "table name is null";
            return false;
        }

        m_name = node.Attribute("name");
        if (!node.Attribute("namespace")) {
            SYLAR_LOG_ERROR(g_logger) << "table namespace is null";
            return false;
        }
        m_namespace = node.Attribute("namespace");

        if (node.Attribute("desc")) {
            m_desc = node.Attribute("desc");
        }

        const tinyxml2::XMLElement *cols = node.FirstChildElement("columns");
        if (!cols) {
            SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " columns is null";
            return false;
        }

        const tinyxml2::XMLElement *col = cols->FirstChildElement("column");
        if (!col) {
            SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " column is null";
            return false;
        }

        std::set<std::string> col_names;
        int index = 0;
        do {
            Column::ptr col_ptr(new Column);
            if (!col_ptr->init(*col)) {
                SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " init column error";
                return false;
            }
            // 检测字段名重复
            if (col_names.insert(col_ptr->getName()).second == false) {
                SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name
                                          << " column name=" << col_ptr->getName() << " exists";
                return false;
            }
            col_ptr->m_index = index++;
            m_cols.push_back(col_ptr);
            col = col->NextSiblingElement("column");
        } while (col);

        const tinyxml2::XMLElement *idxs = node.FirstChildElement("indexs");
        if (!idxs) {
            SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " indexs is null";
            return false;
        }

        const tinyxml2::XMLElement *idx = idxs->FirstChildElement("index");
        if (!idx) {
            SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " index is null";
            return false;
        }

        std::set<std::string> idx_names;
        bool has_pk = false;
        do {
            Index::ptr idx_ptr(new Index);
            if (!idx_ptr->init(*idx)) {
                SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " index init error";
                return false;
            }
            // 检测索引名重复
            if (idx_names.insert(idx_ptr->getName()).second == false) {
                SYLAR_LOG_ERROR(g_logger)
                    << "table name=" << m_name << " index name=" << idx_ptr->getName() << " exists";
                return false;
            }

            // 主键索引 不允许多个
            if (idx_ptr->isPK()) {
                if (has_pk) {
                    SYLAR_LOG_ERROR(g_logger) << "table name=" << m_name << " more than one pk";
                    return false;
                }
                has_pk = true;
            }

            // 索引字段
            auto &cnames = idx_ptr->getCols();
            for (auto &x : cnames) {
                if (col_names.count(x) == 0) {
                    SYLAR_LOG_ERROR(g_logger)
                        << "table name=" << m_name << " idx=" << idx_ptr->getName() << " col=" << x
                        << " not exists";
                    return false;
                }
            }

            m_idxs.push_back(idx_ptr);
            idx = idx->NextSiblingElement("index");
        } while (idx);
        return true;
    }

    void Table::gen(const std::string &path)
    {
        std::string p = path + "/" + sylar::replace(m_namespace, ".", "/");
        sylar::FSUtil::Mkdir(p);
        gen_inc(p);
        gen_src(p);
    }

    void Table::gen_inc(const std::string &path)
    {
        std::string filename = path + "/" + m_name + m_subfix + ".h";
        std::string class_name = m_name + m_subfix;
        std::string class_name_dao = m_name + m_subfix + "_dao";
        std::ofstream ofs(filename);
        ofs << "#ifndef " << GetAsDefineMacro(m_namespace + class_name + ".h") << std::endl;
        ofs << "#define " << GetAsDefineMacro(m_namespace + class_name + ".h") << std::endl;

        ofs << std::endl;

        // 引入 头文件
        std::set<std::string> sincs = {"vector", "json/json.h"};
        for (auto &i : sincs) {
            ofs << "#include <" << i << ">" << std::endl;
        }

        // 引入 自定义头文件
        std::set<std::string> incs = {"sylar/io/db/db.h", "sylar/core/util/json_util.h"};
        for (auto &i : incs) {
            ofs << "#include \"" << i << "\"" << std::endl;
        }
        ofs << std::endl;
        ofs << std::endl;

        std::vector<std::string> ns = sylar::split(m_namespace, '.');
        for (auto it = ns.begin(); it != ns.end(); ++it) {
            ofs << "namespace " << *it << " {" << std::endl;
        }

        ofs << std::endl;
        ofs << "class " << GetAsClassName(class_name_dao) << ";" << std::endl;
        ofs << "class " << GetAsClassName(class_name) << " {" << std::endl;
        ofs << "friend class " << GetAsClassName(class_name_dao) << ";" << std::endl;
        ofs << "public:" << std::endl;
        ofs << "    typedef std::shared_ptr<" << GetAsClassName(class_name) << "> ptr;"
            << std::endl;
        ofs << std::endl;
        ofs << "    " << GetAsClassName(class_name) << "();" << std::endl;
        ofs << std::endl;

        auto cols = m_cols;
        std::sort(cols.begin(), cols.end(), [](const Column::ptr &a, const Column::ptr &b) {
            if (a->getDType() != b->getDType()) {
                return a->getDType() < b->getDType();
            } else {
                return a->getIndex() < b->getIndex();
            }
        });

        for (auto &i : m_cols) {
            ofs << "    " << i->getGetFunDefine();
            ofs << "    " << i->getSetFunDefine();
            ofs << std::endl;
        }
        ofs << "    " << genToStringInc() << std::endl;
        //ofs << "    std::string toInsertSQL() const;" << std::endl;
        //ofs << "    std::string toUpdateSQL() const;" << std::endl;
        //ofs << "    std::string toDeleteSQL() const;" << std::endl;
        ofs << std::endl;

        ofs << "private:" << std::endl;
        for (auto &i : cols) {
            ofs << "    " << i->getMemberDefine();
        }
        //ofs << "    uint64_t _flags = 0;" << std::endl;
        ofs << "};" << std::endl;
        ofs << std::endl;

        ofs << std::endl;
        gen_dao_inc(ofs);
        ofs << std::endl;

        for (auto it = ns.rbegin(); it != ns.rend(); ++it) {
            ofs << "} //namespace " << *it << std::endl;
        }
        ofs << "#endif //" << GetAsDefineMacro(m_namespace + class_name + ".h") << std::endl;
    }

    std::string Table::genToStringInc()
    {
        std::stringstream ss;
        ss << "std::string toJsonString() const;";
        return ss.str();
    }

    std::string Table::genToStringSrc(const std::string &class_name)
    {
        std::stringstream ss;
        ss << "std::string " << GetAsClassName(class_name) << "::toJsonString() const {"
           << std::endl;
        ss << "    Json::Value jvalue;" << std::endl;
        for (auto it = m_cols.begin(); it != m_cols.end(); ++it) {
            ss << "    jvalue[\"" << (*it)->getName() << "\"] = ";
            if ((*it)->getDType() == Column::TYPE_UINT64
                || (*it)->getDType() == Column::TYPE_INT64) {
                ss << "std::to_string(" << GetAsMemberName((*it)->getName()) << ")"
                   << ";" << std::endl;
            } else if ((*it)->getDType() == Column::TYPE_TIMESTAMP) {
                ss << "sylar::Time2Str(" << GetAsMemberName((*it)->getName()) << ")"
                   << ";" << std::endl;
            } else {
                ss << GetAsMemberName((*it)->getName()) << ";" << std::endl;
            }
        }
        ss << "    return sylar::JsonUtil::ToString(jvalue);" << std::endl;
        ss << "}" << std::endl;
        return ss.str();
    }

    /**
         * @brief 生成源文件（.cc）内容并写入指定路径
         * @param[in] path 文件输出的目录路径
         */
    void Table::gen_src(const std::string &path)
    {
        // 构造类名和文件名
        std::string class_name = m_name + m_subfix;
        std::string filename = path + "/" + class_name + ".cc";
        std::ofstream ofs(filename);

        // 写入头文件包含
        ofs << "#include \"" << class_name + ".h\"" << std::endl;
        ofs << "#include \"sylar/core/log/log.h\"" << std::endl;
        ofs << std::endl;

        // 按命名空间层级展开 namespace 块
        std::vector<std::string> ns = sylar::split(m_namespace, '.');
        for (auto it = ns.begin(); it != ns.end(); ++it) {
            ofs << "namespace " << *it << " {" << std::endl;
        }

        ofs << std::endl;
        // 定义日志器
        ofs << "static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME(\"orm\");" << std::endl;

        ofs << std::endl;
        // 生成构造函数定义及初始化列表
        ofs << GetAsClassName(class_name) << "::" << GetAsClassName(class_name) << "()"
            << std::endl;
        ofs << "    :";

        // 对列按类型和索引排序，用于初始化列表顺序
        auto cols = m_cols;
        std::sort(cols.begin(), cols.end(), [](const Column::ptr &a, const Column::ptr &b) {
            if (a->getDType() != b->getDType()) {
                return a->getDType() < b->getDType();
            } else {
                return a->getIndex() < b->getIndex();
            }
        });

        // 输出成员变量的初始化列表
        for (auto it = cols.begin(); it != cols.end(); ++it) {
            if (it != cols.begin()) {
                ofs << std::endl << "    ,";
            }
            ofs << GetAsMemberName((*it)->getName()) << "(" << (*it)->getDefaultValueString()
                << ")";
        }
        ofs << " {" << std::endl;
        ofs << "}" << std::endl;
        ofs << std::endl;

        // 生成 toString 方法实现
        ofs << genToStringSrc(class_name) << std::endl;
        // 注释掉的SQL生成函数预留位置
        //ofs << genToInsertSQL(class_name) << std::endl;
        //ofs << genToUpdateSQL(class_name) << std::endl;
        //ofs << genToDeleteSQL(class_name) << std::endl;

        // 生成各字段的 setter 函数实现
        for (size_t i = 0; i < m_cols.size(); ++i) {
            ofs << m_cols[i]->getSetFunImpl(class_name, i) << std::endl;
        }

        ofs << std::endl;
        // 生成 DAO 相关代码
        gen_dao_src(ofs);
        ofs << std::endl;

        // 结束 namespace 块，按逆序闭合
        for (auto it = ns.rbegin(); it != ns.rend(); ++it) {
            ofs << "} //namespace " << *it << std::endl;
        }
    }

    std::string Table::genToInsertSQL(const std::string &class_name)
    {
        std::stringstream ss;
        ss << "std::string " << GetAsClassName(class_name) << "::toInsertSQL() const {"
           << std::endl;
        ss << "    std::stringstream ss;" << std::endl;
        ss << "    ss << \"insert into " << m_name << "(";
        bool is_first = true;
        for (size_t i = 0; i < m_cols.size(); ++i) {
            if (m_cols[i]->isAutoIncrement()) {
                continue;
            }
            if (!is_first) {
                ss << ",";
            }
            ss << m_cols[i]->getName();
            is_first = false;
        }
        ss << ") values (\"" << std::endl;
        is_first = true;
        for (size_t i = 0; i < m_cols.size(); ++i) {
            if (m_cols[i]->isAutoIncrement()) {
                continue;
            }
            if (!is_first) {
                ss << "    ss << \",\";" << std::endl;
            }
            if (m_cols[i]->getDType() == Column::TYPE_STRING) {
                ss << "    ss << \"'\" << sylar::replace(" << GetAsMemberName(m_cols[i]->getName())
                   << ", \"'\", \"''\") << \"'\";" << std::endl;
            } else {
                ss << "    ss << " << GetAsMemberName(m_cols[i]->getName()) << ";" << std::endl;
            }
            is_first = true;
        }
        ss << "    ss << \")\";" << std::endl;
        ss << "    return ss.str();" << std::endl;
        ss << "}" << std::endl;
        return ss.str();
    }

    std::string Table::genToUpdateSQL(const std::string &class_name)
    {
        std::stringstream ss;
        ss << "std::string " << GetAsClassName(class_name) << "::toUpdateSQL() const {"
           << std::endl;
        ss << "    std::stringstream ss;" << std::endl;
        ss << "    bool is_first = true;" << std::endl;
        ss << "    ss << \"update " << m_name << " set \";" << std::endl;
        for (size_t i = 0; i < m_cols.size(); ++i) {
            ss << "    if(_flags & " << (1ul << i) << "ul) {" << std::endl;
            ss << "        if(!is_first) {" << std::endl;
            ss << "            ss << \",\";" << std::endl;
            ss << "        }" << std::endl;
            ss << "        ss << \" " << m_cols[i]->getName() << " = ";
            if (m_cols[i]->getDType() == Column::TYPE_STRING) {
                ss << "'\" << sylar::replace(" << GetAsMemberName(m_cols[i]->getName())
                   << ", \"'\", \"''\") << \"'\";" << std::endl;
            } else {
                ss << "\" << " << GetAsMemberName(m_cols[i]->getName()) << ";" << std::endl;
            }
            ss << "        is_first = false;" << std::endl;
            ss << "    }" << std::endl;
        }
        ss << genWhere();
        ss << "    return ss.str();" << std::endl;
        ss << "}" << std::endl;
        return ss.str();
    }

    std::string Table::genToDeleteSQL(const std::string &class_name)
    {
        std::stringstream ss;
        ss << "std::string " << GetAsClassName(class_name) << "::toDeleteSQL() const {"
           << std::endl;
        ss << "    std::stringstream ss;" << std::endl;
        ss << "    ss << \"delete from " << m_name << "\";" << std::endl;
        ss << genWhere();
        ss << "    return ss.str();" << std::endl;
        ss << "}" << std::endl;
        return ss.str();
    }

    std::string Table::genWhere() const
    {
        std::stringstream ss;
        ss << "    ss << \" where ";
        auto pks = getPKs();

        for (size_t i = 0; i < pks.size(); ++i) {
            if (i) {
                ss << "    ss << \" and ";
            }
            ss << pks[i]->getName() << " = ";
            if (pks[i]->getDType() == Column::TYPE_STRING) {
                ss << "'\" << sylar::replace(" << GetAsMemberName(m_cols[i]->getName())
                   << ", \"'\", \"''\") << \"'\";" << std::endl;
            } else {
                ss << "\" << " << GetAsMemberName(m_cols[i]->getName()) << ";" << std::endl;
            }
        }
        return ss.str();
    }

    std::vector<Column::ptr> Table::getPKs() const
    {
        std::vector<Column::ptr> cols;
        for (auto &i : m_idxs) {
            if (i->isPK()) {
                for (auto &n : i->getCols()) {
                    cols.push_back(getCol(n));
                }
            }
        }
        return cols;
    }

    Column::ptr Table::getCol(const std::string &name) const
    {
        for (auto &i : m_cols) {
            if (i->getName() == name) {
                return i;
            }
        }
        return nullptr;
    }

    /**
         * @brief 生成数据访问对象（DAO）的头文件声明代码
         * 
         * 该函数用于生成一个C++类的声明代码，该类提供了对数据库表的基本操作接口，
         * 包括增删改查、创建表等方法。生成的类名由表名和后缀组合而成，并以 "_dao" 结尾。
         * 
         * @param ofs 输出流，用于写入生成的代码内容
         */
    void Table::gen_dao_inc(std::ofstream &ofs)
    {
        std::string class_name = m_name + m_subfix;
        std::string class_name_dao = class_name + "_dao";

        // 写入类定义开始
        ofs << "class " << GetAsClassName(class_name_dao) << " {" << std::endl;
        ofs << "public:" << std::endl;

        // 定义智能指针类型别名
        ofs << "    typedef std::shared_ptr<" << GetAsClassName(class_name_dao) << "> ptr;"
            << std::endl;

        // 声明更新记录的方法
        ofs << "    static int Update(" << GetAsClassName(class_name) << "::ptr info, "
            << m_updateclass << "::ptr conn);" << std::endl;

        // 声明插入记录的方法
        ofs << "    static int Insert(" << GetAsClassName(class_name) << "::ptr info, "
            << m_updateclass << "::ptr conn);" << std::endl;

        // 声明插入或更新记录的方法
        ofs << "    static int InsertOrUpdate(" << GetAsClassName(class_name) << "::ptr info, "
            << m_updateclass << "::ptr conn);" << std::endl;

        // 声明根据主键删除记录的方法（参数为主键值和连接对象）
        ofs << "    static int Delete(" << GetAsClassName(class_name) << "::ptr info, "
            << m_updateclass << "::ptr conn);" << std::endl;

        // 获取主键列信息
        auto vs = getPKs();

        // 根据主键字段生成按主键删除的方法声明
        ofs << "    static int Delete(";
        for (auto &i : vs) {
            ofs << "const " << i->getDTypeString() << "& " << GetAsVariable(i->getName()) << ", ";
        }
        ofs << m_updateclass << "::ptr conn);" << std::endl;

        // 遍历索引信息，生成基于索引的删除方法声明
        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_UNIQ || i->getDType() == Index::TYPE_PK
                || i->getDType() == Index::TYPE_INDEX) {
                ofs << "    static int Delete";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_updateclass << "::ptr conn);" << std::endl;
            }
        }

        // 声明查询所有记录的方法
        ofs << "    static int QueryAll(std::vector<" << GetAsClassName(class_name)
            << "::ptr>& results, " << m_queryclass << "::ptr conn);" << std::endl;

        // 声明通过主键查询单条记录的方法
        ofs << "    static " << GetAsClassName(class_name) << "::ptr Query(";
        for (auto &i : vs) {
            ofs << " const " << i->getDTypeString() << "& " << GetAsVariable(i->getName()) << ", ";
        }
        ofs << m_queryclass << "::ptr conn);" << std::endl;

        // 遍历索引信息，生成基于唯一索引和普通索引的查询方法声明
        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_UNIQ) {
                // 唯一索引：生成返回单个对象的查询方法
                ofs << "    static " << GetAsClassName(class_name) << "::ptr Query";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_queryclass << "::ptr conn);" << std::endl;
            } else if (i->getDType() == Index::TYPE_INDEX) {
                // 普通索引：生成返回多个对象的查询方法
                ofs << "    static int Query";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                ofs << "std::vector<" << GetAsClassName(class_name) << "::ptr>& results, ";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_queryclass << "::ptr conn);" << std::endl;
            }
        }

        // 声明创建SQLite3和MySQL表结构的方法
        ofs << "    static int CreateTableSQLite3(" << m_dbclass << "::ptr info);" << std::endl;
        ofs << "    static int CreateTableMySQL(" << m_dbclass << "::ptr info);" << std::endl;

        // 写入类定义结束
        ofs << "};" << std::endl;
    }

    template <class V, class T>
    bool is_exists(const V &v, const T &t)
    {
        for (auto &i : v) {
            if (i == t) {
                return true;
            }
        }
        return false;
    }

    void Table::gen_dao_src(std::ofstream &ofs)
    {
        std::string class_name = m_name + m_subfix;
        std::string class_name_dao = class_name + "_dao";
        ofs << "int " << GetAsClassName(class_name_dao) << "::Update(" << GetAsClassName(class_name)
            << "::ptr info, " << m_updateclass << "::ptr conn) {" << std::endl;
        ofs << "    std::string sql = \"update " << m_name << " set";
        auto pks = getPKs();
        bool is_first = true;
        for (auto &i : m_cols) {
            if (is_exists(pks, i)) {
                continue;
            }
            if (!is_first) {
                ofs << ",";
            }
            ofs << " " << i->getName() << " = ?";
            is_first = false;
        }

        ofs << " where";
        is_first = true;
        for (auto &i : pks) {
            if (!is_first) {
                ofs << " and";
            }
            ofs << " " << i->getName() << " = ?";
        }
        ofs << "\";" << std::endl;
#define CHECK_STMT(v)                                                                              \
    ofs << "    auto stmt = conn->prepare(sql);" << std::endl;                                     \
    ofs << "    if(!stmt) {" << std::endl;                                                         \
    ofs << "        SYLAR_LOG_ERROR(g_logger) << \"stmt=\" << sql" << std::endl                    \
        << "                 << \" errno=\""                                                       \
           " << conn->getErrno() << \" errstr=\" << conn->getErrStr();"                            \
        << std::endl                                                                               \
        << "        return " v ";" << std::endl;                                                   \
    ofs << "    }" << std::endl;

        CHECK_STMT("conn->getErrno()");
        is_first = true;
        int idx = 1;
        for (auto &i : m_cols) {
            if (is_exists(pks, i)) {
                continue;
            }
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << "info->" << GetAsMemberName(i->getName());
            ofs << ");" << std::endl;
            ++idx;
        }
        for (auto &i : pks) {
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << "info->" << GetAsMemberName(i->getName()) << ");" << std::endl;
            ++idx;
        }
        ofs << "    return stmt->execute();" << std::endl;

        ofs << "}" << std::endl << std::endl;
        ofs << "int " << GetAsClassName(class_name_dao) << "::Insert(" << GetAsClassName(class_name)
            << "::ptr info, " << m_updateclass << "::ptr conn) {" << std::endl;
        ofs << "    std::string sql = \"insert into " << m_name << " (";
        is_first = true;
        Column::ptr auto_inc;

        for (auto &i : m_cols) {
            if (i->isAutoIncrement()) {
                auto_inc = i;
                continue;
            }
            if (!is_first) {
                ofs << ", ";
            }
            ofs << i->getName();
            is_first = false;
        }

        ofs << ") values (";
        is_first = true;
        for (auto &i : m_cols) {
            if (i->isAutoIncrement()) {
                continue;
            }
            if (!is_first) {
                ofs << ", ";
            }
            ofs << "?";
            is_first = false;
        }
        ofs << ")\";" << std::endl;

        CHECK_STMT("conn->getErrno()");

        idx = 1;
        for (auto &i : m_cols) {
            if (i->isAutoIncrement()) {
                continue;
            }
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << "info->" << GetAsMemberName(i->getName());
            ofs << ");" << std::endl;
            ++idx;
        }
        ofs << "    int rt = stmt->execute();" << std::endl;
        if (auto_inc) {
            ofs << "    if(rt == 0) {" << std::endl;
            ofs << "        info->" << GetAsMemberName(auto_inc->getName())
                << " = conn->getLastInsertId();" << std::endl
                << "    }" << std::endl;
        }
        ofs << "    return rt;" << std::endl;
        ofs << "}" << std::endl << std::endl;

        ofs << "int " << GetAsClassName(class_name_dao) << "::InsertOrUpdate("
            << GetAsClassName(class_name) << "::ptr info, " << m_updateclass << "::ptr conn) {"
            << std::endl;
        for (auto &i : m_cols) {
            if (i->isAutoIncrement()) {
                auto_inc = i;
                break;
            }
        }
        if (auto_inc) {
            ofs << "    if(info->" << GetAsMemberName(auto_inc->getName()) << " == 0) {"
                << std::endl;
            ofs << "        return Insert(info, conn);" << std::endl;
            ofs << "    }" << std::endl;
        }
        ofs << "    std::string sql = \"replace into " << m_name << " (";
        is_first = true;
        for (auto &i : m_cols) {
            if (!is_first) {
                ofs << ", ";
            }
            ofs << i->getName();
            is_first = false;
        }

        ofs << ") values (";
        is_first = true;
        for (auto &i : m_cols) {
            (void)i;
            if (!is_first) {
                ofs << ", ";
            }
            ofs << "?";
            is_first = false;
        }
        ofs << ")\";" << std::endl;

        CHECK_STMT("conn->getErrno()");
        idx = 1;
        for (auto &i : m_cols) {
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << "info->" << GetAsMemberName(i->getName()) << ");" << std::endl;
            ++idx;
        }
        ofs << "    return stmt->execute();" << std::endl;
        ofs << "}" << std::endl << std::endl;

        ofs << "int " << GetAsClassName(class_name_dao) << "::Delete(" << GetAsClassName(class_name)
            << "::ptr info, " << m_updateclass << "::ptr conn) {" << std::endl;

        ofs << "    std::string sql = \"delete from " << m_name << " where";
        is_first = true;
        for (auto &i : pks) {
            if (!is_first) {
                ofs << " and";
            }
            ofs << " " << i->getName() << " = ?";
            is_first = false;
        }
        ofs << "\";" << std::endl;
        CHECK_STMT("conn->getErrno()");
        idx = 1;
        for (auto &i : pks) {
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << "info->" << GetAsMemberName(i->getName()) << ");" << std::endl;
            ++idx;
        }
        ofs << "    return stmt->execute();" << std::endl;
        ofs << "}" << std::endl << std::endl;

        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_UNIQ || i->getDType() == Index::TYPE_PK
                || i->getDType() == Index::TYPE_INDEX) {
                ofs << "int " << GetAsClassName(class_name_dao) << "::Delete";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_updateclass << "::ptr conn) {" << std::endl;
                ofs << "    std::string sql = \"delete from " << m_name << " where";
                is_first = true;
                for (auto &x : i->getCols()) {
                    if (!is_first) {
                        ofs << " and";
                    }
                    ofs << " " << x << " = ?";
                    is_first = false;
                }
                ofs << "\";" << std::endl;
                CHECK_STMT("conn->getErrno()");
                idx = 1;
                for (auto &x : i->getCols()) {
                    ofs << "    stmt->" << getCol(x)->getBindString() << "(" << idx << ", ";
                    ofs << GetAsVariable(x) << ");" << std::endl;
                }
                ofs << "    return stmt->execute();" << std::endl;
                ofs << "}" << std::endl << std::endl;
            }
        }

        ofs << "int " << GetAsClassName(class_name_dao) << "::QueryAll(std::vector<"
            << GetAsClassName(class_name) << "::ptr>& results, " << m_queryclass << "::ptr conn) {"
            << std::endl;
        ofs << "    std::string sql = \"select ";
        is_first = true;
        for (auto &i : m_cols) {
            if (!is_first) {
                ofs << ", ";
            }
            ofs << i->getName();
            is_first = false;
        }
        ofs << " from " << m_name << "\";" << std::endl;
        CHECK_STMT("conn->getErrno()");
        ofs << "    auto rt = stmt->query();" << std::endl;
        ofs << "    if(!rt) {" << std::endl;
        ofs << "        return stmt->getErrno();" << std::endl;
        ofs << "    }" << std::endl;
        ofs << "    while (rt->next()) {" << std::endl;
        ofs << "        " << GetAsClassName(class_name) << "::ptr v(new "
            << GetAsClassName(class_name) << ");" << std::endl;

#define PARSE_OBJECT(prefix)                                                                       \
    for (size_t i = 0; i < m_cols.size(); ++i) {                                                   \
        ofs << prefix "v->" << GetAsMemberName(m_cols[i]->getName()) << " = ";                     \
        ofs << "rt->" << m_cols[i]->getGetString() << "(" << (i) << ");" << std::endl;             \
    }
        PARSE_OBJECT("        ");
        ofs << "        results.push_back(v);" << std::endl;
        ofs << "    }" << std::endl;
        ofs << "    return 0;" << std::endl;
        ofs << "}" << std::endl << std::endl;

        ofs << GetAsClassName(class_name) << "::ptr " << GetAsClassName(class_name_dao)
            << "::Query(";
        for (auto &i : pks) {
            ofs << " const " << i->getDTypeString() << "& " << GetAsVariable(i->getName()) << ", ";
        }
        ofs << m_queryclass << "::ptr conn) {" << std::endl;

        ofs << "    std::string sql = \"select ";
        is_first = true;
        for (auto &i : m_cols) {
            if (!is_first) {
                ofs << ", ";
            }
            ofs << i->getName();
            is_first = false;
        }
        ofs << " from " << m_name << " where";
        is_first = true;
        for (auto &i : pks) {
            if (!is_first) {
                ofs << " and";
            }
            ofs << " " << i->getName() << " = ?";
            is_first = false;
        }
        ofs << "\";" << std::endl;

        CHECK_STMT("nullptr");
        idx = 1;
        for (auto &i : pks) {
            ofs << "    stmt->" << i->getBindString() << "(" << idx << ", ";
            ofs << GetAsVariable(i->getName()) << ");" << std::endl;
            ++idx;
        }
        ofs << "    auto rt = stmt->query();" << std::endl;
        ofs << "    if(!rt) {" << std::endl;
        ofs << "        return nullptr;" << std::endl;
        ofs << "    }" << std::endl;
        ofs << "    if(!rt->next()) {" << std::endl;
        ofs << "        return nullptr;" << std::endl;
        ofs << "    }" << std::endl;
        ofs << "    " << GetAsClassName(class_name) << "::ptr v(new " << GetAsClassName(class_name)
            << ");" << std::endl;
        PARSE_OBJECT("    ");
        ofs << "    return v;" << std::endl;
        ofs << "}" << std::endl << std::endl;

        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_UNIQ) {
                ofs << "" << GetAsClassName(class_name) << "::ptr "
                    << GetAsClassName(class_name_dao) << "::Query";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_queryclass << "::ptr conn) {" << std::endl;

                ofs << "    std::string sql = \"select ";
                is_first = true;
                for (auto &i : m_cols) {
                    if (!is_first) {
                        ofs << ", ";
                    }
                    ofs << i->getName();
                    is_first = false;
                }
                ofs << " from " << m_name << " where";
                is_first = true;
                for (auto &x : i->getCols()) {
                    if (!is_first) {
                        ofs << " and";
                    }
                    ofs << " " << x << " = ?";
                    is_first = false;
                }
                ofs << "\";" << std::endl;
                CHECK_STMT("nullptr");

                idx = 1;
                for (auto &x : i->getCols()) {
                    ofs << "    stmt->" << getCol(x)->getBindString() << "(" << idx << ", ";
                    ofs << GetAsVariable(x) << ");" << std::endl;
                    ++idx;
                }
                ofs << "    auto rt = stmt->query();" << std::endl;
                ofs << "    if(!rt) {" << std::endl;
                ofs << "        return nullptr;" << std::endl;
                ofs << "    }" << std::endl;
                ofs << "    if(!rt->next()) {" << std::endl;
                ofs << "        return nullptr;" << std::endl;
                ofs << "    }" << std::endl;
                ofs << "    " << GetAsClassName(class_name) << "::ptr v(new "
                    << GetAsClassName(class_name) << ");" << std::endl;
                PARSE_OBJECT("    ");
                ofs << "    return v;" << std::endl;
                ofs << "}" << std::endl << std::endl;
            } else if (i->getDType() == Index::TYPE_INDEX) {
                ofs << "int " << GetAsClassName(class_name_dao) << "::Query";
                std::string tmp = "by";
                for (auto &c : i->getCols()) {
                    tmp += "_" + c;
                }
                ofs << GetAsClassName(tmp) << "(";
                ofs << "std::vector<" << GetAsClassName(class_name) << "::ptr>& results, ";
                for (auto &c : i->getCols()) {
                    auto d = getCol(c);
                    ofs << " const " << d->getDTypeString() << "& " << GetAsVariable(d->getName())
                        << ", ";
                }
                ofs << m_queryclass << "::ptr conn) {" << std::endl;

                ofs << "    std::string sql = \"select ";
                is_first = true;
                for (auto &i : m_cols) {
                    if (!is_first) {
                        ofs << ", ";
                    }
                    ofs << i->getName();
                    is_first = false;
                }
                ofs << " from " << m_name << " where";
                is_first = true;
                for (auto &x : i->getCols()) {
                    if (!is_first) {
                        ofs << " and";
                    }
                    ofs << " " << x << " = ?";
                    is_first = false;
                }
                ofs << "\";" << std::endl;
                CHECK_STMT("conn->getErrno()");

                idx = 1;
                for (auto &x : i->getCols()) {
                    ofs << "    stmt->" << getCol(x)->getBindString() << "(" << idx << ", ";
                    ofs << GetAsVariable(x) << ");" << std::endl;
                    ++idx;
                }
                ofs << "    auto rt = stmt->query();" << std::endl;
                ofs << "    if(!rt) {" << std::endl;
                ofs << "        return 0;" << std::endl;
                ofs << "    }" << std::endl;
                ofs << "    while (rt->next()) {" << std::endl;
                ofs << "        " << GetAsClassName(class_name) << "::ptr v(new "
                    << GetAsClassName(class_name) << ");" << std::endl;
                PARSE_OBJECT("        ");
                ofs << "        results.push_back(v);" << std::endl;
                ofs << "    };" << std::endl;
                ofs << "    return 0;" << std::endl;
                ofs << "}" << std::endl << std::endl;
            }
        }

        ofs << "int " << GetAsClassName(class_name_dao) << "::CreateTableSQLite3(" << m_dbclass
            << "::ptr conn) {" << std::endl;
        ofs << "    return conn->execute(\"CREATE TABLE " << m_name << "(\"" << std::endl;
        is_first = true;
        bool has_auto_increment = false;
        for (auto &i : m_cols) {
            if (!is_first) {
                ofs << ",\"" << std::endl;
            }
            ofs << "            \"" << i->getName() << " " << i->getSQLite3TypeString();
            if (i->isAutoIncrement()) {
                ofs << " PRIMARY KEY AUTOINCREMENT";
                has_auto_increment = true;
            } else {
                ofs << " NOT NULL DEFAULT " << i->getSQLite3Default();
            }
            is_first = false;
        }
        if (!has_auto_increment) {
            ofs << ", PRIMARY KEY(";
            is_first = true;
            for (auto &i : pks) {
                if (!is_first) {
                    ofs << ", ";
                }
                ofs << i->getName();
            }
            ofs << ")";
        }
        ofs << ");\"" << std::endl;
        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_PK) {
                continue;
            }
            ofs << "            \"CREATE";
            if (i->getDType() == Index::TYPE_UNIQ) {
                ofs << " UNIQUE";
            }
            ofs << " INDEX " << m_name;
            for (auto &x : i->getCols()) {
                ofs << "_" << x;
            }
            ofs << " ON " << m_name << "(";
            is_first = true;
            for (auto &x : i->getCols()) {
                if (!is_first) {
                    ofs << ",";
                }
                ofs << x;
                is_first = false;
            }
            ofs << ");\"" << std::endl;
        }
        ofs << "            );" << std::endl;
        ofs << "}" << std::endl << std::endl;

        ofs << "int " << GetAsClassName(class_name_dao) << "::CreateTableMySQL(" << m_dbclass
            << "::ptr conn) {" << std::endl;
        ofs << "    return conn->execute(\"CREATE TABLE " << m_name << "(\"" << std::endl;
        is_first = true;
        for (auto &i : m_cols) {
            if (!is_first) {
                ofs << ",\"" << std::endl;
            }
            ofs << "            \"`" << i->getName() << "` " << i->getMySQLTypeString();
            if (i->isAutoIncrement()) {
                ofs << " AUTO_INCREMENT";
                has_auto_increment = true;
            } else {
                ofs << " NOT NULL DEFAULT " << i->getSQLite3Default();
            }

            if (!i->getUpdate().empty()) {
                ofs << " ON UPDATE " << i->getUpdate() << " ";
            }
            if (!i->getDesc().empty()) {
                ofs << " COMMENT '" << i->getDesc() << "'";
            }
            is_first = false;
        }
        ofs << ",\"" << std::endl << "            \"PRIMARY KEY(";
        is_first = true;
        for (auto &i : pks) {
            if (!is_first) {
                ofs << ", ";
            }
            ofs << "`" << i->getName() << "`";
        }
        ofs << ")";
        for (auto &i : m_idxs) {
            if (i->getDType() == Index::TYPE_PK) {
                continue;
            }
            ofs << ",\"" << std::endl;
            if (i->getDType() == Index::TYPE_UNIQ) {
                ofs << "            \"UNIQUE ";
            } else {
                ofs << "            \"";
            }
            ofs << "KEY `" << m_name;
            for (auto &x : i->getCols()) {
                ofs << "_" << x;
            }
            ofs << "` (";
            is_first = true;
            for (auto &x : i->getCols()) {
                if (!is_first) {
                    ofs << ",";
                }
                ofs << "`" << x << "`";
                is_first = false;
            }
            ofs << ")";
        }
        ofs << ")";
        if (!m_desc.empty()) {
            ofs << " COMMENT='" << m_desc << "'";
        }
        ofs << "\");" << std::endl;
        ofs << "}";
    }

} // namespace orm
} // namespace sylar
