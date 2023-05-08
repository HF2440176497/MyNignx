
#ifndef C_THREADPOOL_H
#define C_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

class CThreadPool;

typedef void*(*lp_Func)(void*);  // 函数指针

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
    void InMsgRecv(char* msg);
    void Call();

private:
    typedef struct ThreadItem {
        pthread_t    _handle;
        CThreadPool* lp_pool;     // 线程对象可选择是否依附于线程池
        char*        msg;         // 线程处理的消息指针
        bool         running;     // 线程是否正在运行
        bool         ifshutdown;  // 线程是否要结束
        ThreadItem(CThreadPool* lp_this): lp_pool(lp_this) {
            msg = nullptr;
            running = false;
            ifshutdown = false;
        };
        ~ThreadItem() {};
    }ThreadItem;


    // ThreadFunc init_thread_item 更适合非静态函数
private:
    static void* ThreadFunc(void* lp_item);
    void  init_thread_item(lp_Func func,  ThreadItem* lp_item);
    char* get_msg_item();
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
    static std::vector<ThreadItem*> m_threadvec;
};

#endif