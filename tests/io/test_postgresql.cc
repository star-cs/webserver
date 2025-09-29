#include "sylar/io/db/postgresql.h"
#include "sylar/core/log/log.h"
#include <iostream>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_postgresql_connection() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Connection ===";
    
    // 测试连接创建
    auto db = sylar::PostgreSQL::Create("localhost", 5433, "test_user", "test_password", "test_db");
    if (!db) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create PostgreSQL connection";
        return;
    }
    
    SYLAR_LOG_INFO(g_logger) << "PostgreSQL connection created successfully";
    
    // 测试ping
    if (db->ping()) {
        SYLAR_LOG_INFO(g_logger) << "PostgreSQL ping successful";
    } else {
        SYLAR_LOG_ERROR(g_logger) << "PostgreSQL ping failed: " << db->getErrStr();
    }
}

void test_postgresql_execute() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Execute ===";
    
    auto db = sylar::PostgreSQL::Create("localhost", 5433, "test_user", "test_password", "test_db");
    if (!db) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create PostgreSQL connection";
        return;
    }
    
    // 测试创建表
    std::string create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS test_users (
            id SERIAL PRIMARY KEY,
            name VARCHAR(100) NOT NULL,
            email VARCHAR(100) UNIQUE NOT NULL,
            age INTEGER,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    int ret = db->execute(create_table_sql);
    if (ret == 0) {
        SYLAR_LOG_INFO(g_logger) << "Table created successfully";
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create table: " << db->getErrStr();
    }
    
    // 测试插入数据
    ret = db->execute("INSERT INTO test_users (name, email, age) VALUES ('张三', 'zhangsan@example.com', 25)");
    if (ret == 0) {
        SYLAR_LOG_INFO(g_logger) << "Data inserted successfully";
        SYLAR_LOG_INFO(g_logger) << "Last insert ID: " << db->getLastInsertId();
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Failed to insert data: " << db->getErrStr();
    }
}

void test_postgresql_query() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Query ===";
    
    auto db = sylar::PostgreSQL::Create("localhost", 5433, "test_user", "test_password", "test_db");
    if (!db) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create PostgreSQL connection";
        return;
    }
    
    // 测试查询数据
    auto res = db->query("SELECT id, name, email, age FROM test_users LIMIT 5");
    if (!res) {
        SYLAR_LOG_ERROR(g_logger) << "Query failed: " << db->getErrStr();
        return;
    }
    
    SYLAR_LOG_INFO(g_logger) << "Query successful, rows: " << res->getDataCount() 
                             << ", columns: " << res->getColumnCount();
    
    // 遍历结果
    while (res->next()) {
        SYLAR_LOG_INFO(g_logger) << "Row: id=" << res->getInt32(0) 
                                 << ", name=" << res->getString(1)
                                 << ", email=" << res->getString(2)
                                 << ", age=" << res->getInt32(3);
    }
}

void test_postgresql_stmt() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Prepared Statement ===";
    
    auto db = sylar::PostgreSQL::Create("localhost", 5433, "test_user", "test_password", "test_db");
    if (!db) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create PostgreSQL connection";
        return;
    }
    
    // 测试预处理语句
    auto stmt = db->prepare("INSERT INTO test_users (name, email, age) VALUES ($1, $2, $3)");
    if (!stmt) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to prepare statement: " << db->getErrStr();
        return;
    }
    
    // 绑定参数并执行
    stmt->bindString(0, "李四");
    stmt->bindString(1, "222 lisi@example.com");
    stmt->bindInt32(2, 30);
    
    int ret = stmt->execute();
    if (ret == 0) {
        SYLAR_LOG_INFO(g_logger) << "Prepared statement executed successfully";
        SYLAR_LOG_INFO(g_logger) << "Last insert ID: " << stmt->getLastInsertId();
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Failed to execute prepared statement: " << stmt->getErrStr();
    }
}

void test_postgresql_transaction() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Transaction ===";
    
    auto db = sylar::PostgreSQL::Create("localhost", 5433, "test_user", "test_password", "test_db");
    if (!db) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to create PostgreSQL connection";
        return;
    }
    
    // 测试事务
    auto trans = db->openTransaction();
    if (!trans) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to open transaction";
        return;
    }
    
    if (!trans->begin()) {
        SYLAR_LOG_ERROR(g_logger) << "Failed to begin transaction";
        return;
    }
    
    // 在事务中执行操作
    int ret = trans->execute("INSERT INTO test_users (name, email, age) VALUES ('王五', 'wangwu@example.com', 28)");
    if (ret == 0) {
        SYLAR_LOG_INFO(g_logger) << "Transaction execute successful";
        if (trans->commit()) {
            SYLAR_LOG_INFO(g_logger) << "Transaction committed successfully";
        } else {
            SYLAR_LOG_ERROR(g_logger) << "Failed to commit transaction";
        }
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Transaction execute failed, rolling back";
        trans->rollback();
    }
}

void test_postgresql_manager() {
    SYLAR_LOG_INFO(g_logger) << "=== Testing PostgreSQL Manager ===";
    
    // 注册数据库连接
    std::map<std::string, std::string> params;
    params["host"] = "localhost";
    params["port"] = "5433";
    params["user"] = "test_user";
    params["passwd"] = "test_password";
    params["dbname"] = "test_db";
    
    sylar::PostgreSQLMgr::GetInstance()->registerPostgreSQL("test_db", params);
    
    // 使用管理器执行查询
    auto res = sylar::PostgreSQLMgr::GetInstance()->query("test_db", "SELECT COUNT(*) FROM test_users");
    if (res && res->next()) {
        SYLAR_LOG_INFO(g_logger) << "Total users: " << res->getInt64(0);
    } else {
        SYLAR_LOG_ERROR(g_logger) << "Manager query failed";
    }
}

int main() {
    SYLAR_LOG_INFO(g_logger) << "Starting PostgreSQL tests...";
    
    
    test_postgresql_connection();
    test_postgresql_execute();
    test_postgresql_query();
    test_postgresql_stmt();
    test_postgresql_transaction();
    test_postgresql_manager();
    
    SYLAR_LOG_INFO(g_logger) << "PostgreSQL tests completed.";
    
    return 0;
}