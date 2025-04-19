#include "memorypool.h"
#include "config.h"
#include <sys/mman.h>

namespace sylar{
// 支持创建多个
static sylar::ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_memPool_config =
                    sylar::Config::Lookup("memory_pool", std::map<std::string, std::map<std::string, std::string>>(), "MemoryPool");


// ********************************* ThreadCache *********************************
ThreadCache::ThreadCache(){
    freeList_.fill(nullptr);
    freeListSize_.fill(0);
}

ThreadCache::~ThreadCache(){
    for(size_t i = 0 ; i < FREE_LIST_SIZE ; ++i){
        if (freeList_[i]) {
            size_t blockSize = (i + 1) * ALIGNMENT;
            size_t blockNum = freeListSize_[i];
            centralCache::GetInstance()->returnRange(freeList_[i], blockNum * blockSize, i);
            freeList_[i] = nullptr;
            freeListSize_[i] = 0;
        }
    }
}

void* ThreadCache::allocate(size_t size){
    if(size == 0){
        size = ALIGNMENT;
    }

    if(size > MAX_BYTES){
        return malloc(size);
    }

    size_t index = SizeClass::getIndex(size);

    freeListSize_[index]--;

    if(void* ptr = freeList_[index]){
        // ptr void*类型强转为void**，再解引用即可得到 ptr->next 的值
        freeList_[index] = *reinterpret_cast<void**>(ptr);
        return ptr;
    }

    // 线程本地自由链表为空，则从中心缓存获取一批内存
    return fetchFromCentralCache(index);
}
void ThreadCache::deallocate(void* ptr, size_t size){
    if(size > MAX_BYTES){
        free(ptr);
        return;
    }

    size_t index = SizeClass::getIndex(size);

    // 插入到线程本地自由链表
    *reinterpret_cast<void**>(ptr) = freeList_[index];
    freeList_[index] = ptr;

    freeListSize_[index]++;

    // 判断是否需要将部分内存回收到中心缓存？
    if(shouldReturnToCentralCache(index)){
        returnToCentralCache(freeList_[index], size);
    }
}

void* ThreadCache::fetchFromCentralCache(size_t index){
    // 从中心缓存批量获取内存
    size_t size = (index + 1) * ALIGNMENT;
    size_t batchNum = getBatchNum(size);
    // 返回 batchNum 个 哈希序号为index 的块
    size_t realBatchNum;
    void* start = centralCache::GetInstance()->fetchRange(index, batchNum, realBatchNum);
    if(!start) return nullptr;

    // 调用 fetchFromCentralCache 之前已经减-1了
    freeListSize_[index] += realBatchNum;

    // 取一个返回，其余放入自由链表
    void* result = start;
    if(batchNum > 1){
        freeList_[index] = *reinterpret_cast<void**>(start);
    }
    *reinterpret_cast<void**>(result) = nullptr;
    
    
    return result;
}

void ThreadCache::returnToCentralCache(void* start, size_t size){
    size_t index = SizeClass::getIndex(size);

    // 补全
    size_t alignedSize = SizeClass::roundUp(size);

    size_t batchNum = freeListSize_[index];
    if(batchNum <= 1) return; // 如果只有一个块，则不归还

    size_t keepNum = std::max(batchNum / 4, size_t(1));
    size_t returnNum = batchNum - keepNum;

    /// 字节偏移，以 1 字节为单位。
    char* current = static_cast<char*>(start);

    char* splitNode = current;
    for(size_t i = 0; i < keepNum - 1; ++i){
        splitNode = reinterpret_cast<char*>(*reinterpret_cast<void**>(splitNode));
        if(splitNode == nullptr){
            
            // 如果链表提前结束，更新实际的返回数量
            returnNum = batchNum - (i+1);
            break;
        }
    }

    if(splitNode != nullptr){
        void* nextNode = *reinterpret_cast<void**>(splitNode);
        *reinterpret_cast<void**>(splitNode) = nullptr;

        freeList_[index] = start;

        freeListSize_[index] = keepNum;

        if(returnNum > 0 && nextNode != nullptr){
            centralCache::GetInstance()->returnRange(nextNode, returnNum * alignedSize, index);
        }
    }
}

bool ThreadCache::shouldReturnToCentralCache(size_t index){
    // 设定阈值，当自由列表的大小超过一定数量时，返回到中心缓存
    size_t threshold = 64;
    return (freeListSize_[index] > threshold); 
}

size_t ThreadCache::getBatchNum(size_t size){
    constexpr size_t MAX_BATCH_SIZE = 4 * 1024;     // 4KB

    size_t baseNum;
    if(size <= 32) baseNum = 64;
    else if(size <= 64) baseNum = 32;
    else if(size <= 128) baseNum = 16;
    else if(size <= 256) baseNum = 8;
    else if(size <= 512) baseNum = 4;
    else if(size <= 1024) baseNum = 2;
    else baseNum = 1;                       // 大于 1024的对象每次只从中心缓存取1个

    size_t maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size); // 确保不小于1

    return std::max(size_t(1), std::min(maxNum, baseNum));  
}

// ********************************* CentralCache *********************************
CentralCache::~CentralCache(){
    // 仅需清理状态，内存由 PageCache 释放
    for (size_t i = 0; i < FREE_LIST_SIZE; ++i) {
        centralFreeList_[i] = nullptr;
    }
    printf("~CentralCache()");
}


// batchNum返回实际分配的块数量
void* CentralCache::fetchRange(size_t index, size_t batchNum, size_t& realBatchNum){
    if(index >= FREE_LIST_SIZE || batchNum == 0){
        return nullptr;
    }

    MutexType::Lock lock(locks_[index]);

    void* result = nullptr;
    try{
        result = centralFreeList_[index];

        if(!result){
            // 中心缓存为空，从页缓存获取新的内存块
            size_t size = (index + 1) * ALIGNMENT;
            size_t numPages;
            result = fetchFromPageCache(size, batchNum, numPages);
            if(!result){
                return nullptr;
            }

            // 将获取的内存块切分为小块
        
            // 分配到的 块数量  
            size_t totalBlocks = (numPages * PageCache::PAGE_SIZE) / size;  
            size_t allocBlocks = std::min(batchNum, totalBlocks);
            realBatchNum = allocBlocks;

            // 转成char*进行 地址加减运算 
            char* start = static_cast<char*>(result);

            // result 返回的链表 allocBlocks块
            if(allocBlocks > 1){
                for(size_t i = 1; i < allocBlocks ; i++){
                    void* current = start + (i-1) * size;
                    void* next = start + i * size;
                    *reinterpret_cast<void**>(current) = next;
                }
                *reinterpret_cast<void**>(start + (allocBlocks - 1) * size) = nullptr;
            }

            // 还剩下的 添加到 自由链表
            if(totalBlocks > allocBlocks){
                void* remainStart = start + allocBlocks * size;
                for(size_t i = allocBlocks + 1 ; i < totalBlocks ; ++i){
                    void* current = start  + (i-1) * size;
                    void* next = start + i * size;
                    *reinterpret_cast<void**>(current) = next;
                }
                *reinterpret_cast<void**>(start + (totalBlocks - 1) * size) = nullptr;

                centralFreeList_[index] = remainStart;
            }
        }else{
            // 中心缓存有index对应大小的内存块
            void* current = result;
            void* prev = nullptr;
            size_t count = 0;

            while(current && count < batchNum){
                prev = current;
                current = *reinterpret_cast<void**>(current);
                count++;
            }

            realBatchNum = count; // 这里同上，只会获得已有的。

            if(prev){
                *reinterpret_cast<void**>(prev) = nullptr;  // 断开
            }
            centralFreeList_[index] = current;
        }
    }catch(...){
        throw;
    }

    return result;
}

void CentralCache::returnRange(void* start, size_t size, size_t index){
    if(!start || size > FREE_LIST_SIZE)
        return;

    MutexType::Lock lock(locks_[index]);

    try{
        size_t blockSize = (index + 1) * ALIGNMENT;
        size_t blockNum  = size / blockSize;

        // 找到要归还的链表的最后一个节点
        void* end = start;
        size_t count = 1;
        while(*reinterpret_cast<void**>(end) != nullptr && count < blockNum){
            end = *reinterpret_cast<void**>(end);
            count ++;
        }

        void* current = centralFreeList_[index];
        *reinterpret_cast<void**>(end) = current;
        centralFreeList_[index] = start;

    }catch(...){
        throw;
    }
}


void* CentralCache::fetchFromPageCache(size_t size, size_t batchNum, size_t& outNumPages){
    size_t totalSize = batchNum * size;
    outNumPages = PageCache::getSpanPage(totalSize);
    return pageCache::GetInstance()->allocateSpan(outNumPages);
}


// ********************************* PageCache *********************************

void* PageCache::allocateSpan(size_t numPages){
    MutexType::Lock lock(mutex_);

    // 寻找合适的 空闲的span
    // lower_bound 函数返回第一个大于等于numPages的元素迭代器
    auto it = freeSpans_.lower_bound(numPages);
    if(it != freeSpans_.end()){
        Span* span = it->second; 

        // 将取出的span从原来的空间链表freeSpans_[it->first]中移除
        if(span->next){
            freeSpans_[it->first] = span->next;
        }else{
            freeSpans_.erase(it);
        }

        // 如果 span 的 numPages 大于 需要的 numPages
        if(span->numPages > numPages){
            Span* newSpan = new Span;
            newSpan->pageAddr = static_cast<char*>(span->pageAddr) + numPages * PAGE_SIZE;
            newSpan->numPages = span->numPages - numPages;
            newSpan->next = nullptr;
            // 记录空闲span
            spanMap_[newSpan->pageAddr] = newSpan;

            // 超出的部分 newSpan 放回 freeSpans_列表头部
            auto& list = freeSpans_[newSpan->numPages];
            newSpan->next = list;
            list = newSpan;

            span->numPages = numPages;
        }

        // 记录分配span信息用于回收
        // 理论上这个 span->pageAddr在创建的时候已经记录了一遍
        // spanMap_[span->pageAddr] = span;
        return span->pageAddr;
    }
    
    // 没有合适的Span，向系统申请
    void* memory = systemAlloc(numPages);
    if(!memory) return nullptr;

    Span* span = new Span;
    span->pageAddr = memory;
    span->numPages = numPages;
    span->next = nullptr;

    spanMap_[span->pageAddr] = span;
    return memory;
}

void PageCache::deallocateSpan(void* ptr, size_t numPages){
    MutexType::Lock lock(mutex_);

    // 查找对应的 span
    auto it = spanMap_.find(ptr);
    if(it == spanMap_.end()) return;

    Span* span = it->second;

    // 尝试合并相邻的span
    void* nextAddr = static_cast<char*>(ptr) + numPages * PAGE_SIZE;
    auto nextIt = spanMap_.find(nextAddr);

    if(nextIt != spanMap_.end()){
        Span* nextSpan = nextIt->second;
        size_t nextSize = nextSpan->numPages;

        auto& nextList = freeSpans_[nextSize];

        Span dummy;
        dummy.next = nextList;
        Span* prev = &dummy;
        bool found = false;

        while(prev->next){
            if(prev->next == nextSpan){
                prev->next = prev->next->next;
                found = true;
                break;
            }
            prev = prev->next;
        }

        if(found){
            // 更新链表并合并
            nextList = dummy.next;
            span->numPages += nextSpan->numPages;
            spanMap_.erase(nextAddr);
            delete nextSpan;
        }
    }

    auto& targetList = freeSpans_[span->numPages];
    span->next = targetList;
    targetList = span;
}


// 当size <= 32KB，默认获取32KB，8页。如果size > 32KB，就按实际需求分配
size_t PageCache::getSpanPage(size_t size){
    if(size > PAGE_SIZE * SPAN_PAGES){
        return (size + PAGE_SIZE - 1) / PAGE_SIZE;
    }else{
        return SPAN_PAGES;
    }
}

void* PageCache::systemAlloc(size_t numPages){
    size_t size = numPages * PAGE_SIZE;

    // 使用mmap分配内存
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(ptr == MAP_FAILED) return nullptr;

    // 清零内存
    memset(ptr, 0, size);
    return ptr;
}

PageCache::~PageCache(){
    // 释放所有 Span 占用的系统内存
    for (auto& entry : spanMap_) {
        Span* span = entry.second;
        if (span->pageAddr) {
            munmap(span->pageAddr, span->numPages * PAGE_SIZE);
            span->pageAddr = nullptr;
        }
        delete span;  // 释放 Span 对象本身
    }
    spanMap_.clear();
    freeSpans_.clear();
    printf("~PageCache()");
}

}
