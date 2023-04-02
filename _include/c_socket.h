
#ifndef CSOCKET_H
#define CSOCKET_H

#include <sys/epoll.h> // epoll_event 等结构体
#include <sys/socket.h>

#define LISTEN_BACKLOG 511
#define MAX_EVENTS 512

class CSocket;

typedef struct listening_s listening_t, *lp_listening_t;
typedef struct connection_s connection_t, *lp_connection_t;
typedef void (CSocket::*lp_event_handler)(lp_connection_t);  // 成员函数指针

struct listening_s {
    int fd;
    int port;
    lp_connection_t s_lpconnection;
};

// 连接对象
struct connection_s {
    int              fd;
    lp_event_handler rhandler;  // 读操作时的函数句柄
    lp_connection_t  next;

    struct sockaddr s_sockaddr;     // 这里用 sockaddr 类型
    lp_listening_t  s_lplistening;  // 始终指向被监听对象 m_lplistenitem
};
// 连接初始化 fd rhandler next s_sockaddr s_lplistening
// 对于待命连接：赋值 fd rhandler (epoll_init); m_lplistenitem->s_lpconnection 指向此待命连接
// 对于有效连接：fd rhandler s_sockaddr

class CSocket {
public:
    CSocket();
    virtual ~CSocket();

public:
    void event_init(int, int);
    void epoll_init();
    void epoll_add_event(int fd, int readevent, int writeevent, uint32_t event_type, lp_connection_t lp_curconn);
    int  epoll_process_events(int port_num, int port_value, int timer);

private:
    int setnonblocking(int fd);
    void connectpool_init();

    int  event_open_listen(int port_num, int port_value);
    void event_close_listen();
    void close_accepted_connection(lp_connection_t lp_curconn);

    void event_accept_handler(lp_connection_t lp_standby_conn);
    void event_request_handler(lp_connection_t lp_effec_conn);

    lp_connection_t get_connection_item();
    void            free_connection_item(lp_connection_t);  // 连接池回收一个连接

public:
    int                m_listenfd;
    int                m_epfd;                  // 每个线程对应的独立 epfd
    struct epoll_event m_events[MAX_EVENTS];  // epoll_wait 的返回参数

    int                m_port_count;          // 端口数量
    int                m_total_connections;   // 每个线程的连接池的元素数量

    // int                m_worker_process       // 可考虑将每个端口创建的进程数 worker_process_num 作为成员变量

    lp_connection_t    m_lpconnections;       // 连接池数组首地址
    lp_connection_t    m_free_lpconnections;  // 连接池空闲元素地址

    int                m_connection_count;    // 当前 worker process 中连接对象总数
    int                m_free_connection_count;
    lp_listening_t     m_lplistenitem;          // listening_t 结构体
};

#endif
