
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <unistd.h>    //STDERR_FILENO等
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/epoll.h> //epoll
#include <sys/socket.h>
#include <sys/ioctl.h> 
#include <arpa/inet.h>

#include "macro.h"
#include "c_socket.h"
#include "func.h"

/**
 * @brief 作为待命连接的 rhandler 完成三次握手
 * 调用时机：epoll_wait 监听到 EPOOLIN 时，需要作为 rhandler 完成三次握手
 * @param lp_standby_conn 待命连接指针
 * @details 循环 accept
*/
void CSocket::event_accept_handler(lp_connection_t p_conn) {
    struct sockaddr conn_addr;
    socklen_t addrlen = sizeof(conn_addr);

    int             connfd;
    lp_connection_t lp_newconn;
    static int      use_accept4 = 1;   // 我们先认为能够使用accept4()函数
    
    while (1) {  
        if (use_accept4) {
            connfd = accept4(p_conn->fd, &conn_addr, &addrlen, SOCK_NONBLOCK);
        } else {
            connfd = accept(p_conn->fd, &conn_addr, &addrlen);
        }   
        if (connfd == -1) {
            if (errno == EAGAIN) {  // 说明此次 epoll_wait 返回的 EPOLLIN 事件已经 accept 完毕，randler 已经完毕，可以 return 了
                return;
            }
            if (use_accept4 && errno == ENOSYS) {
                use_accept4 = 0;
                continue;
            }
            log_error_core(LOG_ALERT , errno, "event_accept_connection: accept failed");
            return;  // 其他错误，说明已经没有来握手的客户端了或者出错，直接返回
        }

        // 检测当前在线用户数
        if (m_online_count >= m_worker_connections) {
            log_error_core(LOG_ERR, 0, "超出系统允许的最大连入用户数[最大允许连入数%d]",m_worker_connections);  
            close(connfd);
            return;
        }
        // 保证连接池尺寸可控
        if (m_connectionList.size() > (m_worker_connections * 5)) {
            if (m_free_connectionList.size() < m_worker_connections) {
                close(connfd);
                return;
            }
        }
        // 走到这里说明 connfd > 0 需要：设置非阻塞，调用 epoll_add_event
        log_error_core(LOG_ALERT, 0, "三次握手建立连接 connfd = [%d]", connfd);
        lp_newconn = get_connection_item();
        if (lp_newconn == nullptr) {
            close(connfd);
            log_error_core(LOG_ALERT, 0, "CSocket::get_connection_item has failed at [%s]", "CSocket::event_accept_connection");
            exit(-1);
        }
        if(!use_accept4) {
            if (setnonblocking(connfd) == -1) { 
                close_connection(lp_newconn);
                exit(-1);
            }
        }
        lp_newconn->GetOneToUse();
        lp_newconn->fd = connfd;
        lp_newconn->p_listenitem = p_conn->p_listenitem;  // 每一个监听对象被一个待命连接和多个有效连接指向
        memcpy(&lp_newconn->s_sockaddr, &conn_addr, ADDR_LEN);
        
        lp_newconn->rhandler = &CSocket::event_readable_request_handler;
        lp_newconn->whandler = &CSocket::event_writable_request_handler;
        if (epoll_oper_event(connfd, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP, 0, lp_newconn) == -1) {
            close_connection(lp_newconn);
            return;
        }
        if (m_TimeEnable == 1) {
            AddToTimerQueue(lp_newconn);
        }
        ++m_online_count;
        break;  // 调用一次 accept 即可
    }  // end while(1)
    return;
}
