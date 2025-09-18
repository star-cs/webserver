#include "async_socket_stream.h"
#include "sylar/core/util/util.h"
#include "sylar/core/log/log.h"
#include "sylar/core/common/macro.h"

namespace sylar
{

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * @brief 初始化上下文对象的成员变量
 * @details 将序列号、超时时间、结果等初始化为默认值
 */
AsyncSocketStream::Ctx::Ctx() : sn(0), timeout(0), result(0), timed(false), scheduler(nullptr)
{
}

/**
 * @brief 处理上下文的响应
 * @details 使用CAS操作确保回调只被执行一次，取消定时器，并将等待的协程重新调度到调度器中
 * 如果发生超时，设置相应的结果和描述字符串
 */
void AsyncSocketStream::Ctx::doRsp()
{
    Scheduler *scd = scheduler;
    // 使用CAS操作确保回调只被执行一次
    if (!sylar::Atomic::compareAndSwapBool(scheduler, scd, (Scheduler *)nullptr)) {
        return;
    }
    SYLAR_LOG_DEBUG(g_logger) << "scd=" << scd << " fiber=" << fiber;
    if (!scd || !fiber) {
        return;
    }
    // 取消定时器，避免重复回调
    if (timer) {
        timer->cancel();
        timer = nullptr;
    }

    // 如果超时，设置相应的结果和描述
    if (timed) {
        result = TIMEOUT;
        resultStr = "timeout";
    }
    // 将协程重新调度到调度器中继续执行
    scd->schedule(&fiber);
}

/**
 * @brief 构造函数，初始化异步Socket流
 * @param sock Socket对象指针
 * @param owner 是否拥有该Socket的所有权
 * @details 初始化父类SocketStream，设置信号量初始值，初始化序列号、自动重连状态等成员变量
 */
AsyncSocketStream::AsyncSocketStream(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner), m_waitSem(2), m_sn(0), m_autoConnect(false), m_tryConnectCount(0),
      m_iomanager(nullptr), m_worker(nullptr)
{
}

/**
 * @brief 启动异步Socket流
 * @return 启动是否成功
 * @details 初始化IOManager，尝试连接Socket，执行连接回调，启动读写协程
 * 如果启动失败且开启了自动重连，则设置定时器进行重试
 */
bool AsyncSocketStream::start()
{
    // 如果未设置IOManager，使用当前线程的IOManager
    if (!m_iomanager) {
        m_iomanager = sylar::IOManager::GetThis();
    }
    // 如果未设置Worker，使用当前线程的IOManager
    if (!m_worker) {
        m_worker = sylar::IOManager::GetThis();
    }

    do {
        // 等待之前的读写协程结束
        waitFiber();

        // 取消之前的定时器
        if (m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }

        // 如果未连接，尝试重连
        if (!isConnected()) {
            if (!m_socket->reconnect()) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        // 执行连接回调，如果回调返回false则启动失败
        if (m_connectCb) {
            if (!m_connectCb(shared_from_this())) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        // 启动读写协程
        startRead();
        startWrite();

        // 重置尝试连接次数，表示连接成功
        m_tryConnectCount = 0;
        return true;
    } while (false);
    
    // 启动失败，增加尝试连接次数
    ++m_tryConnectCount;
    // 如果开启了自动重连，设置定时器进行重试
    if (m_autoConnect) {
        if (m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }

        // 计算等待时间，使用指数退避算法，最大2000ms
        uint64_t wait_ts = m_tryConnectCount * 2 * 50;
        if (wait_ts > 2000) {
            wait_ts = 2000;
        }
        // 设置定时器，到时后重新执行start方法
        m_timer = m_iomanager->addTimer(wait_ts,
                                        std::bind(&AsyncSocketStream::start, shared_from_this()));
    }
    return false;
}

/**
 * @brief 执行读操作
 * @details 循环读取数据，直到连接断开或发生异常
 * 每次读取后调用doRecv处理数据并回复
 * 读取完毕或发生异常时关闭连接，如果开启了自动重连则设置定时器重试
 */
void AsyncSocketStream::doRead()
{
    try {
        // 循环读取数据，直到连接断开
        while (isConnected()) {
            recving = true;
            // 调用子类实现的doRecv方法处理接收到的数据
            auto ctx = doRecv();
            recving = false;
            // 如果有上下文，执行响应处理
            if (ctx) {
                ctx->doRsp();
            }
        }
    } catch (...) {
        //TODO log 异常处理
    }

    SYLAR_LOG_DEBUG(g_logger) << "doRead out " << this;
    // 内部关闭连接
    innerClose();
    m_waitSem.notify();

    // 如果开启了自连动重，设置定时器重新启动
    if (m_autoConnect) {
        m_iomanager->addTimer(10, std::bind(&AsyncSocketStream::start, shared_from_this()));
    }
}

/**
 * @brief 执行写操作
 * @details 从队列中取出发送上下文并执行发送操作
 * 发送过程中发生错误时关闭连接
 * 发送完毕或发生异常时清理队列并通知等待协程
 */
void AsyncSocketStream::doWrite()
{
    try {
        // 循环发送数据，直到连接断开
        while (isConnected()) {
            // 等待信号量，直到有数据需要发送
            m_sem.wait();
            std::list<SendCtx::ptr> ctxs;
            {
                RWMutexType::WriteLock lock(m_queueMutex);
                // 交换队列，减少锁的持有时间
                m_queue.swap(ctxs);
            }
            auto self = shared_from_this();
            // 逐个执行发送操作
            for (auto &i : ctxs) {
                if (!i->doSend(self)) {
                    // 发送失败，关闭连接
                    innerClose();
                    break;
                }
            }
        }
    } catch (...) {
        //TODO log 异常处理
    }
    SYLAR_LOG_DEBUG(g_logger) << "doWrite out " << this;
    // 清理发送队列
    {
        RWMutexType::WriteLock lock(m_queueMutex);
        m_queue.clear();
    }
    m_waitSem.notify();
}

/**
 * @brief 启动读协程
 * @details 在IOManager中调度doRead方法，开始异步读操作
 */
void AsyncSocketStream::startRead()
{
    m_iomanager->schedule(std::bind(&AsyncSocketStream::doRead, shared_from_this()));
}

/**
 * @brief 启动写协程
 * @details 在IOManager中调度doWrite方法，开始异步写操作
 */
void AsyncSocketStream::startWrite()
{
    m_iomanager->schedule(std::bind(&AsyncSocketStream::doWrite, shared_from_this()));
}

/**
 * @brief 处理超时事件
 * @param ctx 上下文对象指针
 * @details 从上下文库中删除超时的上下文，设置超时状态，并执行响应处理
 */
void AsyncSocketStream::onTimeOut(Ctx::ptr ctx)
{
    SYLAR_LOG_DEBUG(g_logger) << "onTimeOut " << ctx;
    {
        RWMutexType::WriteLock lock(m_mutex);
        // 从上下文库中删除超时的上下文
        m_ctxs.erase(ctx->sn);
    }
    // 设置超时状态
    ctx->timed = true;
    // 执行响应处理
    ctx->doRsp();
}

/**
 * @brief 根据序列号获取上下文
 * @param sn 序列号
 * @return 上下文对象指针，如果不存在则返回nullptr
 * @details 使用读锁保护上下文库的并发访问
 */
AsyncSocketStream::Ctx::ptr AsyncSocketStream::getCtx(uint32_t sn)
{
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    return it != m_ctxs.end() ? it->second : nullptr;
}

/**
 * @brief 根据序列号获取并删除上下文
 * @param sn 序列号
 * @return 上下文对象指针，如果不存在则返回nullptr
 * @details 使用写锁保护上下文库的并发访问，确保删除操作的原子性
 */
AsyncSocketStream::Ctx::ptr AsyncSocketStream::getAndDelCtx(uint32_t sn)
{
    Ctx::ptr ctx;
    RWMutexType::WriteLock lock(m_mutex);
    auto it = m_ctxs.find(sn);
    if (it != m_ctxs.end()) {
        ctx = it->second;
        m_ctxs.erase(it);
    }
    return ctx;
}

/**
 * @brief 添加上下文到上下文库
 * @param ctx 上下文对象指针
 * @return 添加是否成功
 * @details 使用写锁保护上下文库的并发访问，将上下文对象插入到映射中
 */
bool AsyncSocketStream::addCtx(Ctx::ptr ctx)
{
    RWMutexType::WriteLock lock(m_mutex);
    m_ctxs.insert(std::make_pair(ctx->sn, ctx));
    return true;
}

/**
 * @brief 将发送上下文加入发送队列
 * @param ctx 发送上下文对象指针
 * @return 队列是否从空变为非空
 * @details 使用写锁保护发送队列的并发访问，如果队列为空则通知写协程
 */
bool AsyncSocketStream::enqueue(SendCtx::ptr ctx)
{
    SYLAR_ASSERT(ctx);
    RWMutexType::WriteLock lock(m_queueMutex);
    bool empty = m_queue.empty();
    m_queue.push_back(ctx);
    lock.unlock();
    // 如果队列为空，则通知写协程有新数据需要发送
    if (empty) {
        m_sem.notify();
    }
    return empty;
}

/**
 * @brief 内部关闭方法
 * @return 关闭是否成功
 * @details 执行断开连接回调，关闭Socket，清理上下文和队列等资源
 * 通知所有等待的协程操作失败
 */
bool AsyncSocketStream::innerClose()
{
    SYLAR_ASSERT(m_iomanager == sylar::IOManager::GetThis());
    // 执行断开连接回调
    if (isConnected() && m_disconnectCb) {
        m_disconnectCb(shared_from_this());
    }
    // 执行关闭回调
    onClose();
    // 关闭Socket
    SocketStream::close();
    // 通知写协程
    m_sem.notify();
    // 清理上下文库
    std::unordered_map<uint32_t, Ctx::ptr> ctxs;
    {
        RWMutexType::WriteLock lock(m_mutex);
        ctxs.swap(m_ctxs);
    }
    // 清理发送队列
    {
        RWMutexType::WriteLock lock(m_queueMutex);
        m_queue.clear();
    }
    // 通知所有等待的协程操作失败
    for (auto &i : ctxs) {
        i.second->result = IO_ERROR;
        i.second->resultStr = "io_error";
        i.second->doRsp();
    }
    return true;
}

/**
 * @brief 等待协程结束
 * @return 操作是否成功
 * @details 等待读协程和写协程结束，通过等待两个信号量实现
 */
bool AsyncSocketStream::waitFiber()
{
    // 等待两个信号量，分别对应读协程和写协程
    m_waitSem.wait();
    m_waitSem.wait();
    return true;
}

/**
 * @brief 关闭Socket流
 * @details 取消自动重连，切换到IOManager线程，取消定时器，并关闭Socket
 */
void AsyncSocketStream::close()
{
    // 取消自动重连
    m_autoConnect = false;
    // 切换到IOManager线程
    SchedulerSwitcher ss(m_iomanager);
    // 取消定时器
    if (m_timer) {
        m_timer->cancel();
    }
    // 关闭Socket
    SocketStream::close();
}

/**
 * @brief 构造函数，初始化异步Socket流管理器
 * @details 初始化连接数量和索引计数器
 */
AsyncSocketStreamManager::AsyncSocketStreamManager() : m_size(0), m_idx(0)
{
}

/**
 * @brief 添加一个异步Socket流
 * @param stream AsyncSocketStream对象指针
 * @details 将流添加到管理列表中，并设置回调函数
 */
void AsyncSocketStreamManager::add(AsyncSocketStream::ptr stream)
{
    RWMutexType::WriteLock lock(m_mutex);
    m_datas.push_back(stream);
    ++m_size;

    // 设置连接回调
    if (m_connectCb) {
        stream->setConnectCb(m_connectCb);
    }

    // 设置断开连接回调
    if (m_disconnectCb) {
        stream->setDisconnectCb(m_disconnectCb);
    }
}

/**
 * @brief 清空所有连接
 * @details 关闭所有管理的Socket流并清空列表
 */
void AsyncSocketStreamManager::clear()
{
    RWMutexType::WriteLock lock(m_mutex);
    // 关闭所有连接
    for (auto &i : m_datas) {
        i->close();
    }
    m_datas.clear();
    m_size = 0;
}

/**
 * @brief 设置连接列表
 * @param streams AsyncSocketStream对象指针列表
 * @details 替换当前管理的连接列表，关闭旧连接，并为新连接设置回调函数
 */
void AsyncSocketStreamManager::setConnection(const std::vector<AsyncSocketStream::ptr> &streams)
{
    auto cs = streams;
    RWMutexType::WriteLock lock(m_mutex);
    // 交换连接列表
    cs.swap(m_datas);
    m_size = m_datas.size();
    // 为新连接设置回调函数
    if (m_connectCb || m_disconnectCb) {
        for (auto &i : m_datas) {
            if (m_connectCb) {
                i->setConnectCb(m_connectCb);
            }
            if (m_disconnectCb) {
                i->setDisconnectCb(m_disconnectCb);
            }
        }
    }
    lock.unlock();

    // 关闭旧连接
    for (auto &i : cs) {
        i->close();
    }
}

/**
 * @brief 获取一个可用的连接
 * @return AsyncSocketStream对象指针，如果没有可用连接则返回nullptr
 * @details 使用轮询方式选择一个已连接的Socket流，确保负载均衡
 */
AsyncSocketStream::ptr AsyncSocketStreamManager::get()
{
    RWMutexType::ReadLock lock(m_mutex);
    // 尝试轮询查找可用连接
    for (uint32_t i = 0; i < m_size; ++i) {
        // 使用原子操作增加索引，确保线程安全
        auto idx = sylar::Atomic::addFetch(m_idx, 1);
        // 取模操作实现轮询
        if (m_datas[idx % m_size]->isConnected()) {
            return m_datas[idx % m_size];
        }
    }
    return nullptr;
}

/**
 * @brief 设置连接回调函数
 * @param v 连接回调函数
 * @details 设置管理器的连接回调，并为所有已管理的连接设置相同的回调
 */
void AsyncSocketStreamManager::setConnectCb(connect_callback v)
{
    m_connectCb = v;
    RWMutexType::WriteLock lock(m_mutex);
    // 为所有已管理的连接设置连接回调
    for (auto &i : m_datas) {
        i->setConnectCb(m_connectCb);
    }
}

/**
 * @brief 设置断开连接回调函数
 * @param v 断开连接回调函数
 * @details 设置管理器的断开连接回调，并为所有已管理的连接设置相同的回调
 */
void AsyncSocketStreamManager::setDisconnectCb(disconnect_callback v)
{
    m_disconnectCb = v;
    RWMutexType::WriteLock lock(m_mutex);
    // 为所有已管理的连接设置断开连接回调
    for (auto &i : m_datas) {
        i->setDisconnectCb(m_disconnectCb);
    }
}

} // namespace sylar