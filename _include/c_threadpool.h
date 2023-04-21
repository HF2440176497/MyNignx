
#ifndef C_THREADPOOL_H
#define C_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

class CThreadPool;

typedef void*(*LP_Func)(void*);  // 函数指针

typedef struct _ThreadItem {
    pthread_t    _handle;
    CThreadPool* lp_pool;
    char*        msg;         // 线程处理的消息指针
    bool         running;     // 线程是否正在运行
    bool         ifshutdown;  // 线程是否要结束
    _ThreadItem(CThreadPool* lp_this) : lp_pool(lp_this) {
        msg = nullptr;
        running = false;
        ifshutdown = false;
    }
    ~_ThreadItem() {}
} ThreadItem, *LPThreadItem;

class CThreadPool {
public:
    CThreadPool();
    ~CThreadPool();

public:
    void Create(int threadnum);
    bool StopAll();
    void InMsgRecv(char* msg);
    void Call();

private:
    // ThreadFunc init_thread_item 更适合非静态函数

    static void* ThreadFunc(void* lp_item);
    void  init_thread_item(LP_Func func,  LPThreadItem lp_item);
    char* get_msg_item();

    void MsgFunc(LPThreadItem lp_thread);  // 暂时没有用到
    void TimingMonitor();

public:
    int                     m_iCreateThread;  // 创建的线程数
    static std::list<char*> m_msgqueue;

private:
    // 线程池维护线程队列，有关线程队列的相关量都可以是静态的
    static bool             m_shutdown;        // 标记整个线程池是否要关闭 与类关联 因此是 static 的
    static pthread_mutex_t  m_pthreadMutex;    // 线程同步互斥量/也叫线程同步锁
    static pthread_cond_t   m_pthreadCond;     // 线程同步条件变量
    static std::atomic_uint m_iRunningThread;
    static std::vector<LPThreadItem> m_threadvec;
};

#endif