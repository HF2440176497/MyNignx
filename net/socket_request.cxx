
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "macro.h"
#include "c_socket.h"
#include "func.h"

// 一次最多读 MESSAGE_SIZE - 1 个字节的数据
#define MESSAGE_SIZE 501

/**
 * @brief 监听到读事件的 rhandler，打印客户端发送的字符
 * 当发生错误时，回收 lp_effec_conn，在 epoll_process_events 中移除监听
 */
void CSocket::event_request_handler(lp_connection_t lp_effec_conn) {

    char message[MESSAGE_SIZE];
    memset(message, 0, MESSAGE_SIZE);
    log_error_core(NGX_LOG_STDERR, 0, "对端发送信息...");

    int rfd = lp_effec_conn->fd;  // 此处不必判断 -1 的情况
    
    while (1) {
        int n = recv(rfd, message, MESSAGE_SIZE-1, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 读取完成最后进入此分支
                log_error_core(NGX_LOG_STDERR, errno, "读取完毕 [%s] Read alreday down", "EAGAIN");
                return;  // handler 返回 
            } else {
                log_error_core(NGX_LOG_STDERR, 0 , "读取出错 [%s]", "recv < 0");
                close_accepted_connection(lp_effec_conn);
                return;  // handler 返回
            }
        } else if (n == 0) {
            log_error_core(NGX_LOG_STDERR, 0 , "对端已关闭连接 [%s]", "recv == 0");
            close_accepted_connection(lp_effec_conn);
            return;
        } else {
            log_error_core(NGX_LOG_STDERR, 0 , "收到的字节数为 [%d] 内容: [%s]", n, message);
            continue;  // continue while
        }
    }  
    return;
}