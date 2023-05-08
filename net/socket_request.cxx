
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   
#include <unistd.h>  
#include <fcntl.h> 
#include <errno.h>    
#include <sys/ioctl.h> 
#include <arpa/inet.h>

#include "macro.h"
#include "global.h"
#include "func.h"
#include "c_memory.h"
#include "c_socketlogic.h"

CMemory* CMemory::m_instance = nullptr;  // 定义并初始化

// 一次最多读 MESSAGE_SIZE - 1 个字节的数据
#define MESSAGE_SIZE 501

/**
 * @brief 暂时未用到
 */
// void CSocket::event_request_handler(lp_connection_t lp_effec_conn) {
//     char message[MESSAGE_SIZE];
//     memset(message, 0, MESSAGE_SIZE);
//     log_error_core(LOG_STDERR, 0, "对端发送信息...");

//     int rfd = lp_effec_conn->fd;  // 此处不必判断 -1 的情况
    
//     while (1) {
//         int n = recv(rfd, message, MESSAGE_SIZE-1, 0);
//         if (n < 0) {
//             if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 读取完成最后进入此分支
//                 log_error_core(LOG_STDERR, errno, "读取完毕 [%s] Read alreday down", "EAGAIN");
//                 return;  // handler 返回 
//             } else {
//                 log_error_core(LOG_STDERR, 0 , "读取出错 [%s]", "recv < 0");
//                 close_accepted_connection(lp_effec_conn);
//                 return;  // handler 返回
//             }
//         } else if (n == 0) {
//             log_error_core(LOG_STDERR, 0 , "对端已关闭连接 [%s]", "recv == 0");
//             close_accepted_connection(lp_effec_conn);
//             return;
//         } else {
//             log_error_core(LOG_STDERR, 0 , "收到的字节数为 [%d] 内容: [%s]", n, message);
//             continue;  // continue while
//         }
//     }  
//     return;
// }

/**
 * @brief 此函数用于读取指定长度的数据，或者读完缓冲区，例如收取一个包的包头
 * @param lp_curconn 
 * @param buf 
 * @param buflen 要求接收的数据的长度 不要传入 0 以免发生意外情况
 * @return 正常情况下返回 buflen，-1 说明读取错误，< buflen 说明读取完毕，但不符合要求的长度
 * ssize_t 与 size_t 的区别：ssize_t 有符号整型，32 位 or 64 位；size_t 同理是无符号整型
 * 实际传入的 buf，应当有 buflen + 1 空间，结尾是 0
 * 
 * 新增说明：正常情况的返回值 最小 == 0
 * @details 封装了连接出错，关闭连接的操作
 */
ssize_t CSocket::recvproc(lp_connection_t lp_curconn, char* buf, ssize_t buflen) {
    char* lp_curbuf = buf;
    ssize_t total_len = 0;
    while (true) {
        ssize_t n = recv(lp_curconn->fd, lp_curbuf, buflen-total_len, 0);  // buflen-total 是 buf 剩余的空间
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // log_error_core(LOG_ALERT, errno, "此轮已读取完缓冲区, 实际读取长度 = [%d], 要求读取长度 = [%d]", total_len, buflen);
                return total_len;
            } else if (errno == EINTR) {  // 需要忽略的错误需要放在 else if                   
                // ...
                continue; 
            } else {
                log_error_core(LOG_ALERT, errno , "读取出错, 连接: [%d] fd: [%d] 读取出错", lp_curconn, lp_curconn->fd);
                close_accepted_connection(lp_curconn);
                return -1;
            }
        } else if (n == 0) {
            log_error_core(LOG_STDERR, 0 , "对端已关闭, 连接: [%d] fd: [%d]", lp_curconn, lp_curconn->fd);
            close_accepted_connection(lp_curconn);
            return -1;
        } else {  // 读取到数据
            if (n == buflen-total_len) {
                // log_error_core(LOG_STDERR, 0 , "已读取完指定长度数据 buflen = [%d]", buflen);
                return buflen;
            } else {  // n < buflen-total_len 需要继续读取
                lp_curbuf += n;  // 指针移动到新的位置
                total_len += n;  // 记录此轮读取到的总长度
                continue;
            }
        }
    }
    close_accepted_connection(lp_curconn);
    return -1;  // 若意外情况退出 while 循环，返回 -1 表示异常
}


/**
 * @brief 有限状态机：根据处理结果更新状态
 * 当连接关闭时，连接对象的释放交给 recvproc 函数调用 close_accepted_connection 来处理
 * @param lp_effec_conn 
 * @todo 
 */
void CSocket::event_pkg_request_handler(lp_connection_t lp_conn) {
    log_error_core(LOG_INFO, 0, "进入状态机, 连接: [%d] fd: [%d]", lp_conn, lp_conn->fd);
    unsigned int suppose_size;
    while (true) {
        if (lp_conn->s_curstat == _PKG_HD_INIT) {
            char* header_mem = new char[PKG_HEADER_LEN](); 
            lp_conn->s_headerinfo = (LPCOMM_PKG_HEADER)header_mem;

            lp_conn->s_precvbuf = header_mem;
            lp_conn->s_recvlen = PKG_HEADER_LEN;

            suppose_size = lp_conn->s_recvlen;
            int recv_size = pkg_header_recv(lp_conn);
            // log_error_core(LOG_STDERR, 0, "收到包头长度 [%d]", recv_size);
            if (recv_size == -1) { 
                return;
            }
            if (recv_size == 0) {
                log_error_core(LOG_INFO, 0, "_PKG_HD_INIT 状态机读取完缓冲区");
                return;
            }
            if (recv_size < suppose_size)  {  // s_recvlen 此时已更新
                lp_conn->s_curstat = _PKG_HD_RECVING;
                continue; 
            }
            // 此时收到完整的包头 进行处理
            pkg_header_proc(lp_conn);
        }

        if (lp_conn->s_curstat == _PKG_HD_RECVING) {  
            suppose_size = lp_conn->s_recvlen;
            int recv_size = pkg_header_recv(lp_conn);  // 继续读取包头
            // log_error_core(LOG_STDERR, 0, "收到包头长度 [%d]", recv_size);
            if (recv_size == -1) {
                return;
            }
            if (recv_size == 0) {
                log_error_core(LOG_INFO, 0, "_PKG_HD_RECVING 状态机读取完缓冲区");
                return;
            }
            if (recv_size < suppose_size) {  
                lp_conn->s_curstat = _PKG_HD_RECVING;
                continue; 
            }
            pkg_header_proc(lp_conn);
        }

        // 开始收取包体 说明此时已运行完包头处理函数
        // 此时 suppose_size 在 header_proc 设置为包体长度
        if (lp_conn->s_curstat == _PKG_BD_INIT) {  
            suppose_size = lp_conn->s_recvlen;
            int recv_size = pkg_body_recv(lp_conn);
            // log_error_core(LOG_STDERR, 0, "收到包体长度 [%d]", recv_size);
            if (recv_size == -1) {
                return; 
            }
            if (recv_size == 0) {
                log_error_core(LOG_INFO, 0, "_PKG_BD_INIT 状态机读取完缓冲区");
                return;
            }
            if (recv_size < suppose_size) {
                lp_conn->s_curstat = _PKG_BD_RECVING;
                continue;
            }
            // 包体收取完整 进入处理函数
            pkg_body_proc(lp_conn);
        }

        if (lp_conn->s_curstat == _PKG_BD_RECVING) {
            suppose_size = lp_conn->s_recvlen;
            int recv_size = pkg_body_recv(lp_conn);
            // log_error_core(LOG_STDERR, 0, "收到包体长度 [%d]", recv_size);
            if (recv_size == -1) {
                return;
            }
            if (recv_size == 0) {
                log_error_core(LOG_INFO, 0, "_PKG_BD_RECVING 状态机读取完缓冲区");
                return;
            }
            if (recv_size < suppose_size) {
                lp_conn->s_curstat = _PKG_BD_RECVING;
                continue;
            }
            pkg_body_proc(lp_conn);
        }
    }  // end while (1)
}

/**
 * @brief 调用 recvproc，要求收取 lp_curconn->s_recvlen 长度的数据
 * @param lp_curcon 此时 stat 状态为 HD_INIT 或 HD_RECVING
 * @return 收全包头 or 没有收全，返回读取到实际长度
 * return -1 说明 recvproc return -1，该连接要进行回收
 * @details 若收全了指定长度 lp_curconn->s_precvbuf 不会改变位置，若没有收全指定长度 lp_curconn->s_precvbuf 会更新
 */
int CSocket::pkg_header_recv(lp_connection_t lp_curconn) {
    u_char curstat = lp_curconn->s_curstat;

    if (curstat != _PKG_HD_INIT && curstat != _PKG_HD_RECVING) {
        log_error_core(LOG_ALERT, 0 , "包头收取函数: 状态不符合调用要求");
        return -1;
    }
    ssize_t real_size = recvproc(lp_curconn, lp_curconn->s_precvbuf, lp_curconn->s_recvlen);
    if (real_size == -1) { return -1; }  // 已打印日志
    if (real_size < lp_curconn->s_recvlen) {  // 此次读取未读取到要求长度的数据，real_size 可能 == 0
        lp_curconn->s_precvbuf += real_size;
        lp_curconn->s_recvlen -= real_size;
    }
    return real_size; 
}


/**
 * @brief 包头处理函数，构造消息头，分配总内存，负责状态转移，lp_curconn->s_precvbuf 定位到分配内存处，准备收包体
 * @return int 返回包头处理结果 -1：包头非法，需要释放已分配的内存，这时候我们未收取包体，未创建消息头，因此只释放 headerinfo
 */
void CSocket::pkg_header_proc(lp_connection_t lp_curconn) {
    // log_error_core(LOG_STDERR, 0, "进入包头处理函数"); 
    size_t pkg_size = ntohs(lp_curconn->s_headerinfo->pkgLen);  // size_t 是 unsigned int 型，可用 uint16_t 代替

    // 根据包头内指定的包长度，判断包头是否合法
    if (pkg_size < PKG_HEADER_LEN || pkg_size > _PKG_MAX_LENGTH-1000) { 
        log_error_core(LOG_ALERT, 0, "包头不合法，重新回到 _PKG_HD_INIT"); 
        // p_mem_manager->FreeMemory(lp_curconn->s_headerinfo);
        lp_curconn->s_curstat = _PKG_HD_INIT;
        return;                 
    }
    // 分配内存，作为 s_msgmem
    size_t msg_size = MSG_HEADER_LEN + pkg_size;
    size_t pkg_body_size = pkg_size - PKG_HEADER_LEN;
    lp_curconn->s_msgmem = new char[msg_size+1]();
    
    // 构建并拷贝消息头
    STRUC_MSG_HEADER msg_header;
    msg_header.lp_curconn = lp_curconn;
    msg_header.msg_cursequence = lp_curconn->s_cursequence;

    char* buf = (char*)memcpy(lp_curconn->s_msgmem, &msg_header, MSG_HEADER_LEN) + MSG_HEADER_LEN;
    lp_curconn->s_precvbuf = (char*)memcpy(buf, lp_curconn->s_headerinfo, PKG_HEADER_LEN) + PKG_HEADER_LEN;  // 定位到包体需要读取到的位置
    lp_curconn->s_recvlen = pkg_body_size;

    if (pkg_body_size == 0) {
        pkg_body_proc(lp_curconn);
        return;
    }
    lp_curconn->s_curstat = _PKG_BD_INIT;  // 开始收取包体
    return;
}


int CSocket::pkg_body_recv(lp_connection_t lp_curconn) { 
    u_char curstat = lp_curconn->s_curstat;
    if (curstat != _PKG_BD_INIT && curstat != _PKG_BD_RECVING) {
        log_error_core(LOG_ALERT, 0 , "包体收取函数: 状态不符合调用要求");
        return -1;
    }
    ssize_t real_size = recvproc(lp_curconn, lp_curconn->s_precvbuf, lp_curconn->s_recvlen);
    if (real_size < 0) {
        return -1;
    }
    if (real_size < lp_curconn->s_recvlen) {  
        lp_curconn->s_precvbuf += real_size;
        lp_curconn->s_recvlen -= real_size;
    }
    return real_size; 
}

/**
 * @brief 包体处理函数 放入消息处理队列
 * @param lp_curconn 
 * @return int 
 */
void CSocket::pkg_body_proc(lp_connection_t lp_conn) {
    log_error_core(LOG_INFO, 0, "包体收取结束，放入消息队列 连接：[%d]", lp_conn->fd);
    g_threadpoll.InMsgRecv(lp_conn->s_msgmem);  
    lp_conn->s_msgmem = nullptr;
    p_mem_manager->FreeMemory(lp_conn->s_headerinfo);
    lp_conn->PutToStateMach();
    return;
}