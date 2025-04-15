#ifndef __SYLAR_MEMORYPOOL_H__
#define __SYLAR_MEMORYPOOL_H__

#include <cstddef>
#include <array>
#include <atomic>
#include <map>
#include "mutex.h"
#include "singleton.h"

namespace sylar{

constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 256 * 1024; // 256KB
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // ALIGNMENT等于指针void*的大小
   
constexpr size_t SPAN_PAGES = 8;

// 内存块头部信息
struct BlockHeader
{
    size_t size; // 内存块大小
    bool   inUse; // 使用标志
    BlockHeader* next; // 指向下一个内存块
};

// 大小类管理
class SizeClass 
{
public:
    static size_t roundUp(size_t bytes)
    {
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    static size_t getIndex(size_t bytes)
    {   
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

class ThreadCache{
public:
    ThreadCache();

    ~ThreadCache();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

private:
    // 从中心缓存获取内存
    void* fetchFromCentralCache(size_t index);
    // 归还内存到中心缓存
    void returnToCentralCache(void* start, size_t size);
    bool shouldReturnToCentralCache(size_t index);
    size_t getBatchNum(size_t size);

private:
    // freeList_ 是一个 FREE_LIST_SIZE 个 void* 数组。
    std::array<void*, FREE_LIST_SIZE> freeList_;    
    std::array<size_t, FREE_LIST_SIZE> freeListSize_;
};



class CentralCache{
public:
    using MutexType = Spinlock;

    CentralCache(){}
    ~CentralCache();

    // 分配 index 序号的内存大小 
    void* fetchRange(size_t index, size_t batchNum, size_t& realBatchNum);

    // 返回，start首地址，size字节总长度，bytes返还对应的下标
    void returnRange(void* start, size_t size, size_t index);

private:
    void* fetchFromPageCache(size_t size, size_t batchNum, size_t& outNumPages);
    
private:
    std::array<void*, FREE_LIST_SIZE> centralFreeList_;
    std::array<MutexType, FREE_LIST_SIZE> locks_;
};

class PageCache{
public:
    using MutexType = CASLock;

    static const size_t PAGE_SIZE = 4096;

    void* allocateSpan(size_t numPages);

    void deallocateSpan(void* ptr, size_t numPages);

    static size_t getSpanPage(size_t size);
    PageCache() = default;
    ~PageCache();

private:
    // 向系统申请内存
    void* systemAlloc(size_t numPages);

private:
    struct Span{
        void*  pageAddr; // 页起始地址
        size_t numPages; // 页数
        Span*  next;     // 链表指针
    };
    // 按页数管理空闲 Span ，不同页数对应不同 Span 链表
    std::map<size_t, Span*> freeSpans_;
    // 页首地址 到 span 的映射，用于回收
    // spanMap_不仅记录已分配的span，也记录空闲span
    std::map<void*, Span*> spanMap_;
    MutexType mutex_;
};

typedef sylar::ThreadLocalSingleton<ThreadCache> threadCache;
typedef sylar::Singleton<CentralCache> centralCache;
typedef sylar::Singleton<PageCache> pageCache;


}
#endif