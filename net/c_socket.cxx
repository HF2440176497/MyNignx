

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "macro.h"
#include "global.h"
#include "c_conf.h"
#include "c_socket.h"

#include "func.h"


CSocket::CSocket() {
    // socket fd 相关
    m_listenfd = -1;
    m_epfd = -1;

    // 连接池相关
    m_port_count = 1;
    m_total_connections = 1;
    m_lpconnections = nullptr;
    m_free_lpconnections = nullptr;
    return;
}

// 先释放连接池的所有连接 free_connection_item
// 再回收 m_lpconnections 数组的所有内存
CSocket::~CSocket() {
    event_close_listen();
    if (m_lplistenitem)
        delete m_lplistenitem;
    if (m_lpconnections != nullptr)
        delete[] m_lpconnections;
    return;
}


/**
 * @brief 初始化连接池，每个进程都会有各自的成员 m_lpconnections m_free_lpconnections
 * 
 */
void CSocket::connectpool_init() {
    m_lpconnections = new connection_t[m_total_connections];
    m_free_lpconnections = m_lpconnections;
    m_connection_count = 0;
    m_free_connection_count = m_total_connections;

    int i = m_total_connections;  // 注意这里应当确保 m_total_connections 有值
    lp_connection_t lp_curnext = nullptr;
    size_t addr_len = sizeof(struct sockaddr);
    do {
        i--;
        init_connection_item(&m_lpconnections[i]);
        m_lpconnections[i].next = lp_curnext;
        lp_curnext = &m_lpconnections[i];
    } while (i);

    return;
}

/**
 * @brief 创建并初始化监听对象 init_worker_process 中调用
 */
void CSocket::event_init(int port_num, int port_value) {
    m_listenfd = event_open_listen(port_num, port_value);  // 需要传入端口相关信息
    if (m_listenfd == -1) {
        log_error_core(LOG_ALERT, errno, "event_init failed at port_num: [%d], port: [%d]", port_num, port_value);
        exit(-1);
    }
    m_lplistenitem = new listening_t;  // 析构函数中才会释放
    m_lplistenitem->fd = m_listenfd;
    m_lplistenitem->port = port_value;
    m_lplistenitem->s_lpconnection = nullptr;  // 后续 epoll_init 调用 get_connection 创建待命连接

    connectpool_init();
    return;
}


/**
 * @brief 此时连接池，监听对象结构体已经创建出来，我们需要（1）创建 epfd （2）创建 m_listenfd 对应的待命连接（3）加入监听
*/
void CSocket::epoll_init() {
    m_epfd = epoll_create(m_total_connections);
    if (m_epfd == -1) {
        log_error_core(LOG_ALERT, errno, "epoll_create failed");
        exit(-1);
    }

    // 创建一个待命连接
    lp_connection_t lp_new_connitem = get_connection_item();
    if (lp_new_connitem == nullptr) {
        log_error_core(LOG_ALERT, errno, "CSocket::get_connection_item has failed at [%s]", "CSocket::epoll_init");
        exit(-1);
    }
    m_connection_count++;
    m_free_connection_count--;

    // 待命连接是 m_lplistenitem->s_lpconnection 所指向的连接，fd 是 listenfd，sockaddr 不用赋值
    lp_new_connitem->fd = m_listenfd;  
    lp_new_connitem->rhandler = &CSocket::event_accept_handler;
    m_lplistenitem->s_lpconnection = lp_new_connitem;

    // listenfd 加入 epfd 树，用来后续唤醒，进行 accept
    // epoll_add_event 内不负责 get_connection, 传入的参数是提前创建好的
    epoll_add_event(m_listenfd, 1, 0, EPOLL_CTL_ADD, lp_new_connitem);
    return;
}


/**
 * @brief 参考《高性能编程》
 * @param sockfd 
 * @return int 
 */
int CSocket::setnonblocking(int sockfd) {
    int old_option = fcntl(sockfd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, new_option) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting NONBLOCK has failed at [%s]", "setnonblocking");
        return -1;
    }
    return 0;
}

/**
 * @brief event_init 中调用 
 * @param port_num 端口序号
 * @param port_value 端口值
 * @return 返回绑定的 socket 句柄，出错返回 -1
*/
int CSocket::event_open_listen(int port_num, int port_value) {

    int isock;
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;                
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    isock = socket(AF_INET, SOCK_STREAM, 0);
    if (isock == -1) {
        log_error_core(LOG_ALERT, errno, "Creating socket has failed");
        return -1;
    }
    int en_reuseaddr = 1;
    int en_reuseport = 1;

    // SO_REUSEADDR SO_REUSEPORT 是有所区别的，前者允许 TIME_WAIT 重用，后者允许多个绑定
    if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*)&en_reuseaddr, sizeof(int)) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting SO_REUSEADDR has failed at [%s]", "CSocket::event_open_listen");
        return -1;
    }
    if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT, (const void*)&en_reuseaddr, sizeof(int))== -1) {
        log_error_core(LOG_ALERT, errno, "Setting SO_REUSEPORT has failed at [%s]", "CSocket::event_open_listen");
        return -1;
    }
    if (setnonblocking(isock) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting NONBLOCKING has failed at [%s]", "CSocket::event_open_listen");
        return -1;
    }
    serv_addr.sin_port = htons((in_port_t)port_value);  // 转换为网络字节序

    if (bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
        log_error_core(LOG_ALERT, errno, "Binding has failed at [%s]", "CSocket::event_open_listen");
        return -1;
    }
    if (listen(isock, LISTEN_BACKLOG) == -1) {
        log_error_core(LOG_ALERT, errno, "Listening has failed at [%s]", "CSocket::event_open_listen");
        return -1;            
    }
    return isock;
}

/**
 * @brief 关闭监听对象 m_lplistenitem 不处理连接池
 * 此函数被 CSocket::~CSocket() 调用
 * 守护进程的父进程退出时，CSocket 析构，m_lplistenitem == nullptr 不用关闭套接字
 * 每个工作线程的对应有一个 m_lplistenitem 用来处理新连接
 */
void CSocket::event_close_listen() {
    if (m_lplistenitem != nullptr) {
        int listenfd = m_lplistenitem->fd;
        if (listenfd > 0) {
            log_error_core(LOG_INFO, 0, "Closing listen port at [%d]", m_lplistenitem->port);
            close(listenfd);
        } else {
            log_error_core(LOG_INFO, 0, "试图关闭监听 fd，但 listen socket fd 已无效");
        }     
    } 
    return;
}

/**
 * @brief 将 fd 以 ET 模式添加监听
 * @param fd 可以是 listenfd 也可以是 connfd
 * @param event_type 可以是 ADD MOD DEL，并不是事件类型
 * @param lp_curconn 作为 ptr，后续取到 connection_t 调用其 handler，lp_curconn 可以是待命连接 or 一般连接
 * invoked by epoll_init，event_accept
*/
void CSocket::epoll_add_event(int fd, int readevent, int writeevent, uint32_t event_type, lp_connection_t lp_curconn) {
    
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.ptr = lp_curconn;  // 注意：data 是 union 类型

    if (readevent == 1) {
        event.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLERR;  // EPOLLRDHUP 对端正常关闭，EPOLLERR 这是检测自己（服务端出错）
    } else if (writeevent == 1) {  

    } else {  // 其他监听事件类型

    }

    if (epoll_ctl(m_epfd, event_type, fd, &event) == -1) {
        log_error_core(LOG_ALERT, errno, "epoll_ctl has failed at [%s]", "epoll_add_event");
        exit(-1);
    }
    return;
}


/**
 * @brief 调用 epoll_wait
 * @param timer 阻塞时间 -1: 无限阻塞
 * @details 此函数在 worker_process_cycle 中调用，且一直循环，此函数不再循环调用 epoll_wait
 * @return int -1 说明非正常返回，在 worker_process_cycle 中退出；0 说明正常返回，在 worker_process_cycle 中再次循环
 */
int CSocket::epoll_process_events(int port_num, int port_value, int timer) {
    log_error_core(0, 0, "开始等待 epoll_wait 响应");
    int events_num = epoll_wait(m_epfd, m_events, MAX_EVENTS, timer);

    // 意外情况处理
    if (events_num == -1) {
        if (errno == EINTR) {
            log_error_core(LOG_INFO, errno, "epoll_wait has failed at [%s]", "CSocekt::ngx_epoll_process_events");
            return 0;  // 正常返回，再次 epoll_wait
        } else {
            log_error_core(LOG_ALERT, errno, "epoll_wait has failed at [%s]", "CSocekt::ngx_epoll_process_events");
            return -1;  // 非正常返回
        }
    }
    // 超时情况处理
    if (events_num == 0)  {
        if (timer != -1) 
            return 0;
        log_error_core(LOG_ALERT, 0, "epoll_wait wait indefinitely, but no event is returned at [%s]", "CSocekt::ngx_epoll_process_events");
        return -1;
    }
    log_error_core(0, 0, "epoll_wait 有响应事件，数量 events = [%d]", events_num);
    lp_connection_t lp_curconn;  // lp_curconn 有可能是待命连接，也可能是有效连接
    u_int32_t revents;  
    
    for (int i = 0; i < events_num; i++) {
        lp_curconn = (lp_connection_t)m_events[i].data.ptr;  // 此时 lp_curconn->handler 已赋值完成

        // 说明没有取到连接
        if (lp_curconn == nullptr) {  
            log_error_core(LOG_ALERT, errno, "事件编号 events [%d], 没有取到有效连接 at [%s]", i, "epoll_process_events");
            continue;
        }
        // 说明是已失效的连接对象
        // 若属于同一个连接对象的不同事件在 events 中，若前面的事件致使连接失效，close_accepted_connection 会使得 fd == -1
        if (lp_curconn->fd == -1) {
            log_error_core(LOG_ALERT, 0, "已失效的连接 lp_curconn->fd == -1");
            continue;
        }
        int curfd = lp_curconn->fd;
        revents = m_events[i].events;  
        if (revents & (EPOLLERR | EPOLLHUP)) {  // 出错，个人理解应当是关闭连接
            log_error_core(LOG_ALERT, 0, "EPOLLERR | EPOLLHUP");
            close_accepted_connection(lp_curconn);
            continue;
        }

        if (revents & EPOLLRDHUP) {  // 对方关闭连接
            log_error_core(0, 0, "对端已关闭连接 [%s]", "EPOLLRDHUP");
            close_accepted_connection(lp_curconn);
            continue;
        }

        if (revents & EPOLLIN) {
            log_error_core(LOG_ALERT, 0, "监听到可读事件 调用 rhandler");
            (this->*(lp_curconn->rhandler))(lp_curconn);
            // 说明读取出错，跳出了 handler
            // 若此时 event_request_handler 回收了此连接（说明调用了 close_accepted_connection）
            if (lp_curconn->fd == -1) {  
                epoll_ctl(m_epfd, EPOLL_CTL_DEL, curfd, NULL);  
                continue;
            }
            // 对于 ET 模式，handler 应当是循环结构，直到出现 error
        }

        if (revents & EPOLLOUT) {
            log_error_core(LOG_ALERT, 0, "监听到可写事件");
        }
        // 其他类型事件...
    }
    log_error_core(LOG_STDERR, 0, "离开 events 的 for 循环，epoll_process_events 结束");
    return 0;
}

/**
 * @brief （1）回收连接对象（2）关闭 connfd
 * @param lp_curconn 此时传入的 lp_curconn != nullptr 调用之后 lp_curconn 重新回到初始化状态
 */
void CSocket::close_accepted_connection(lp_connection_t lp_curconn) {
    log_error_core(0, 0, "调用了 close_accepted_connection, 关闭了 connfd = [%d]", lp_curconn->fd);
    int fd = lp_curconn->fd;
    free_connection_item(lp_curconn);
    if (close(fd) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd of current conn_t has failed at [%s]", "CSocket::close_accepted_connection");
    }
    return;
}

int CSocket::ThreadRecvProc(char *msg) {
    return 0;
}