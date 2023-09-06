
#include <thread>
#include <atomic>
#include <mutex>
#include <system_error>
#include <condition_variable>

#include "global.h"
#include "macro.h"
#include "func.h"

#include <pthread.h>
#include "c_threadpool.h"
#include "c_conf.h"
#include "c_lock.h"


std::list<std::shared_ptr<char>>  CThreadPool::m_msgqueue = {};
std::vector<CThreadPool::ThreadItem*> CThreadPool::m_threadvec = {}; 

CThreadPool::CThreadPool() {
    pthread_mutex_init(&m_pthreadMutex, NULL);
    pthread_cond_init(&m_pthreadCond, NULL);

    m_msgqueue_size  = 0;   // 收消息队列大小
    m_iCreateThread  = 10;  // 线程创建数缺省值
    m_iRunningThread = 0;
}


/**
 * @brief 叫停所有线程，清空消息队列，回收 item 内存
 * 此析构函数会自动调用 mutex condition_variable 的析构
 */
CThreadPool::~CThreadPool() {
    if (!StopAll()) {
        log_error_core(LOG_ALERT, 0, "线程关闭出错 可能已重复调用 at ~CThreadPool");
    }
    while (!m_msgqueue.empty()) {
        auto msg_release = m_msgqueue.front();
        m_msgqueue.pop_front();
        --m_msgqueue_size;
        msg_release = nullptr;
    }
    for (auto item:m_threadvec) {
        delete item;  
    }
    m_threadvec.clear();
}

/**
 * @brief 关闭所有线程，等待所有线程执行完，但未回收内存
 * 设置 m_shutdown ifshutdown 确保线程执行完毕，验证 running 是否退出成功
 * @return true 终止各个线程成功
 */
bool CThreadPool::StopAll() {
    if (m_shutdown == true) {  // 防止重复调用
        return false;
    }
    m_shutdown = true;
    for (auto item:m_threadvec) {
        item->ifshutdown = true;
    }
WaitShutdown:
    int errnum = pthread_cond_broadcast(&m_pthreadCond); 
    if (errnum != 0) {
        log_error_core(LOG_ALERT, 0, "cond_broadcast 失败 at CThreadPool::StopAll()");
        return false;
    }
    for (auto item:m_threadvec) {      
        if (item->running == true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            goto WaitShutdown;
        } else {
            pthread_join(item->_handle, NULL);  // 线程退出后 join 返回
        }
    }
    // 所有线程都退出后，才会销毁互斥量，条件变量
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);
    return true;
}


/**
 * @brief CThreadPool 类的线程对象创建函数
 * @param func 线程执行的回调函数
 * @param lp_item 回调函数参数
 */
void CThreadPool::init_thread_item(lp_Func func, ThreadItem* lp_item) {
    int errnum = pthread_create(&(lp_item->_handle), NULL, func, lp_item);  // 创建线程，错误不返回到errno，一般返回错误码
    if (errnum != 0) { // errnum 并不是 errno
        log_error_core(LOG_ALERT, 0, "CThreadPool::Create()创建线程失败，返回的错误码为 [%d]", errnum);
        exit(-1);
    }
    return;
}


void CThreadPool::ReadConf() {
    CConfig* p_config = CConfig::GetInstance();
    m_iCreateThread = p_config->GetInt("ThreadNum", m_iCreateThread);
    return;
}

/**
 * @brief 线程池的初始化工作，都位于子进程中
 */
void CThreadPool::Initialize_SubProc() {
    ReadConf();
    // 待补充...

    return;
}

/**
 * @brief 应当在接收连接之前就创建好线程池
 * @param threadnum 也可将成员作为参数
 * @return true 
 * @return false 
 */
void CThreadPool::Create() {
    for (int i = 0; i < m_iCreateThread; i++) {
        ThreadItem* lp_threaditem = new ThreadItem(this); 
        init_thread_item(&CThreadPool::ThreadFunc, lp_threaditem);  // _thread_fun 接收 thread_param 参数
        m_threadvec.push_back(lp_threaditem);
    }
WaitRunning:
    for (ThreadItem* lp_item:m_threadvec) {
        if (lp_item->running == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            goto WaitRunning;
        }
    }
    return;
}


/**
 * @brief 用于从消息队列获得一个消息
 * @return char* 消息指针
 * @todo queue 只是移除了了消息指针，对应内存还未释放，需要在处理函数完成后再释放
 */
std::shared_ptr<char> CThreadPool::get_msg_item() { 
    if (m_msgqueue.empty()) {
        return nullptr;
    }
    std::shared_ptr<char> tmp = m_msgqueue.front();
    m_msgqueue.pop_front();
    --m_msgqueue_size;
    return tmp;
}

/**
 * @brief 线程对象的消息处理函数
 */
// void CThreadPool::MsgFunc(ThreadItem* lp_thread) {
//     log_error_core(LOG_STDERR, 0, "正在处理消息。。。");
//     std::this_thread::sleep_for(std::chrono::milliseconds(4000));  // 模拟处理消息
//     // do something。。。
//     log_error_core(LOG_STDERR, 0, "处理消息完毕。。。");
//     return;
// }


/**
 * @brief 线程入口函数，running == true 真正表示启动成功
 * @param lp_thread 
 * 
 * 关于退出条件：
 * lock 之前添加判断是没有用的：lock() 处才会导致阻塞
 * cond_wait 之前会检查退出条件（while 判断）
 * cond_wait 解除阻塞加锁后，无论是否有消息，都会判断 ifshutdown
 * 从线程的角度考虑，各个线程退出，一定先使得获得锁才能判断退出条件
 */
void* CThreadPool::ThreadFunc(void* lp_item) {
    ThreadItem* lp_thread = static_cast<ThreadItem*>(lp_item);  
    CThreadPool* lp_this = lp_thread->lp_pool;
    pthread_t tid = pthread_self();  // 作用范围仅限于此函数
    // log_error_core(LOG_INFO, 0, "线程进入 ThreadFunc 函数");
    int errnum;

    while (true) {
        errnum = pthread_mutex_lock(&lp_this->m_pthreadMutex); 
        if (errnum != 0) {
            log_error_core(LOG_ALERT, errnum, "当前线程 [%d] 加锁失败 at ThreadFunc", tid);
        }
        while (lp_this->m_msgqueue.size() == 0 && lp_thread->ifshutdown == false) {      
            if (lp_thread->running == false) {  // 线程首次运行到此处，设置 running
                lp_thread->running = true;
            }
            // log_error_core(LOG_INFO, 0, "当前线程 [%d] 运行至 m_cond.wait，阻塞释放锁", tid);
            pthread_cond_wait(&lp_this->m_pthreadCond, &lp_this->m_pthreadMutex);  // 没有消息，则解锁互斥量，并阻塞在此等待唤醒，唤醒时会重新加锁，但只有一个加锁成功。加锁成功的会循环判断判断         
        }
        if (lp_thread->ifshutdown) {
            lp_thread->running = false;
            errnum = pthread_mutex_unlock(&lp_this->m_pthreadMutex); 
            if (errnum != 0) {
                log_error_core(LOG_INFO, 0, "当前线程 [%d] 收到关闭指令，但解锁失败 at ThreadFunc", tid);
            }
            break; 
        }
        // 说明是来消息引发的退出
        lp_thread->msg_ptr = lp_this->get_msg_item();  // 此时 msg_ptr 用来管理分配的内存，释放时置空
        if (lp_thread->msg_ptr == nullptr) {
            errnum = pthread_mutex_unlock(&lp_this->m_pthreadMutex); 
            if (errnum != 0) {
                log_error_core(LOG_INFO, 0, "当前线程 [%d] 未获取消息，但解锁失败 at ThreadFunc", tid);
                break;
            }
            continue;
        }
        errnum = pthread_mutex_unlock(&lp_this->m_pthreadMutex); 
        if (errnum != 0) {
            log_error_core(LOG_INFO, 0, "当前线程 [%d] 解锁失败 at ThreadFunc", tid);
            break;
        }
        // log_error_core(LOG_STDERR, 0, "线程拿到消息");
        // lp_thread 构造时注意指针置空
        lp_thread->msg = lp_thread->msg_ptr.get();  // 05.24 更新：这里不用变，因为取到的还是 shared_ptr
        lp_this->m_iRunningThread.fetch_add(1, std::memory_order_seq_cst);

        if (g_socket.ThreadRecvProc(lp_thread->msg) == -1) {
            log_error_core(LOG_INFO, 0, "ThreadRecvProc 错误");
        }   
        lp_thread->msg = nullptr;
        lp_thread->msg_ptr = nullptr;  // msg 使用完之后 msg_ptr 才能置空
        lp_this->m_iRunningThread.fetch_sub(1, std::memory_order_seq_cst);
    } // end while (1)
    log_error_core(LOG_ALERT, 0, "线程非人为因素退出");
    lp_thread->running = false;
    return nullptr;
}

/**
 * @brief 消息入队列，唤醒线程
 * 调用 Call 函数不必加锁
 */
void CThreadPool::InMsgRecv(std::shared_ptr<char> msg) {
    int errnum = pthread_mutex_lock(&m_pthreadMutex);
    if (errnum != 0) {
        log_error_core(LOG_INFO, 0, "线程池尝试收取消息，加锁失败 at InMsgRecv");
    }
    m_msgqueue.push_back(msg);  // 相当于是引用计数增加 
    ++m_msgqueue_size;
    errnum = pthread_mutex_unlock(&m_pthreadMutex);
    if (errnum != 0) {
        log_error_core(LOG_INFO, 0, "线程池收取消息，解锁失败 at InMsgRecv");
    }
    Call();
    return;
}

/**
 * @brief 试图唤醒一个线程
 */
void CThreadPool::Call() {
    int errnum = pthread_cond_signal(&m_pthreadCond);
    if (errnum != 0) {
        log_error_core(LOG_ALERT, 0, "cond_signal 失败");
    }
    if (m_iRunningThread >= m_iCreateThread && !m_msgqueue.empty()) {  // 例如：线程拿走了消息，还在处理消息中，但又来了消息触发此次 Call 
        // log_error_core(LOG_STDERR, 0, "消息积压，并且无空闲线程，考虑增加线程");
        // 交给 PrintInfo
    }
    return;
}