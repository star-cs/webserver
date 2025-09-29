#include "postgresql.h"
#include "sylar/core/log/log.h"
#include "sylar/core/config/config.h"
#include <libpq-fe.h>
#include <cstdarg>
#include <sstream>
#include <cstring>

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
static sylar::ConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr
    g_postgresql_dbs =
        sylar::Config::Lookup("postgresql.dbs",
                              std::map<std::string, std::map<std::string, std::string> >(),
                              "postgresql dbs");

// PostgreSQL utility functions
static PGconn *postgresql_init(const std::map<std::string, std::string> &params)
{
    std::string host = sylar::GetParamValue<std::string>(params, "host", "localhost");
    int port = sylar::GetParamValue(params, "port", 5432);
    std::string user = sylar::GetParamValue<std::string>(params, "user");
    std::string passwd = sylar::GetParamValue<std::string>(params, "passwd");
    std::string dbname = sylar::GetParamValue<std::string>(params, "dbname");

    std::ostringstream conninfo;
    conninfo << "host=" << host << " port=" << port;
    if (!user.empty()) {
        conninfo << " user=" << user;
    }
    if (!passwd.empty()) {
        conninfo << " password=" << passwd;
    }
    if (!dbname.empty()) {
        conninfo << " dbname=" << dbname;
    }

    PGconn *conn = PQconnectdb(conninfo.str().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        SYLAR_LOG_ERROR(g_logger) << "PostgreSQL connection failed: " << PQerrorMessage(conn);
        PQfinish(conn);
        return nullptr;
    }

    return conn;
}

// PostgreSQL class implementation
PostgreSQL::ptr PostgreSQL::Create(PGconn *conn)
{
    if (!conn) {
        return nullptr;
    }
    PostgreSQL::ptr rt(new PostgreSQL(conn));
    return rt;
}

PostgreSQL::ptr PostgreSQL::Create(const std::string &host, int port, const std::string &user,
                                   const std::string &passwd, const std::string &dbname)
{
    std::map<std::string, std::string> params;
    params["host"] = host;
    params["port"] = std::to_string(port);
    params["user"] = user;
    params["passwd"] = passwd;
    params["dbname"] = dbname;

    PGconn *conn = postgresql_init(params);
    if (!conn) {
        return nullptr;
    }
    return Create(conn);
}

PostgreSQL::PostgreSQL(PGconn *conn) : m_conn(conn), m_lastUsedTime(time(0))
{
}

PostgreSQL::~PostgreSQL()
{
    if (m_conn) {
        PQfinish(m_conn);
    }
}

IStmt::ptr PostgreSQL::prepare(const std::string &stmt)
{
    return PostgreSQLStmt::Create(shared_from_this(), stmt);
}

int PostgreSQL::getErrno()
{
    if (!m_conn) {
        return -1;
    }
    return 0;
}

std::string PostgreSQL::getErrStr()
{
    if (!m_conn) {
        return "Connection is null";
    }
    const char *err = PQerrorMessage(m_conn);
    return err ? err : "";
}

int PostgreSQL::execute(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQL::execute(const char *format, va_list ap)
{
    m_cmd = sylar::StringUtil::Formatv(format, ap);

    int rt = execute(m_cmd);
    return rt;
}

int PostgreSQL::execute(const std::string &sql)
{
    if (!m_conn) {
        return -1;
    }

    PGresult *res = PQexec(m_conn, sql.c_str());
    if (!res) {
        return -1;
    }

    ExecStatusType status = PQresultStatus(res);
    int rt = 0;
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        SYLAR_LOG_ERROR(g_logger) << "PostgreSQL execute error: " << PQerrorMessage(m_conn);
        rt = -1;
    }

    PQclear(res);
    m_lastUsedTime = time(0);
    return rt;
}

int64_t PostgreSQL::getLastInsertId()
{
    if (!m_conn) {
        return -1;
    }

    PGresult *res = PQexec(m_conn, "SELECT lastval()");
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res)
            PQclear(res);
        return -1;
    }

    int64_t id = 0;
    if (PQntuples(res) > 0) {
        id = atoll(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return id;
}

ISQLData::ptr PostgreSQL::query(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    auto rt = query(format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr PostgreSQL::query(const char *format, va_list ap)
{
    m_cmd = sylar::StringUtil::Formatv(format, ap);
    auto rt = query(m_cmd);
    return rt;
}

ISQLData::ptr PostgreSQL::query(const std::string &sql)
{
    if (!m_conn) {
        return std::make_shared<PostgreSQLRes>(nullptr, -1, "Connection is null");
    }

    PGresult *res = PQexec(m_conn, sql.c_str());
    m_lastUsedTime = time(0);

    if (!res) {
        return std::make_shared<PostgreSQLRes>(nullptr, -1, PQerrorMessage(m_conn));
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string err = PQerrorMessage(m_conn);
        PQclear(res);
        return std::make_shared<PostgreSQLRes>(nullptr, -1, err.c_str());
    }

    return std::make_shared<PostgreSQLRes>(res, 0, "");
}

ITransaction::ptr PostgreSQL::openTransaction(bool auto_commit)
{
    return std::make_shared<PostgreSQLTransaction>(shared_from_this(), auto_commit);
}

int PostgreSQL::close()
{
    if (m_conn) {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
    return 0;
}

bool PostgreSQL::ping()
{
    if (!m_conn) {
        return false;
    }
    return PQstatus(m_conn) == CONNECTION_OK;
}

PGconn *PostgreSQL::getConn()
{
    return m_conn;
}

// PostgreSQLRes class implementation
PostgreSQLRes::PostgreSQLRes(PGresult *res, int err, const char *errstr)
    : m_errno(err), m_cur(-1), m_errstr(errstr ? errstr : ""), m_res(res)
{
}

PostgreSQLRes::~PostgreSQLRes()
{
    if (m_res) {
        PQclear(m_res);
    }
}

int PostgreSQLRes::getErrno() const
{
    return m_errno;
}

const std::string &PostgreSQLRes::getErrStr() const
{
    return m_errstr;
}

int PostgreSQLRes::getDataCount()
{
    if (!m_res) {
        return 0;
    }
    return PQntuples(m_res);
}

int PostgreSQLRes::getColumnCount()
{
    if (!m_res) {
        return 0;
    }
    return PQnfields(m_res);
}

int PostgreSQLRes::getColumnBytes(int idx)
{
    if (!m_res || m_cur < 0 || idx < 0 || idx >= PQnfields(m_res)) {
        return 0;
    }
    return PQgetlength(m_res, m_cur, idx);
}

int PostgreSQLRes::getColumnType(int idx)
{
    if (!m_res || idx < 0 || idx >= PQnfields(m_res)) {
        return 0;
    }
    return PQftype(m_res, idx);
}

std::string PostgreSQLRes::getColumnName(int idx)
{
    if (!m_res || idx < 0 || idx >= PQnfields(m_res)) {
        return "";
    }
    return PQfname(m_res, idx);
}

bool PostgreSQLRes::isNull(int idx)
{
    if (!m_res || m_cur < 0 || idx < 0 || idx >= PQnfields(m_res)) {
        return true;
    }
    return PQgetisnull(m_res, m_cur, idx);
}

int8_t PostgreSQLRes::getInt8(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<int8_t>(atoi(PQgetvalue(m_res, m_cur, idx)));
}

uint8_t PostgreSQLRes::getUint8(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<uint8_t>(atoi(PQgetvalue(m_res, m_cur, idx)));
}

int16_t PostgreSQLRes::getInt16(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<int16_t>(atoi(PQgetvalue(m_res, m_cur, idx)));
}

uint16_t PostgreSQLRes::getUint16(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<uint16_t>(atoi(PQgetvalue(m_res, m_cur, idx)));
}

int32_t PostgreSQLRes::getInt32(int idx)
{
    if (isNull(idx))
        return 0;
    return atoi(PQgetvalue(m_res, m_cur, idx));
}

uint32_t PostgreSQLRes::getUint32(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<uint32_t>(atol(PQgetvalue(m_res, m_cur, idx)));
}

int64_t PostgreSQLRes::getInt64(int idx)
{
    if (isNull(idx))
        return 0;
    return atoll(PQgetvalue(m_res, m_cur, idx));
}

uint64_t PostgreSQLRes::getUint64(int idx)
{
    if (isNull(idx))
        return 0;
    return static_cast<uint64_t>(atoll(PQgetvalue(m_res, m_cur, idx)));
}

float PostgreSQLRes::getFloat(int idx)
{
    if (isNull(idx))
        return 0.0f;
    return atof(PQgetvalue(m_res, m_cur, idx));
}

double PostgreSQLRes::getDouble(int idx)
{
    if (isNull(idx))
        return 0.0;
    return atof(PQgetvalue(m_res, m_cur, idx));
}

std::string PostgreSQLRes::getString(int idx)
{
    if (isNull(idx))
        return "";
    return PQgetvalue(m_res, m_cur, idx);
}

std::string PostgreSQLRes::getBlob(int idx)
{
    if (isNull(idx))
        return "";
    return PQgetvalue(m_res, m_cur, idx);
}

time_t PostgreSQLRes::getTime(int idx)
{
    if (isNull(idx))
        return 0;
    // PostgreSQL timestamp format parsing would go here
    // For simplicity, returning 0 for now
    return 0;
}

bool PostgreSQLRes::next()
{
    if (!m_res) {
        return false;
    }
    ++m_cur;
    return m_cur < PQntuples(m_res);
}

// PostgreSQLStmt class implementation
PostgreSQLStmt::ptr PostgreSQLStmt::Create(PostgreSQL::ptr db, const std::string &stmt)
{
    if (!db) {
        return nullptr;
    }
    PostgreSQLStmt::ptr rt(new PostgreSQLStmt(db, stmt));
    return rt;
}

PostgreSQLStmt::PostgreSQLStmt(PostgreSQL::ptr db, const std::string &stmt)
    : m_db(db), m_stmt(stmt), m_prepared(false)
{
    // Generate unique statement name
    static int stmt_counter = 0;
    m_stmtName = "stmt_" + std::to_string(++stmt_counter);

    // Count the number of parameters in the SQL statement by counting $n placeholders
    int paramCount = 0;
    for (size_t i = 0; i < m_stmt.length(); ++i) {
        if (m_stmt[i] == '$' && i + 1 < m_stmt.length() && std::isdigit(m_stmt[i + 1])) {
            int num = 0;
            size_t j = i + 1;
            while (j < m_stmt.length() && std::isdigit(m_stmt[j])) {
                num = num * 10 + (m_stmt[j] - '0');
                ++j;
            }
            if (num > paramCount) {
                paramCount = num;
            }
            i = j - 1; // Skip the digits we just processed
        }
    }
    // resize
    m_params.resize(paramCount);
    m_paramValues.resize(paramCount);
    m_paramLengths.resize(paramCount);
    m_paramFormats.resize(paramCount);
}

PostgreSQLStmt::~PostgreSQLStmt()
{
    if (m_prepared && m_db && m_db->getConn()) {
        std::string deallocate = "DEALLOCATE " + m_stmtName;
        PQexec(m_db->getConn(), deallocate.c_str());
    }
}

#define BIND_IMPL(idx, value)                                                                      \
    if (idx < 0)                                                                                   \
        return -1;                                                                                 \
    m_params[idx] = std::to_string(value);                                                         \
    m_paramValues[idx] = m_params[idx].c_str();                                                    \
    m_paramLengths[idx] = m_params[idx].length();                                                  \
    m_paramFormats[idx] = 0;                                                                       \
    return 0;

int PostgreSQLStmt::bindInt8(int idx, const int8_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindUint8(int idx, const uint8_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindInt16(int idx, const int16_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindUint16(int idx, const uint16_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindInt32(int idx, const int32_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindUint32(int idx, const uint32_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindInt64(int idx, const int64_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindUint64(int idx, const uint64_t &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindFloat(int idx, const float &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindDouble(int idx, const double &value)
{
    BIND_IMPL(idx, value)
}

int PostgreSQLStmt::bindString(int idx, const char *value)
{
    if (idx < 0 || !value)
        return -1;
    m_params[idx] = std::string(value);
    m_paramValues[idx] = m_params[idx].c_str();
    m_paramLengths[idx] = m_params[idx].length();
    m_paramFormats[idx] = 0;
    return 0;
}

int PostgreSQLStmt::bindString(int idx, const std::string &value)
{
    return bindString(idx, value.c_str());
}

int PostgreSQLStmt::bindBlob(int idx, const void *value, int64_t size)
{
    if (idx < 0 || !value || size < 0)
        return -1;
    m_params[idx] = std::string(static_cast<const char *>(value), size);
    m_paramValues[idx] = m_params[idx].c_str();
    m_paramLengths[idx] = size;
    m_paramFormats[idx] = 1; // Binary format
    return 0;
}

int PostgreSQLStmt::bindBlob(int idx, const std::string &value)
{
    return bindBlob(idx, value.data(), value.size());
}

int PostgreSQLStmt::bindTime(int idx, const time_t &value)
{
    if (idx < 0)
        return -1;
    // Convert time_t to PostgreSQL timestamp format
    struct tm *tm_info = localtime(&value);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    m_params[idx] = buffer;
    m_paramValues[idx] = m_params[idx].c_str();
    m_paramLengths[idx] = m_params[idx].length();
    m_paramFormats[idx] = 0;
    return 0;
}

int PostgreSQLStmt::bindNull(int idx)
{
    if (idx < 0)
        return -1;
    m_params[idx] = "";
    m_paramValues[idx] = nullptr;
    m_paramLengths[idx] = 0;
    m_paramFormats[idx] = 0;
    return 0;
}

ISQLData::ptr PostgreSQLStmt::query()
{
    if (!m_db || !m_db->getConn()) {
        return std::make_shared<PostgreSQLRes>(nullptr, -1, "Database connection is null");
    }

    // Prepare statement if not already prepared
    if (!m_prepared) {
        // Count the number of parameters in the SQL statement by counting $n placeholders
        int paramCount = 0;
        for (size_t i = 0; i < m_stmt.length(); ++i) {
            if (m_stmt[i] == '$' && i + 1 < m_stmt.length() && std::isdigit(m_stmt[i + 1])) {
                int num = 0;
                size_t j = i + 1;
                while (j < m_stmt.length() && std::isdigit(m_stmt[j])) {
                    num = num * 10 + (m_stmt[j] - '0');
                    ++j;
                }
                if (num > paramCount) {
                    paramCount = num;
                }
                i = j - 1; // Skip the digits we just processed
            }
        }

        PGresult *res =
            PQprepare(m_db->getConn(), m_stmtName.c_str(), m_stmt.c_str(), paramCount, nullptr);
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string err = PQerrorMessage(m_db->getConn());
            if (res)
                PQclear(res);
            return std::make_shared<PostgreSQLRes>(nullptr, -1, err.c_str());
        }
        PQclear(res);
        m_prepared = true;
    }

    // Execute prepared statement
    PGresult *res =
        PQexecPrepared(m_db->getConn(), m_stmtName.c_str(), m_paramValues.size(),
                       m_paramValues.data(), m_paramLengths.data(), m_paramFormats.data(), 0);

    if (!res) {
        return std::make_shared<PostgreSQLRes>(nullptr, -1, PQerrorMessage(m_db->getConn()));
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string err = PQerrorMessage(m_db->getConn());
        PQclear(res);
        return std::make_shared<PostgreSQLRes>(nullptr, -1, err.c_str());
    }

    return std::make_shared<PostgreSQLRes>(res, 0, "");
}

int PostgreSQLStmt::execute()
{
    auto res = query();
    return res->getErrno();
}

int64_t PostgreSQLStmt::getLastInsertId()
{
    return m_db->getLastInsertId();
}

int PostgreSQLStmt::getErrno()
{
    return 0;
}

std::string PostgreSQLStmt::getErrStr()
{
    if (!m_db || !m_db->getConn()) {
        return "Database connection is null";
    }
    return PQerrorMessage(m_db->getConn());
}

// PostgreSQLTransaction class implementation
PostgreSQLTransaction::PostgreSQLTransaction(PostgreSQL::ptr db, bool auto_commit)
    : m_db(db), m_status(0), m_autoCommit(auto_commit)
{
}

PostgreSQLTransaction::~PostgreSQLTransaction()
{
    if (m_status == 1 && m_autoCommit) {
        rollback();
    }
}

bool PostgreSQLTransaction::begin()
{
    if (!m_db) {
        return false;
    }
    int rt = m_db->execute("BEGIN");
    if (rt == 0) {
        m_status = 1;
    }
    return rt == 0;
}

bool PostgreSQLTransaction::commit()
{
    if (!m_db || m_status != 1) {
        return false;
    }
    int rt = m_db->execute("COMMIT");
    if (rt == 0) {
        m_status = 2;
    }
    return rt == 0;
}

bool PostgreSQLTransaction::rollback()
{
    if (!m_db || m_status != 1) {
        return false;
    }
    int rt = m_db->execute("ROLLBACK");
    if (rt == 0) {
        m_status = 3;
    }
    return rt == 0;
}

int PostgreSQLTransaction::execute(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = m_db->execute(format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLTransaction::execute(const std::string &sql)
{
    if (!m_db) {
        return -1;
    }
    return m_db->execute(sql);
}

int64_t PostgreSQLTransaction::getLastInsertId()
{
    if (!m_db) {
        return -1;
    }
    return m_db->getLastInsertId();
}

// PostgreSQLManager class implementation
PostgreSQLManager::PostgreSQLManager() : m_maxConn(10)
{
    sylar::Config::LoadFromConfDir("conf");
}

PostgreSQLManager::~PostgreSQLManager()
{
    MutexType::Lock lock(m_mutex);
    for (auto &i : m_conns) {
        for (auto &j : i.second) {
            delete j;
        }
    }
}

PostgreSQL::ptr PostgreSQLManager::get(const std::string &name)
{
    MutexType::Lock lock(m_mutex);
    auto it = m_conns.find(name);
    if (it != m_conns.end()) {
        if (!it->second.empty()) {
            PostgreSQL *m = it->second.front();
            it->second.pop_front();
            lock.unlock();
            if (!m->ping()) {
                delete m;
                return get(name);
            }
            return PostgreSQL::ptr(m, std::bind(&PostgreSQLManager::freePostgreSQL, this, name,
                                                std::placeholders::_1));
        }
    }

    auto config_it = m_dbDefines.find(name);
    if (config_it == m_dbDefines.end()) {
        return nullptr;
    }

    lock.unlock();
    PGconn *conn = postgresql_init(config_it->second);
    if (!conn) {
        return nullptr;
    }

    PostgreSQL *m = new PostgreSQL(conn);
    return PostgreSQL::ptr(
        m, std::bind(&PostgreSQLManager::freePostgreSQL, this, name, std::placeholders::_1));
}

void PostgreSQLManager::registerPostgreSQL(const std::string &name,
                                           const std::map<std::string, std::string> &params)
{
    MutexType::Lock lock(m_mutex);
    m_dbDefines[name] = params;
}

void PostgreSQLManager::checkConnection(int sec)
{
    time_t now = time(0);
    std::vector<PostgreSQL *> conns;
    MutexType::Lock lock(m_mutex);
    for (auto &i : m_conns) {
        for (auto it = i.second.begin(); it != i.second.end();) {
            if ((int)(now - (*it)->m_lastUsedTime) >= sec) {
                conns.push_back(*it);
                i.second.erase(it++);
            } else {
                ++it;
            }
        }
    }
    lock.unlock();

    for (auto &i : conns) {
        delete i;
    }
}

int PostgreSQLManager::execute(const std::string &name, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(name, format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLManager::execute(const std::string &name, const char *format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        return -1;
    }
    return conn->execute(format, ap);
}

int PostgreSQLManager::execute(const std::string &name, const std::string &sql)
{
    auto conn = get(name);
    if (!conn) {
        return -1;
    }
    return conn->execute(sql);
}

ISQLData::ptr PostgreSQLManager::query(const std::string &name, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    auto rt = query(name, format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr PostgreSQLManager::query(const std::string &name, const char *format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        return nullptr;
    }
    return conn->query(format, ap);
}

ISQLData::ptr PostgreSQLManager::query(const std::string &name, const std::string &sql)
{
    auto conn = get(name);
    if (!conn) {
        return nullptr;
    }
    return conn->query(sql);
}

PostgreSQLTransaction::ptr PostgreSQLManager::openTransaction(const std::string &name,
                                                              bool auto_commit)
{
    auto conn = get(name);
    if (!conn) {
        return nullptr;
    }
    return std::make_shared<PostgreSQLTransaction>(conn, auto_commit);
}

void PostgreSQLManager::freePostgreSQL(const std::string &name, PostgreSQL *m)
{
    MutexType::Lock lock(m_mutex);
    if (m_conns[name].size() < m_maxConn) {
        m_conns[name].push_back(m);
    } else {
        delete m;
    }
}

} // namespace sylar