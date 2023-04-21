
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
void CSocket::event_accept_handler(lp_connection_t lp_standby_conn) {
    struct sockaddr conn_addr;
    socklen_t addrlen = sizeof(conn_addr);

    // ET 模式下，需要循环 accept 直到返回错误码（有几种错误类型不做处理）说明 accept 完成
    // listenfd 上多个连接到来
    int connfd;
    lp_connection_t lp_newconn;

    // 若待命连接的 fd 不是 listenfd，说明不是待命连接，直接退出
    if (lp_standby_conn->fd != m_listenfd) {
        log_error_core(LOG_ALERT, 0, "当前 connection_t 非待命连接，无法建立三次握手 at [%s]", "event_accept_handler");
        exit(-1);
    }

    while (1) {
        connfd = accept(m_listenfd, &conn_addr, &addrlen);
        if (connfd == -1) {
            if (errno == ECONNABORTED || errno == EPROTO) {  // 忽略这些错误
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 说明此次 epoll_wait 返回的 EPOLLIN 事件已经 accept 完毕，randler 已经完毕，可以 return 了
                // continue;
                return;
            }
            log_error_core(LOG_ALERT , errno, "Invoking accept has overed or failed at [%s]", "event_accept_connection");
            return;  // 其他错误，说明已经没有来握手的客户端了或者出错，直接返回
        }

        // 走到这里说明 connfd > 0 需要：设置非阻塞，调用 epoll_add_event
        log_error_core(LOG_ALERT, 0, "三次握手建立连接 connfd = [%d]", connfd);
        if (setnonblocking(connfd)) { exit(-1); }

        // 创建一个有效连接
        lp_newconn = get_connection_item();
        if (lp_newconn == nullptr) {
            log_error_core(LOG_ALERT, 0, "CSocket::get_connection_item has failed at [%s]", "CSocket::event_accept_connection");
            exit(-1);
        }
        m_connection_count++;
        m_free_connection_count--;

        lp_newconn->fd = connfd;
        // lp_newconn->rhandler = &CSocket::event_request_handler; 
        lp_newconn->rhandler = &CSocket::event_pkg_request_handler;
        lp_newconn->s_lplistening = m_lplistenitem;
        epoll_add_event(connfd, 1, 0, EPOLL_CTL_ADD, lp_newconn);
    }
    return;
}
