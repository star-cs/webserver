#ifndef __SYLAR_MACRO_H__
#define __SYLAR_MACRO_H__

#include "sylar/core/util/util.h"

#include <assert.h>

#if defined __GNUC__ || defined __llvm__
/// AS_LIKELY 宏的封装, 告诉编译器优化,条件大概率成立
#define AS_LIKELY(x) __builtin_expect(!!(x), 1)
/// AS_UNLIKELY 宏的封装, 告诉编译器优化,条件大概率不成立
#define AS_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define AS_LIKELY(x) (x)
#define AS_UNLIKELY(x) (x)
#endif

// #if __has_cpp_attribute(likely) && __has_cpp_attribute(unlikely)
// #define AS_LIKELY [[likely]]
// #define AS_UNLIKELY [[unlikely]]
// #else
// #define AS_LIKELY
// #define AS_UNLIKELY
// #endif


#define SYLAR_ASSERT(x) \
    if(!(x)){ \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION : " #x   \
                                          << "\nbacktrace:\n"    \
                                          << sylar::BacktraceToString(100, 2, "         "); \
        assert(x);  \
    } \

#define SYLAR_ASSERT2(x, w) \
    if(!(x)){ \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION : " #x   \
                                          << "\n" << w \
                                          << "\nbacktrace:\n"    \
                                          << sylar::BacktraceToString(100, 2, "         "); \
        assert(x);  \
    } \

#endif