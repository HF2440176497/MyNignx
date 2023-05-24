
#ifndef C_THREADPOOL_H
#define C_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

class CThreadPool;

typedef void*(*lp_Func)(void*);

// ThreadItem 定义在 ThreadPool 类内
// private 类外是不可见的，但是类内可见


class CThreadPool {
public:
    CThreadPool();
    ~CThreadPool();

public:
    void ReadConf();
    void Initialize_SubProc();
    void Create();
    bool StopAll();
    void InMsgRecv(std::shared_ptr<char> msg);
    void Call();

private:
    typedef struct ThreadItem {
        pthread_t               _handle;
        CThreadPool*            lp_pool;     // 线程对象可选择是否依附于线程池
        std::shared_ptr<char>   msg_ptr;     // 从 List 中取得的指针
        char*                   msg;         // 线程处理的消息指针 由 msg_ptr 获得
        bool                    running;     // 线程是否正在运行
        bool                    ifshutdown;  // 线程是否要结束
        ThreadItem(CThreadPool* lp_this): lp_pool(lp_this) {
            msg_ptr    = nullptr;
            msg        = nullptr;
            running    = false;
            ifshutdown = false;
        };
        ~ThreadItem() {};
    }ThreadItem;

private:
    static void*            ThreadFunc(void* lp_item);
    void                    init_thread_item(lp_Func func, ThreadItem* lp_item);
    std::shared_ptr<char>   get_msg_item();
    void                    TimingMonitor();

public:
    int                                       m_iCreateThread;  // 创建的线程数
    static std::list<std::shared_ptr<char>>   m_msgqueue;       // 收取消息对列

private:
    // 线程池维护线程队列，有关线程队列的相关量都可以是静态的
    bool                            m_shutdown;        // 标记整个线程池是否要关闭 与类关联 因此是 static 的
    pthread_mutex_t                 m_pthreadMutex;    // 线程同步互斥量/也叫线程同步锁
    pthread_cond_t                  m_pthreadCond;     // 线程同步条件变量
    static std::atomic_int          m_iRunningThread;  // 线程运行数
    static std::vector<ThreadItem*> m_threadvec;
};

#endif