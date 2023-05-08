
#ifndef C_SOCKET_H
#define C_SOCKET_H

#include <sys/epoll.h>  // epoll_event 等结构体
#include <sys/socket.h>
#include <atomic>
#include <list>
#include <vector>
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

    int             epoll_oper_event(int fd, uint32_t flag, uint32_t event_type, int bcaction, lp_connection_t pconn);
    int             epoll_process_events(int port_num, int port_value, int timer);
    ssize_t         recvproc(lp_connection_t lp_curconn, char* buf, ssize_t buflen);
    virtual int     ThreadRecvProc(char* msg);
    // bool            JudgeOutdate(lp_connection_t );

private:
    int             setnonblocking(int fd);
    void            connectpool_init();

    void            event_open_listen(int port_num, int port_value);
    void            event_close_listen();

    void            close_accepted_connection(lp_connection_t lp_curconn);

    void            event_accept_handler(lp_connection_t lp_standby_conn);
    void            event_pkg_request_handler(lp_connection_t lp_effec_conn);

    int             pkg_header_recv(lp_connection_t lp_curcon);  // 包头收取函数
    int             pkg_body_recv(lp_connection_t lp_curconn);

    void            pkg_header_proc(lp_connection_t lp_curcon);  // 包头处理函数
    void            pkg_body_proc(lp_connection_t lp_curconn);

    lp_connection_t get_connection_item();
    void            free_connection_item(lp_connection_t lp_conn_tofree);  // 连接池回收一个连接

    void            InRecyConnQueue(lp_connection_t lp_conn_torecy);       // 加入到延迟回收队列，在状态机 handler 中调用
    static void*    RecyConnThreadFunc(void* lp_item);                     // 线程入口函数，静态函数

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

    std::list<lp_connection_t>        m_connectionList;         // 全体连接
    std::list<lp_connection_t>        m_free_connectionList;    // 空闲连接
    std::atomic_uint                  m_connection_count;       // 当前 worker process 中连接对象总数
    std::atomic_uint                  m_free_connection_count;  // 当前 worker process 中空闲连接对象数
    lp_listening_t                    m_lplistenitem;
    pthread_mutex_t                   m_socketmutex;            // 对于连接池操作的互斥量

    std::list<lp_connection_t> m_recy_connectionList;    // 延迟待回收的连接
    std::vector<CSocket::ThreadItem*> m_threadVector;           // CSocket 额外的辅助线程
    pthread_mutex_t                   m_recymutex;              // 保护延迟回收队列的互斥量
};

#endif
