#ifndef __SYLAR_MEMORYPOOL_H__
#define __SYLAR_MEMORYPOOL_H__

#include <cstddef>
#include <array>
#include <atomic>
#include <map>
#include "sylar/core/log/log.h"
#include "sylar/core/mutex.h"
#include "sylar/core/common/singleton.h"
// 包含必要的头文件以使用 mprotect 和 PROT_NONE 定义
#include <sys/mman.h>

#define SYLAR_THREAD_MALLOC(stackSize_) sylar::ThreadCache::GetInstance()->allocate(stackSize_)
#define SYLAR_THREAD_FREE(stack_, stackSize_)                                                      \
    sylar::ThreadCache::GetInstance()->deallocate(stack_, stackSize_)
#define SYLAR_MALLOC_PROTECT(ptr, size) sylar::ProtectStack(ptr, size)
#define SYLAR_MALLOC_UNPROTECT(ptr) sylar::UnprotectStack(ptr)

namespace sylar
{

constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 256 * 1024;                 // 256KB
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // ALIGNMENT等于指针void*的大小

constexpr size_t SPAN_PAGES = 8;

extern bool ProtectStack(void *ptr, std::size_t size);
extern void UnprotectStack(void *ptr);

// 内存块头部信息
struct BlockHeader {
    size_t size;       // 内存块大小
    bool inUse;        // 使用标志
    BlockHeader *next; // 指向下一个内存块
};

// 大小类管理
class SizeClass
{
public:
    static size_t roundUp(size_t bytes) { return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1); }

    static size_t getIndex(size_t bytes)
    {
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

class ThreadCache
{
public:
    static ThreadCache *GetInstance()
    {
        static thread_local ThreadCache instance;
        return &instance;
    }

    void *allocate(size_t size);
    void deallocate(void *ptr, size_t size);

private:
    ThreadCache();
    ~ThreadCache();
    // 从中心缓存获取内存
    void *fetchFromCentralCache(size_t index);
    // 归还内存到中心缓存
    void returnToCentralCache(size_t index);
    // 判断是否需要归还内存给中心缓存
    bool shouldReturnToCentralCache(size_t index);
    // 计算批量获取内存块的数量
    size_t getBatchNum(size_t size);

private:
    // freeList_ 是一个 FREE_LIST_SIZE 个 void* 数组。
    std::array<void *, FREE_LIST_SIZE> freeList_;
    std::array<size_t, FREE_LIST_SIZE> freeListSize_;
};

class CentralCache
{
public:
    static CentralCache &GetInstance()
    {
        static CentralCache instance;
        return instance;
    }

    // 分配 index 序号的内存大小
    void *fetchRange(size_t index, size_t batchNum, size_t &realBatchNum);

    // 返回，start首地址，size字节总长度，index返还对应的下标
    void returnRange(void *start, size_t size, size_t index);

private:
    CentralCache()
    {
        for (auto &ptr : centralFreeList_) {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        // 初始化所有锁
        for (auto &lock : locks_) {
            lock.clear();
        }
    }
    ~CentralCache();
    // 从页缓存获取内存
    void *fetchFromPageCache(size_t size, size_t batchNum, size_t &outNumPages);

private:
    std::array<std::atomic<void *>, FREE_LIST_SIZE> centralFreeList_;
    std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;
};

class PageCache
{
public:
    static const size_t PAGE_SIZE = 4096;

    static PageCache &GetInstance()
    {
        static PageCache instance;
        return instance;
    }

    void *allocateSpan(size_t numPages);

    void deallocateSpan(void *ptr, size_t numPages);

    static size_t getSpanPage(size_t size);

private:
    PageCache() = default;
    ~PageCache();

    // 向系统申请内存
    void *systemAlloc(size_t numPages);

private:
    struct Span {
        void *pageAddr;  // 页起始地址
        size_t numPages; // 页数
        Span *next;      // 链表指针
    };
    // 按页数管理空闲 Span ，不同页数对应不同 Span 链表
    std::map<size_t, Span *> freeSpans_;
    // 页首地址 到 span 的映射，用于回收
    // spanMap_不仅记录已分配的span，也记录空闲span
    std::map<void *, Span *> spanMap_;
    std::mutex mutex_;
};

struct MemoryPool_Initer {
    /// @brief 保证 缓存初始化顺序
    MemoryPool_Initer()
    {
        PageCache::GetInstance();
        CentralCache::GetInstance();
    }
};

static MemoryPool_Initer memoryPool_initer;

} // namespace sylar
#endif