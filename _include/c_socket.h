
#ifndef CSOCKET_H
#define CSOCKET_H

#include <sys/epoll.h> // epoll_event 等结构体
#include <sys/socket.h>
#include <list>
#include "comm.h"

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
    uint64_t         s_cursequence;  // 序号，标记取用次数，可以经过几次取用，get_item 中 ++

    lp_event_handler rhandler;       // 读操作时的函数句柄
    lp_connection_t  next;

    u_char              s_curstat;                       // 表示收包状态
    LPCOMM_PKG_HEADER   s_headerinfo;                    // 指向包头结构体，初始化时应当 nullptr
    char*            s_msgmem;                        // 指向为整个消息开辟的内存，待传入消息队列
    char*            s_precvbuf;                      // 接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
    unsigned int     s_recvlen;                       // 要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码

    // 以下成员来自自己提出的包缓冲机制

    
    struct sockaddr  s_sockaddr;                      // 这里用 sockaddr 类型
    lp_listening_t   s_lplistening;                   // 始终指向被监听对象 m_lplistenitem
};

typedef struct _STRUC_MSG_HEADER {
    lp_connection_t lp_curconn;  // 指向发包的连接对象
    uint64_t msg_cursequence;    // 记录收到包时 lp_curcon 后续连接可能会被回收，用于检验 lp_curconn
}STRUC_MSG_HEADER, *LPSTRUC_MSG_HEADER;


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

    void            InMsgQueue(lp_connection_t lp_curconn);      //

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

    // 消息队列缓存
    std::list<char *>              m_msgrecvqueue; 
};

#endif
