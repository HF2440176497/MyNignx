
#ifndef C_SOCKET_H
#define C_SOCKET_H

#include <sys/epoll.h> // epoll_event 等结构体
#include <sys/socket.h>
#include <list>
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
    void            epoll_add_event(int fd, int readevent, int writeevent, uint32_t event_type, lp_connection_t lp_curconn);
    int             epoll_process_events(int port_num, int port_value, int timer);
    ssize_t         recvproc(lp_connection_t lp_curconn, char* buf, ssize_t buflen);
    virtual int     ThreadRecvProc(char *msg); 

private:
    int             setnonblocking(int fd);
    void            connectpool_init();

    int             event_open_listen(int port_num, int port_value);
    void            event_close_listen();
    
    void            close_accepted_connection(lp_connection_t lp_curconn);

    void            event_accept_handler(lp_connection_t lp_standby_conn);
    void            event_request_handler(lp_connection_t lp_effec_conn);
    void            event_pkg_request_handler(lp_connection_t lp_effec_conn);  // 收包、解包实战
    
    int             pkg_header_recv(lp_connection_t lp_curcon);  // 包头收取函数
    int             pkg_body_recv(lp_connection_t lp_curconn);
    
    void            pkg_header_proc(lp_connection_t lp_curcon);  // 包头处理函数
    void            pkg_body_proc(lp_connection_t lp_curconn);

    

    lp_connection_t get_connection_item();
    void            init_connection_item(lp_connection_t);
    void            free_connection_item(lp_connection_t);  // 连接池回收一个连接



public:
    int                m_port_count;          // 端口数量
    int                m_total_connections;   // 每个线程的连接池的元素数量

private:
    int                m_listenfd;
    int                m_epfd;                // 每个线程对应的独立 epfd
    struct epoll_event m_events[MAX_EVENTS];  // epoll_wait 的返回参数

    // int                m_worker_process       // 可考虑将每个端口创建的进程数 worker_process_num 作为成员变量

    lp_connection_t    m_lpconnections;       // 连接池数组首地址
    lp_connection_t    m_free_lpconnections;  // 连接池空闲元素地址

    int                m_connection_count;    // 当前 worker process 中连接对象总数
    int                m_free_connection_count;
    lp_listening_t     m_lplistenitem;          // listening_t 结构体
};

#endif
