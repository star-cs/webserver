#include "../sylar/config.h"
#include "../sylar/log.h"
#include "../sylar/util.h"

sylar::ConfigVar<int>::ptr g_int_value_config = 
    sylar::Config::Lookup("server.port", (int)8080, "server port");

int main(int argc, char** argv){
    SYLAR_LOG_INFO(SYLAR_ROOT()) << g_int_value_config->getName();
    SYLAR_LOG_INFO(SYLAR_ROOT()) << g_int_value_config->getValue();
    SYLAR_LOG_INFO(SYLAR_ROOT()) << g_int_value_config->getDescription();
}