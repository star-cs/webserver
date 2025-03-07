#include "scheduler.h"
#include "macro.h"

namespace sylar
{
/**
 * 协程调度器~~~
 * 每次都要考虑多种情况：
 * 1. 当Main函数主线程参与调度
 * 2. 使用线程池里的进行调度
 */

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * 当前线程的调度器，同一个调度器下的所有线程共享同一个实例
 * 1. use_caller = true，主线程、子线程池都会指向同一个
 * 2. use_caller = false，纯线程池模式，主线程不赋值，子线程池都会指向同一个
 * 
 * 目的：
 * 1. 让每个子线程能访问 同一个 调度器，也可以添加任务
 * 2. 
 * 3. stop的时候，通过是否指向同一个，判断当前调用stop的 是不是 外部线程 （详细见 stop() 分析）
 */ 
static thread_local Scheduler* t_scheduler = nullptr;

/**
 * 当前线程的调度协程，每个线程都独一份
 * Fiber文件里：t_fiber当前运行的协程，t_thread_fiber主协程。
 * t_scheduler_fiber 调度协程。
 * t_thread_fiber <--> t_scheduler_fiber <--> t_fiber
 * 
 * 如果是Mian函数的主线程里，这个不是主协程，是子协程（充当调度协程）
 * 如果是线程池里的子线程里，这个是主协程
 */
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name){
    SYLAR_ASSERT(threads > 0);

    m_useCaller = use_caller;
    m_name = name;

    if(use_caller){ // 主线程也添加到 调度
        threads--;
        sylar::Fiber::GetThis();            // 创建主协程，初始化 t_thread_fiber
        SYLAR_ASSERT(GetThis() == nullptr)  // 当前线程没有调度器
        t_scheduler = this;                 // 设置当前线程的调度器

        /**
         * caller线程的主协程不会被线程的调度协程run进行调度，而且，线程的调度协程停止时，应该返回caller线程的主协程
         * 在user caller情况下，把caller线程的主协程暂时保存起来，等调度协程结束时，再resume caller协程
         * 
         * t_thread_fiber <--> t_scheduler_fiber <--> t_fiber(任务协程)
         */
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));  
        t_scheduler_fiber = m_rootFiber.get();   // 主线程里的 调度协程
        sylar::Thread::SetName(m_name);
        m_rootThread = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);    // 线程池，线程ID数组
    }else{
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler* Scheduler::GetThis(){
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber(){
    return t_scheduler_fiber;
}

void Scheduler::setThis(){
    t_scheduler = this;
}

Scheduler::~Scheduler(){
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::~Scheduler()";
    SYLAR_ASSERT(m_stopping);
    if(GetThis() == this){
        t_scheduler = nullptr;
    }
}

/**
 * 开始调度：
 * 创建线程池~，每个线程都会目标运行 Scheduler::run，并且传入了this隐式指针。使得每个子线程里的 t_scheduler 都能指向 主线程 创建 的 scheduler
 * 保存线程池里的 线程id
 */
void Scheduler::start() {
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::start()";
    MutexType::Lock lock(m_mutex);
    if(m_stopping){
        SYLAR_LOG_DEBUG(g_logger) << "Scheduler is stopped";
        return;
    }
    SYLAR_ASSERT(m_threads.empty());
    m_threads.resize(m_threadCount);
    for(size_t i = 0; i < m_threadCount; ++i){
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    // 停止，任务队列为空，线程池都结束了任务
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() { 
    SYLAR_LOG_DEBUG(g_logger) << "tickle"; 
}

void Scheduler::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "idle";
    while (!stopping()) {
        sylar::Fiber::GetThis()->yield();
    }
}

/**
 * 1. use_caller 模式下，必须是主线程，因为我们需要 切换到 主线程里的调度协程 消费一下任务
 * 2. 纯线程池 模型是，只要是外部线程即可stop。
 */
void Scheduler::stop() {
    SYLAR_LOG_DEBUG(g_logger) << "stop";
    if(stopping()){
        return;
    }
    m_stopping = true;

    /**
     * 在 use_caller 模式下，caller主线程和线程池里的都会指向同一个 t_scheduler 
     * 在 纯线程池 模型下，caller主线程 t_scheduler 不会被赋值。
     */ 
    if (m_useCaller) {
        SYLAR_ASSERT(GetThis() == this && sylar::GetThreadId() == m_rootThread);        //    确保当前是主线程，因为下面，我们需要 切换到主线程的调度协程
    } else { 
        SYLAR_ASSERT(GetThis() != this);        // 线程池里的都会指向 this，所以会排除
    }

    for (size_t i = 0; i < m_threadCount; i++) {
        tickle();
    }

    if (m_rootFiber) {
        tickle();
    }

    /**
     * 在use caller情况下，use_caller主协程里的调度协程其实，一直没有机会运行。
     * 因为use_caller主协程一直在main函数里顺序执行语句。
     * 所有在stop时，切换到调度协程，让调度协程最后参与一下run，消费任务。
     * 
     * m_rootFiber 保存的就是 use_caller 调度协程
     */ 
    if (m_rootFiber) {
        m_rootFiber->resume();
        SYLAR_LOG_DEBUG(g_logger) << "m_rootFiber end";
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for(auto &i : thrs){
        i->join();
    }
}

/**
 * run
 * 分多种情况：
 * 1. use_caller:
 *      主线程里的调度协程  （相当于把当前线程也参与了调度，处理任务队列）
 *      子线程
 * 2. 非use_caller:
 *      子线程
 * 
 * 也就是这个，主要有两种协程调用 run ：1. start里创建的子线程；2. use_caller主线程里的调度协程。
 * 
 * 
 * run里的协程，不特别说明，都默认是 m_runInScheduler=true，任务协程
 * 例如：idle_fiber, cb_fiber, task_fiber（任务队列里的），子线程里的调度协程 t_scheduler_fiber
 * 
 * task_fiber->resume():
 *      swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_mtx);        // 指定 与 t_scheduler_fiber 并交换
 * task_fiber->yield():
 *      swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx));
 * 
 * 
 * 如果不是任务协程，m_runInScheduler=false
 * 例如：1. use_caller主线程里的主协程、调度协程 t_scheduler_fiber
 */
void Scheduler::run() {
    SYLAR_LOG_DEBUG(g_logger) << "run";
    setThis();                                      /// 每个子线程都会把 传入的Scheduler，保证每个任务线程指向 同一个 Scheduler
    if(sylar::GetThreadId() != m_rootThread){       /// 意味着当前执行run 的是start()里创建的子线程
        t_scheduler_fiber = sylar::Fiber::GetThis().get();      // 此时的子线程 还没主协程，故，创建并赋值给 t_scheduler_fiber，作为当前线程的调度协程~ 
    }       // use_caller主线程，在调度器初始化时，已经把 主线程的调度协程，赋值给 t_scheduler_fiber
    

    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));    // 空转 协程
    Fiber::ptr cb_fiber;                                                    // 用于封装 cb 仿函数任务的协程

    ScheduleTask task;
    while(true){
        task.reset();
        bool tickle_me = false;     // 是否 tickle 其他线程进行任务调度
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            while(it != m_tasks.end()){
                // 如果当前的任务 不在 目标线程里
                if(it->thread != -1 && it->thread != sylar::GetThreadId()){
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，
                    // 然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                SYLAR_ASSERT(it->fiber || it->cb);

                if(it->fiber){
                    // 任务队列时的协程一定是 READY 状态
                    SYLAR_ASSERT(it->fiber->getState() == Fiber::READY);
                }
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }

            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if(tickle_me){
            tickle();           // 实际 tickle 不会通知，因为 只要 调度不停止，就会不断拿去 任务~ （这里是一个while(true){...}）
        }

        if(task.fiber){
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃线程数减一
            // 分情况：
            // 1. 子线程，主协程（调度协程） --> 任务协程
            // 2. use_caller线程，子协程（调度协程）--> 任务协程        再次强调，use_caller线程里的主协程并不操作任务
            task.fiber->resume();      
            --m_activeThreadCount;
            task.reset();
        }else if(task.cb){
            if(cb_fiber){
                cb_fiber->reset(task.cb);
            } else {
                cb_fiber.reset(new Fiber(task.cb));
            }

            task.reset();
            // 同上
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        }else{
            // 进到这个分支情况一定是任务队列空了，调度idle协程即可
            if(idle_fiber->getState() == Fiber::TERM){
                // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，不会结束，如果idle协程结束了，那一定是调度器停止了
                SYLAR_LOG_DEBUG(g_logger) << "idel fiber term";
                break;
            }
            ++m_idleThreadCount;
            // 同上
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
}




}