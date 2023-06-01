
#ifndef C_SOCKET_H
#define C_SOCKET_H

#include <sys/epoll.h>  // epoll_event 等结构体
#include <sys/socket.h>
#include <atomic>
#include <list>
#include <vector>
#include <queue>
#include <map>
#include <semaphore.h>
#include "comm.h"

#define LISTEN_BACKLOG 511
#define MAX_EVENTS 512

class CSocket {
public:
    CSocket();
    virtual ~CSocket();

public:
    void            event_init(int, int);
    void            epoll_init();
    void            ReadConf();
    void            Initialize();          // master 进程中 g_socket 的初始化工作
    void            Initialize_SubProc();  // worker 进程中 g_socket 的初始化工作
    void            Shutdown_SubProc();    // worker 进程中 g_socket 的回收工作

    void            connectpool_clean();   // 连接池清理
    void            sendmsglist_clean();   // 发送队列清理
    void            timermap_clean();

    int             epoll_oper_event(int fd, uint32_t flag, uint32_t event_type, int bcaction, lp_connection_t pconn);
    int             epoll_process_events(int port_num, int port_value, int timer);
    ssize_t         recvproc(lp_connection_t lp_curconn, char* buf, ssize_t buflen);
    virtual int     ThreadRecvProc(char* msg);                  // 子类重载函数，实现处理单个完整消息

    ssize_t         sendproc(lp_connection_t lp_conn, char* buf, ssize_t send_len);


private:
    int             setnonblocking(int fd);
    void            connectpool_init();

    void            event_open_listen(int port_num, int port_value);
    void            event_close_listen();

    void            close_accepted_connection(lp_connection_t lp_conn, bool istimeout_close); // 延迟回收连接
    void            close_connection(lp_connection_t lp_conn);          // 立即回收连接


    void            event_accept_handler(lp_connection_t lp_standby_conn);
    void            event_readable_request_handler(lp_connection_t lp_effec_conn);
    void            event_writable_request_handler(lp_connection_t lp_effc_conn);

    int             pkg_header_recv(lp_connection_t lp_curcon);  // 包头收取函数
    int             pkg_body_recv(lp_connection_t lp_curconn);

    void            pkg_header_proc(lp_connection_t lp_curcon);  // 包头处理函数
    void            pkg_body_proc(lp_connection_t lp_curconn);

    lp_connection_t get_connection_item();
    void            free_connection_item(lp_connection_t lp_conn_tofree);  // 连接池回收一个连接

    void            InRecyConnQueue(lp_connection_t lp_conn_torecy);       // 加入到延迟回收队列，在状态机 handler 中调用
    static void*    RecyConnThreadFunc(void* lp_item);                     // 延迟回收线程入口函数，静态函数

public:
    void                              MsgSendInQueue(std::shared_ptr<char> msg_tosend);
    static void*                      SendMsgThreadFunc(void* lp_item);           // 发送消息线程入口函数，静态函数
    void                              AddToTimerQueue(lp_connection_t lp_conn);   // 将新的有效连接
    static void*                      ServerTimerQueueThreadFunc(void* lp_item);  // 监视 timermap 的线程
    std::shared_ptr<STRUC_MSG_HEADER> GetOverTime(time_t cur_time);               // 返回 map 中开始的元素的包头指针，并更新此元素的时间重新插入
    void                              TimeCheckingProc(LPSTRUC_MSG_HEADER, time_t cur_time);
    void                              TimeOutProc(LPSTRUC_MSG_HEADER);
    void                              DeleteFromTimerQueue(LPSTRUC_MSG_HEADER);   // 根据传入的消息头
    std::shared_ptr<STRUC_MSG_HEADER> RemoveTimerFrist();

public:
    int             m_port_count;                // 端口数量
    int             m_create_connections_count;  // 每个线程的连接池的元素数量
    int             m_delay_time;                // 设定的延迟时间

private:
    typedef struct ThreadItem {
        pthread_t _handle;
        CSocket*  lp_socket;   // 指向
        bool      running;     // 线程是否正在运行
        bool      ifshutdown;  // 线程是否要结束
        ThreadItem(CSocket* lp_this) : lp_socket(lp_this) {
            running = false;
            ifshutdown = false;
        }
        ~ThreadItem() {}
    } ThreadItem;

private:
    int                               m_listenfd;
    int                               m_epfd;                   // 每个线程对应的独立 epfd
    struct epoll_event                m_events[MAX_EVENTS];     // epoll_wait 的返回参数
    lp_connection_t                   lp_standby_connitem;      // 待命连接，每个连接池只有一个

    std::queue<lp_connection_t>       m_connectionList;         // 全体连接
    std::queue<lp_connection_t>       m_free_connectionList;    // 空闲连接
    std::atomic_int                   m_connection_count;       // 当前 worker process 中连接对象总数
    std::atomic_int                   m_free_connection_count;  // 当前 worker process 中空闲连接对象数
    lp_listening_t                    m_lplistenitem;
    pthread_mutex_t                   m_socketmutex;            // 对于连接池操作的互斥量，同时保护空闲连接队列，总队列

    std::list<lp_connection_t>        m_recy_connectionList;    // 延迟待回收的连接
    std::vector<CSocket::ThreadItem*> m_threadVector;           // CSocket 额外的辅助线程
    pthread_mutex_t                   m_recymutex;              // 保护延迟回收队列的互斥量
    std::atomic_int                   m_recy_connection_count;  // 延迟待回收的连接个数

    std::list<std::shared_ptr<char>>   m_send_msgList;     // 待发送的消息列表
    pthread_mutex_t                    m_sendmutex;        // 保护 m_send_msgList 的互斥量
    std::atomic_int                    m_msgtosend_count;  // 待发送的消息数
    sem_t                              m_sendsem;          // sem_wait 用于发消息线程的运行函数

    int                                                      m_waittime;    // 检查 map 中某元素的时间间隔
    int                                                      m_TimeEnable;  // 表示是否开启心跳包机制 ReadConf 由配置决定，1 表示开启，并读取相关配置
    std::multimap<time_t, std::shared_ptr<STRUC_MSG_HEADER>> m_timermap;    // 用于保存所有连接的检查时间
    pthread_mutex_t                                          m_timer_mutex;
    time_t                                                   m_timer_value;  // 计时队列的队首时间值
    size_t                                                   m_timermap_size;
};

#endif
