#include "orm_out/test/orm/user_info.h"
#include "sylar/io/db/sqlite3.h"
#include "sylar/io/db/mysql.h"

/**
 * @brief 主函数，用于测试数据库操作功能
 * 
 * 根据命令行参数决定使用 SQLite3 或 MySQL 数据库。
 * 执行一系列数据库操作，包括创建表、插入数据、查询、更新和删除等。
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出状态码，0 表示正常退出
 */
int main(int argc, char **argv)
{
    sylar::IDB::ptr db;

    // 如果没有传入命令行参数，则使用 SQLite3 数据库
    if (argc == 1) {
        db = sylar::SQLite3::Create("abc.db");
        std::cout << "create table: " << test::orm::UserInfoDao::CreateTableSQLite3(db)
                  << std::endl;
    } else {
        // 否则使用 MySQL 数据库，并配置连接参数
        std::map<std::string, std::string> params;
        params["host"] = "127.0.0.1";
        params["port"] = "3307";
        params["user"] = "root";
        params["passwd"] = "12345mysql";
        params["dbname"] = "sylar";

        sylar::MySQL::ptr m(new sylar::MySQL(params));
        m->connect();
        db = m;
        // std::cout << "create table: " << test::orm::UserInfoDao::CreateTableMySQL(db) << std::endl;
    }

    // 插入 10 条测试数据到数据库中
    for (int i = 0; i < 10; ++i) {
        test::orm::UserInfo::ptr u(new test::orm::UserInfo);
        u->setName("name_a" + std::to_string(i));
        u->setEmail("mail_a" + std::to_string(i));
        u->setPhone("phone_a" + std::to_string(i));
        u->setStatus(i % 2);

        std::cout << "i= " << i << " - " << test::orm::UserInfoDao::Insert(u, db);
        std::cout << " - " << u->toJsonString() << std::endl;
    }

    // 查询 status 为 1 的用户信息
    std::vector<test::orm::UserInfo::ptr> us;
    std::cout << "query_by_status: " << test::orm::UserInfoDao::QueryByStatus(us, 1, db)
              << std::endl;

    // 更新查询到的用户信息（在名字后添加 "_new"）
    for (size_t i = 0; i < us.size(); ++i) {
        std::cout << "i=" << i << " - " << us[i]->toJsonString() << std::endl;
        us[i]->setName(us[i]->getName() + "_new");
        test::orm::UserInfoDao::Update(us[i], db);
    }

    // 删除 status 为 1 的用户信息
    std::cout << "delete: " << test::orm::UserInfoDao::DeleteByStatus(1, db) << std::endl;

    // 再次查询 status 为 0 的用户信息并输出
    us.clear();
    std::cout << "query_by_status: " << test::orm::UserInfoDao::QueryByStatus(us, 0, db)
              << std::endl;
    for (size_t i = 0; i < us.size(); ++i) {
        std::cout << "i=" << i << " - " << us[i]->toJsonString() << std::endl;
    }

    // 查询所有用户信息并输出
    us.clear();
    std::cout << "query_all: " << test::orm::UserInfoDao::QueryAll(us, db) << std::endl;
    for (size_t i = 0; i < us.size(); ++i) {
        std::cout << "i=" << i << " - " << us[i]->toJsonString() << std::endl;
    }

    return 0;
}
