#ifndef __SYLAR_DB_POSTGRESQL_H__
#define __SYLAR_DB_POSTGRESQL_H__

#include <memory>
#include <string>
#include <list>
#include <map>
#include <vector>
#include "db.h"
#include "sylar/core/mutex.h"
#include "sylar/core/common/singleton.h"

struct pg_conn;
typedef struct pg_conn PGconn;
struct pg_result;
typedef struct pg_result PGresult;

namespace sylar
{

class PostgreSQLStmt;

class PostgreSQLManager;
class PostgreSQL : public IDB, public std::enable_shared_from_this<PostgreSQL>
{
    friend class PostgreSQLManager;

public:
    typedef std::shared_ptr<PostgreSQL> ptr;
    static PostgreSQL::ptr Create(PGconn *conn);
    static PostgreSQL::ptr Create(const std::string &host, int port, const std::string &user,
                                  const std::string &passwd, const std::string &dbname);
    ~PostgreSQL();

    IStmt::ptr prepare(const std::string &stmt) override;

    int getErrno() override;
    std::string getErrStr() override;

    int execute(const char *format, ...) override;
    int execute(const char *format, va_list ap);
    int execute(const std::string &sql) override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query(const char *format, ...) override;
    ISQLData::ptr query(const char *format, va_list ap);
    ISQLData::ptr query(const std::string &sql) override;

    ITransaction::ptr openTransaction(bool auto_commit = false) override;

    template <typename... Args>
    int execStmt(const char *stmt, Args &&...args);

    template <class... Args>
    ISQLData::ptr queryStmt(const char *stmt, const Args &...args);

    int close();
    bool ping();
    PGconn *getConn();

    uint64_t getLastUsedTime() const { return m_lastUsedTime; }

private:
    PostgreSQL(PGconn *conn);

private:
    PGconn *m_conn;
    std::string m_cmd;
    uint64_t m_lastUsedTime = 0;
};

class PostgreSQLStmt;
class PostgreSQLRes : public ISQLData
{
public:
    typedef std::shared_ptr<PostgreSQLRes> ptr;
    PostgreSQLRes(PGresult *res, int err, const char *errstr);
    ~PostgreSQLRes();

    int getErrno() const override;
    const std::string &getErrStr() const override;
    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;

    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;

    bool next() override;

private:
    int m_errno;
    int m_cur;
    std::string m_errstr;
    PGresult *m_res;
};

class PostgreSQLStmt : public IStmt, public std::enable_shared_from_this<PostgreSQLStmt>
{
    friend class PostgreSQLRes;

public:
    typedef std::shared_ptr<PostgreSQLStmt> ptr;
    static PostgreSQLStmt::ptr Create(PostgreSQL::ptr db, const std::string &stmt);

    ~PostgreSQLStmt();

    int bindInt8(int idx, const int8_t &value) override;
    int bindUint8(int idx, const uint8_t &value) override;
    int bindInt16(int idx, const int16_t &value) override;
    int bindUint16(int idx, const uint16_t &value) override;
    int bindInt32(int idx, const int32_t &value) override;
    int bindUint32(int idx, const uint32_t &value) override;
    int bindInt64(int idx, const int64_t &value) override;
    int bindUint64(int idx, const uint64_t &value) override;
    int bindFloat(int idx, const float &value) override;
    int bindDouble(int idx, const double &value) override;
    int bindString(int idx, const char *value) override;
    int bindString(int idx, const std::string &value) override;
    int bindBlob(int idx, const void *value, int64_t size) override;
    int bindBlob(int idx, const std::string &value) override;
    int bindTime(int idx, const time_t &value) override;
    int bindNull(int idx) override;

    ISQLData::ptr query() override;
    int execute() override;
    int64_t getLastInsertId() override;

    int getErrno() override;
    std::string getErrStr() override;

protected:
    PostgreSQLStmt(PostgreSQL::ptr db, const std::string &stmt);

protected:
    PostgreSQL::ptr m_db;
    std::string m_stmt;
    std::string m_stmtName;
    std::vector<std::string> m_params;
    std::vector<const char *> m_paramValues;
    std::vector<int> m_paramLengths;
    std::vector<int> m_paramFormats;
    bool m_prepared;
};

class PostgreSQLTransaction : public ITransaction
{
public:
    PostgreSQLTransaction(PostgreSQL::ptr db, bool auto_commit = false);
    ~PostgreSQLTransaction();
    bool begin() override;
    bool commit() override;
    bool rollback() override;

    int execute(const char *format, ...) override;
    int execute(const std::string &sql) override;
    int64_t getLastInsertId() override;

private:
    PostgreSQL::ptr m_db;
    int8_t m_status;
    bool m_autoCommit;
};

class PostgreSQLManager
{
public:
    typedef sylar::Mutex MutexType;
    PostgreSQLManager();
    ~PostgreSQLManager();

    PostgreSQL::ptr get(const std::string &name);
    void registerPostgreSQL(const std::string &name,
                            const std::map<std::string, std::string> &params);

    void checkConnection(int sec = 30);

    template <class... Args>
    ISQLData::ptr queryStmt(const std::string &name, const char *stmt, const Args &...args);

    int execute(const std::string &name, const char *format, ...);
    int execute(const std::string &name, const char *format, va_list ap);
    int execute(const std::string &name, const std::string &sql);

    ISQLData::ptr query(const std::string &name, const char *format, ...);
    ISQLData::ptr query(const std::string &name, const char *format, va_list ap);
    ISQLData::ptr query(const std::string &name, const std::string &sql);

    PostgreSQLTransaction::ptr openTransaction(const std::string &name, bool auto_commit);

private:
    void freePostgreSQL(const std::string &name, PostgreSQL *m);

private:
    uint32_t m_maxConn;
    MutexType m_mutex;
    std::map<std::string, std::list<PostgreSQL *> > m_conns;
    std::map<std::string, std::map<std::string, std::string> > m_dbDefines;
};

typedef sylar::Singleton<PostgreSQLManager> PostgreSQLMgr;

// 模板实现部分
template <typename... Args>
int PostgreSQL::execStmt(const char *stmt, Args &&...args)
{
    auto st = prepare(stmt);
    if (!st) {
        return getErrno();
    }
    // 简化实现，不使用复杂的模板绑定
    return st->execute();
}

template <class... Args>
ISQLData::ptr PostgreSQL::queryStmt(const char *stmt, const Args &...args)
{
    auto st = prepare(stmt);
    if (!st) {
        return nullptr;
    }
    // 简化实现，不使用复杂的模板绑定
    return st->query();
}

template <class... Args>
ISQLData::ptr PostgreSQLManager::queryStmt(const std::string &name, const char *stmt, const Args &...args)
{
    auto conn = get(name);
    if (!conn) {
        return nullptr;
    }
    return conn->queryStmt(stmt, args...);
}

} // namespace sylar

#endif