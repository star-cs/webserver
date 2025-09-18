#pragma once

#include <functional>
#include <memory>
#include "sylar/core/iomanager.h"
#include "sylar/core/mutex.h"
#include "socket_stream.h"
#include <boost/any.hpp>

namespace sylar
{

/**
 * @brief 异步Socket流类，提供异步读写功能的Socket流实现
 * @details 继承自SocketStream，并实现了异步IO操作，通过IOManager来管理事件
 * 使用协程来处理异步读写，支持自动重连和回调通知
 */
class AsyncSocketStream : public SocketStream,
                          public std::enable_shared_from_this<AsyncSocketStream>
{
public:
    typedef std::shared_ptr<AsyncSocketStream> ptr;
    typedef sylar::RWMutex RWMutexType;
    typedef std::function<bool(AsyncSocketStream::ptr)> connect_callback;
    typedef std::function<void(AsyncSocketStream::ptr)> disconnect_callback;

    /**
     * @brief 构造函数
     * @param sock Socket对象指针
     * @param owner 是否拥有该Socket的所有权
     */
    AsyncSocketStream(Socket::ptr sock, bool owner = true);

    /**
     * @brief 启动异步读写
     * @return 启动是否成功
     * @details 初始化IOManager，尝试连接Socket，并启动读写协程
     */
    virtual bool start();

    /**
     * @brief 关闭Socket流
     * @details 重写父类方法，取消自动重连，取消定时器，并关闭Socket
     */
    virtual void close() override;

public:
    /**
     * @brief 错误码枚举
     * @details 定义异步Socket操作中可能出现的错误类型
     */
    enum Error {
        OK = 0,             // 操作成功
        TIMEOUT = -1,       // 操作超时
        IO_ERROR = -2,      // IO错误
        NOT_CONNECT = -3,   // 未连接
    };

protected:
    /**
     * @brief 发送上下文基类
     * @details 所有发送上下文的抽象基类，定义了发送操作的接口
     */
    struct SendCtx {
    public:
        typedef std::shared_ptr<SendCtx> ptr;
        virtual ~SendCtx() {}

        /**
         * @brief 执行发送操作
         * @param stream 异步Socket流对象指针
         * @return 发送是否成功
         */
        virtual bool doSend(AsyncSocketStream::ptr stream) = 0;
    };

    /**
     * @brief 上下文类，继承自SendCtx，用于管理请求-响应模式的上下文
     * @details 包含请求序列号、超时设置、结果信息、调度器、协程和定时器等
     */
    struct Ctx : public SendCtx {
    public:
        typedef std::shared_ptr<Ctx> ptr;
        virtual ~Ctx() {}
        
        /**
         * @brief 构造函数，初始化成员变量
         */
        Ctx();

        uint32_t sn;            // 请求序列号
        uint32_t timeout;       // 超时时间（毫秒）
        uint32_t result;        // 操作结果
        bool timed;             // 是否超时

        Scheduler *scheduler;   // 调度器，用于调度协程
        Fiber::ptr fiber;       // 等待结果的协程
        Timer::ptr timer;       // 超时定时器

        std::string resultStr;  // 结果描述字符串

        /**
         * @brief 处理响应
         * @details 取消定时器，并将协程重新调度到调度器中继续执行
         */
        virtual void doRsp();
    };

public:
    /**
     * @brief 设置工作线程调度器
     * @param v IOManager指针
     */
    void setWorker(sylar::IOManager *v) { m_worker = v; }
    
    /**
     * @brief 获取工作线程调度器
     * @return IOManager指针
     */
    sylar::IOManager *getWorker() const { return m_worker; }

    /**
     * @brief 设置IO线程调度器
     * @param v IOManager指针
     */
    void setIOManager(sylar::IOManager *v) { m_iomanager = v; }
    
    /**
     * @brief 获取IO线程调度器
     * @return IOManager指针
     */
    sylar::IOManager *getIOManager() const { return m_iomanager; }

    /**
     * @brief 获取自动重连状态
     * @return 是否开启自动重连
     */
    bool isAutoConnect() const { return m_autoConnect; }
    
    /**
     * @brief 设置自动重连状态
     * @param v 是否开启自动重连
     */
    void setAutoConnect(bool v) { m_autoConnect = v; }

    /**
     * @brief 获取连接回调函数
     * @return 连接回调函数
     */
    connect_callback getConnectCb() const { return m_connectCb; }
    
    /**
     * @brief 获取断开连接回调函数
     * @return 断开连接回调函数
     */
    disconnect_callback getDisconnectCb() const { return m_disconnectCb; }
    
    /**
     * @brief 设置连接回调函数
     * @param v 连接回调函数
     */
    void setConnectCb(connect_callback v) { m_connectCb = v; }
    
    /**
     * @brief 设置断开连接回调函数
     * @param v 断开连接回调函数
     */
    void setDisconnectCb(disconnect_callback v) { m_disconnectCb = v; }

    /**
     * @brief 设置自定义数据
     * @tparam T 数据类型
     * @param v 数据值
     */
    template <class T>
    void setData(const T &v)
    {
        m_data = v;
    }

    /**
     * @brief 获取自定义数据
     * @tparam T 数据类型
     * @return 数据值，如果类型不匹配则返回默认值
     */
    template <class T>
    T getData() const
    {
        try {
            return boost::any_cast<T>(m_data);
        } catch (...) {
        }
        return T();
    }

protected:
    /**
     * @brief 执行读操作
     * @details 循环读取数据，直到连接断开或发生错误
     * 每次读取后调用doRecv处理数据并回复
     */
    virtual void doRead();
    
    /**
     * @brief 执行写操作
     * @details 从队列中取出发送上下文并执行发送操作
     */
    virtual void doWrite();
    
    /**
     * @brief 启动读协程
     * @details 在IOManager中调度doRead方法
     */
    virtual void startRead();
    
    /**
     * @brief 启动写协程
     * @details 在IOManager中调度doWrite方法
     */
    virtual void startWrite();
    
    /**
     * @brief 处理超时事件
     * @param ctx 上下文对象指针
     * @details 从上下文库中删除超时的上下文，并设置超时状态
     */
    virtual void onTimeOut(Ctx::ptr ctx);
    
    /**
     * @brief 接收数据处理（纯虚函数）
     * @return 上下文对象指针
     * @details 子类必须实现此方法，用于处理接收到的数据
     */
    virtual Ctx::ptr doRecv() = 0;
    
    /**
     * @brief 关闭回调（虚函数）
     * @details 子类可以重写此方法，在关闭时执行额外操作
     */
    virtual void onClose() {}

    /**
     * @brief 根据序列号获取上下文
     * @param sn 序列号
     * @return 上下文对象指针，如果不存在则返回nullptr
     */
    Ctx::ptr getCtx(uint32_t sn);
    
    /**
     * @brief 根据序列号获取并删除上下文
     * @param sn 序列号
     * @return 上下文对象指针，如果不存在则返回nullptr
     */
    Ctx::ptr getAndDelCtx(uint32_t sn);

    /**
     * @brief 根据序列号获取指定类型的上下文
     * @tparam T 上下文类型
     * @param sn 序列号
     * @return 指定类型的上下文对象指针，如果不存在或类型不匹配则返回nullptr
     */
    template <class T>
    std::shared_ptr<T> getCtxAs(uint32_t sn)
    {
        auto ctx = getCtx(sn);
        if (ctx) {
            return std::dynamic_pointer_cast<T>(ctx);
        }
        return nullptr;
    }

    /**
     * @brief 根据序列号获取并删除指定类型的上下文
     * @tparam T 上下文类型
     * @param sn 序列号
     * @return 指定类型的上下文对象指针，如果不存在或类型不匹配则返回nullptr
     */
    template <class T>
    std::shared_ptr<T> getAndDelCtxAs(uint32_t sn)
    {
        auto ctx = getAndDelCtx(sn);
        if (ctx) {
            return std::dynamic_pointer_cast<T>(ctx);
        }
        return nullptr;
    }

    /**
     * @brief 添加上下文到上下文库
     * @param ctx 上下文对象指针
     * @return 添加是否成功
     */
    bool addCtx(Ctx::ptr ctx);
    
    /**
     * @brief 将发送上下文加入发送队列
     * @param ctx 发送上下文对象指针
     * @return 队列是否从空变为非空
     */
    bool enqueue(SendCtx::ptr ctx);

    /**
     * @brief 内部关闭方法
     * @return 关闭是否成功
     * @details 处理断开连接回调，关闭Socket，清理上下文和队列等资源
     */
    bool innerClose();
    
    /**
     * @brief 等待协程结束
     * @return 操作是否成功
     * @details 等待读协程和写协程结束
     */
    bool waitFiber();

protected:
    sylar::FiberSemaphore m_sem;          // 信号量，用于控制写操作
    sylar::FiberSemaphore m_waitSem;      // 信号量，用于等待协程结束
    RWMutexType m_queueMutex;             // 保护发送队列的读写锁
    std::list<SendCtx::ptr> m_queue;      // 发送队列
    RWMutexType m_mutex;                  // 保护上下文库的读写锁
    std::unordered_map<uint32_t, Ctx::ptr> m_ctxs; // 上下文库，存储请求-响应上下文

    uint32_t m_sn;                        // 序列号计数器
    bool m_autoConnect;                   // 是否自动重连
    uint16_t m_tryConnectCount;           // 尝试连接次数
    sylar::Timer::ptr m_timer;            // 定时器，用于自动重连
    sylar::IOManager *m_iomanager;        // IO线程调度器
    sylar::IOManager *m_worker;           // 工作线程调度器

    connect_callback m_connectCb;         // 连接回调函数
    disconnect_callback m_disconnectCb;   // 断开连接回调函数

    boost::any m_data;                    // 自定义数据，可以存储任意类型的数据

public:
    bool recving = false;                 // 是否正在接收数据
};

/**
 * @brief 异步Socket流管理器类
 * @details 管理多个AsyncSocketStream对象，提供负载均衡和连接管理功能
 */
class AsyncSocketStreamManager
{
public:
    typedef sylar::RWMutex RWMutexType;
    typedef AsyncSocketStream::connect_callback connect_callback;
    typedef AsyncSocketStream::disconnect_callback disconnect_callback;

    /**
     * @brief 构造函数，初始化成员变量
     */
    AsyncSocketStreamManager();
    
    /**
     * @brief 虚析构函数
     */
    virtual ~AsyncSocketStreamManager() {}

    /**
     * @brief 添加一个异步Socket流
     * @param stream AsyncSocketStream对象指针
     */
    void add(AsyncSocketStream::ptr stream);
    
    /**
     * @brief 清空所有连接
     * @details 关闭所有管理的Socket流并清空列表
     */
    void clear();
    
    /**
     * @brief 设置连接列表
     * @param streams AsyncSocketStream对象指针列表
     * @details 替换当前管理的连接列表，关闭旧连接
     */
    void setConnection(const std::vector<AsyncSocketStream::ptr> &streams);
    
    /**
     * @brief 获取一个可用的连接
     * @return AsyncSocketStream对象指针，如果没有可用连接则返回nullptr
     * @details 使用轮询方式选择一个已连接的Socket流
     */
    AsyncSocketStream::ptr get();
    
    /**
     * @brief 获取指定类型的可用连接
     * @tparam T 连接类型
     * @return 指定类型的连接对象指针，如果没有可用连接或类型不匹配则返回nullptr
     */
    template <class T>
    std::shared_ptr<T> getAs()
    {
        auto rt = get();
        if (rt) {
            return std::dynamic_pointer_cast<T>(rt);
        }
        return nullptr;
    }

    /**
     * @brief 获取连接回调函数
     * @return 连接回调函数
     */
    connect_callback getConnectCb() const { return m_connectCb; }
    
    /**
     * @brief 获取断开连接回调函数
     * @return 断开连接回调函数
     */
    disconnect_callback getDisconnectCb() const { return m_disconnectCb; }
    
    /**
     * @brief 设置连接回调函数
     * @param v 连接回调函数
     * @details 设置所有管理的连接的连接回调函数
     */
    void setConnectCb(connect_callback v);
    
    /**
     * @brief 设置断开连接回调函数
     * @param v 断开连接回调函数
     * @details 设置所有管理的连接的断开连接回调函数
     */
    void setDisconnectCb(disconnect_callback v);

private:
    RWMutexType m_mutex;                  // 保护连接列表的读写锁
    uint32_t m_size;                      // 连接数量
    uint32_t m_idx;                       // 当前索引，用于轮询选择连接
    std::vector<AsyncSocketStream::ptr> m_datas; // 连接列表
    connect_callback m_connectCb;         // 连接回调函数
    disconnect_callback m_disconnectCb;   // 断开连接回调函数
};
} // namespace sylar