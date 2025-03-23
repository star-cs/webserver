#ifndef __SYLAR__UTIL_H__
#define __SYLAR__UTIL_H__

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace sylar{

/**
 * @brief 获取线程id
 * @note 这里不要把pid_t和pthread_t混淆，关于它们之的区别可参考gettid(2)
 */
pid_t GetThreadId();

/**
 * @brief 获取协程id
 * @todo 桩函数，暂时返回0，等协程模块完善后再返回实际值
 */
uint64_t GetFiberId();

/**
 * @brief 获取线程名称，参考pthread_getname_np(3)
 */
std::string GetThreadName();

/**
 * @brief 设置线程名称，参考pthread_setname_np(3)
 * @note 线程名称不能超过16字节，包括结尾的'\0'字符
 */
void SetThreadName(const std::string &name);

/**
 * @brief 获取当前的调用栈
 * @param[out] bt 保存调用栈
 * @param[in] size 最多返回层数
 * @param[in] skip 跳过栈顶的层数
 */
void Backtrace(std::vector<std::string>& bt, int size, int skip);

/**
 * @brief 获取当前栈信息的字符串
 * @param[in] size 栈的最大层数
 * @param[in] skip 跳过栈顶的层数
 * @param[in] prefix 栈信息前输出的内容
 */
std::string BacktraceToString(int size = 64, int skip = 2, const std::string &prefix = "");

// 返回当前时间的毫秒
uint64_t GetCurrentTimeMS();

// 返回当前时间的微秒
uint64_t GetCurrentTimeUS();


}

#endif