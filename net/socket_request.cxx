
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

/**
 * @brief 此函数用于读取指定长度的数据
 * @param lp_curconn 
 * @param buf 
 * @param buflen 要求接收的数据的长度 不要传入 0 以免发生意外情况
 * @return 返回 -1 时调用者直接返回，结束此次 read_handler 调用
 * @details 
 */
ssize_t CSocket::recvproc(lp_connection_t lp_conn, char* buf, ssize_t recv_len) {
    int fd = lp_conn->fd;
    char* cur_buf = buf;

    ssize_t n = recv(fd, cur_buf, recv_len, 0);
    if (n == 0) {
        if (errno == 0) {  // 此时返回 0，表示什么都没有读取
            return n;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;
        } else {  // 以下错误认为对端已关闭，此时服务端需要关闭
            // log_error_core(LOG_STDERR, 0 , "recvproc: 关闭连接, errno: [%d] fd: [%d]", errno, fd);
            close_accepted_connection(lp_conn, false);
            return -1;
        } 
    }
    if (n < 0) {
        if (errno == EINTR) {  // 此时不应算作错误，可以视为此次读取无效
            // ...
            return 0;
        } 
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 理论上不该出现此错误，不做处理返回即可
            // ...
            return -1;
        }
        if (errno == ECONNRESET) {  // 对端客户端直接整个关闭
            // ...
        } else {
            log_error_core(LOG_ERR, 0, "CSocket::recvproc 错误码 [%d]", errno);
        }
        close_accepted_connection(lp_conn, false);
        return -1;
    } 
    return n;
}

/**
 * @brief 发送指定长度的数据，直到发送完或缓冲区满则返回 
 * 发送线程调用，此时连接对象在互斥量保护中
 * @param lp_conn 
 * @param buf 
 * @param send_len 理应发送长度
 * @return ssize_t -1 发送出错 此条发送消息作废 外部进行内存回收
 */
ssize_t CSocket::sendproc(lp_connection_t lp_conn, char* buf, ssize_t send_len) {
    char* cur_buf = buf;  
    int fd = lp_conn->fd;
    while (true) {
        ssize_t n = send(fd, cur_buf, send_len, 0);
        if (n < 0) {
            if (errno == EINTR) {  // 再次尝试发送
                continue;
            } else if (errno == EAGAIN) {  // 不算错误 缓冲区满时可以按照正常情况继续处理
                return 0;
            } else {
                log_error_core(LOG_ALERT, errno , "发送出错 errno: [%d] fd: [%d]", errno, fd);
                return -1;
            }
        } else if (n == 0) {
            log_error_core(LOG_ALERT, errno , "发送时对端关闭, fd: [%d]", fd);
            return -1;
        } else {
            return n;
        }
    }
    return -1;
}


/**
 * @brief 收包的有限状态机：需要完成收取一个包
 * @param lp_effec_conn 
 * @todo 
 */
void CSocket::event_readable_request_handler(lp_connection_t lp_conn) {
    size_t suppose_size;

    if (lp_conn->curstat == _PKG_HD_INIT) {
        char* header_mem = (char*)p_mem_manager->AllocMemory(PKG_HEADER_LEN, true);
        lp_conn->p_headerinfo = (LPCOMM_PKG_HEADER)header_mem;

        lp_conn->p_recvbuf = header_mem;  // header_recv 将包头信息收取到 header_mem
        lp_conn->recvlen = PKG_HEADER_LEN;

        suppose_size = lp_conn->recvlen;
        int recv_size = pkg_header_recv(lp_conn);
        if (recv_size == -1) {  // recvproc return -1 表示出错（包括 EAGAIN 错误）或对端关闭 
            return;
        }
        if (recv_size == suppose_size) {
            pkg_header_proc(lp_conn);
        } else {
            lp_conn->curstat = _PKG_HD_RECVING;  
        }    
    } else if (lp_conn->curstat == _PKG_HD_RECVING) {  
        suppose_size = lp_conn->recvlen;
        if (suppose_size == 0) { return; }
        int recv_size = pkg_header_recv(lp_conn);  // 继续读取包头
        if (recv_size == -1) {
            return;
        }
        if (recv_size == suppose_size) {
            pkg_header_proc(lp_conn);
        } else {
            lp_conn->curstat = _PKG_HD_RECVING;  
        }
    } else if (lp_conn->curstat == _PKG_BD_INIT) {
        suppose_size = lp_conn->recvlen;  // suppose_size 此时为包体长度
        if (suppose_size == 0) { return; }
        int recv_size = pkg_body_recv(lp_conn);
        if (recv_size == -1) { 
            return;  // recvproc return -1 表示出错（包括 EAGAIN 错误）或对端关闭 
        }
        if (recv_size == suppose_size) {
            pkg_body_proc(lp_conn);
        } else {  // 可能返回 0，继续收取
            lp_conn->curstat = _PKG_BD_RECVING;  
        }    
    } else if (lp_conn->curstat == _PKG_BD_RECVING) {
        suppose_size = lp_conn->recvlen;
        if (suppose_size == 0) { return; }
        int recv_size = pkg_body_recv(lp_conn);
        if (recv_size == -1) {
            return;
        }
        if (recv_size == suppose_size) {
            pkg_body_proc(lp_conn);
        } else {
            lp_conn->curstat = _PKG_BD_RECVING;  
        }
    }
    return;
}

/**
 * @brief 调用 recvproc，要求收取 lp_curconn->recvlen 长度的数据
 * @param lp_curcon 此时 stat 状态为 HD_INIT 或 HD_RECVING
 * @return 收全包头 or 没有收全，返回读取到实际长度
 * return -1 说明 recvproc return -1，该连接要进行回收
 * @details 若收全了指定长度 lp_curconn->p_recvbuf 不会改变位置，若没有收全指定长度 lp_curconn->p_recvbuf 会更新
 */
int CSocket::pkg_header_recv(lp_connection_t lp_conn) {
    u_char curstat = lp_conn->curstat;

    if (curstat != _PKG_HD_INIT && curstat != _PKG_HD_RECVING) {
        log_error_core(LOG_ALERT, 0 , "包头收取函数: 状态不符合调用要求");
        return -1;
    }
    ssize_t real_size = recvproc(lp_conn, lp_conn->p_recvbuf, lp_conn->recvlen);
    if (real_size == -1) {  // 出错情况已经由 recvproc 处理
        return -1;
    }
    if (real_size < lp_conn->recvlen) {  // 此次读取未读取到要求长度的数据，real_size 可能 == 0
        lp_conn->p_recvbuf += real_size;
        lp_conn->recvlen -= real_size;
    }
    lp_conn->recvlen_already += real_size;
    return real_size; 
}


/**
 * @brief 包头处理函数，构造消息头，分配总内存，负责状态转移，lp_curconn->p_recvbuf 定位到分配内存处，准备收包体
 * @return int 返回包头处理结果 -1：包头非法，需要释放已分配的内存，这时候我们未收取包体，未创建消息头，因此只释放 headerinfo
 */
void CSocket::pkg_header_proc(lp_connection_t lp_conn) {
    // log_error_core(LOG_STDERR, 0, "进入包头处理函数"); 
    size_t pkg_size = ntohs(lp_conn->p_headerinfo->pkgLen);  // size_t 是 unsigned int 型，可用 uint16_t 代替

    // 根据包头内指定的包长度，判断包头是否合法
    if (pkg_size < PKG_HEADER_LEN || pkg_size > _PKG_MAX_LENGTH-1000) { 
        log_error_core(LOG_ALERT, 0, "包头不合法，重新回到 _PKG_HD_INIT"); 
        p_mem_manager->FreeMemory(lp_conn->p_headerinfo);
        lp_conn->curstat = _PKG_HD_INIT;
        return;                 
    }
    size_t msg_size = MSG_HEADER_LEN + pkg_size;
    size_t pkg_body_size = pkg_size - PKG_HEADER_LEN;

    // 使用 make_shared 无法定义自己的删除器
    lp_conn->p_msgrecv = std::shared_ptr<char>(new char[msg_size + 1](), [](char* p) { delete[] p; });
    lp_conn->recv_str = lp_conn->p_msgrecv.get();

    // 构建并拷贝消息头
    STRUC_MSG_HEADER msg_header;
    msg_header.lp_curconn = lp_conn;
    msg_header.msg_cursequence = lp_conn->sequence;

    char* buf = (char*)memcpy(lp_conn->recv_str, &msg_header, MSG_HEADER_LEN) + MSG_HEADER_LEN;       // 消息头是后添加的，需要定位到包头位置
    lp_conn->p_recvbuf = (char*)memcpy(buf, lp_conn->p_headerinfo, PKG_HEADER_LEN) + PKG_HEADER_LEN;  // 定位到包体需要读取到的位置
    lp_conn->recvlen = pkg_body_size;

    if (pkg_body_size == 0) {
        pkg_body_proc(lp_conn);
        return;
    }
    lp_conn->curstat = _PKG_BD_INIT;  // 开始收取包体
    return;
}


int CSocket::pkg_body_recv(lp_connection_t lp_conn) { 
    u_char curstat = lp_conn->curstat;
    if (curstat != _PKG_BD_INIT && curstat != _PKG_BD_RECVING) {
        lp_conn->recv_str = nullptr;
        lp_conn->p_msgrecv = nullptr;
        p_mem_manager->FreeMemory(lp_conn->p_headerinfo);
        log_error_core(LOG_ALERT, 0 , "包体收取函数: 状态不符合调用要求");
        return -1;
    }
    ssize_t real_size = recvproc(lp_conn, lp_conn->p_recvbuf, lp_conn->recvlen);
    if (real_size == -1) {
        lp_conn->recv_str = nullptr;
        lp_conn->p_msgrecv = nullptr;
        return -1;
    }
    if (real_size < lp_conn->recvlen) {  
        lp_conn->p_recvbuf += real_size;
        lp_conn->recvlen -= real_size;
    }
    lp_conn->recvlen_already += real_size;
    return real_size; 
}

/**
 * @brief 包体处理函数 放入消息处理队列
 * @param lp_curconn 
 * @return int 
 */
void CSocket::pkg_body_proc(lp_connection_t lp_conn) {
    // log_error_core(LOG_INFO, 0, "状态机收取完包体，收取包长度 [%d]", lp_conn->recvlen_already);
    g_threadpool.InMsgRecv(lp_conn->p_msgrecv);        // 传入消息 shared_ptr 
    lp_conn->p_recvbuf = nullptr;
    lp_conn->recv_str = nullptr;                       // recv_str 由 p_msgrecv 得到，因此先置空 msgstr
    lp_conn->p_msgrecv = nullptr;                      // 最后置空 p_msgrecv
    p_mem_manager->FreeMemory(lp_conn->p_headerinfo);  // headerinfo 在状态机开始处分配，到状态机一轮结束后释放
    lp_conn->PutToStateMach();                         // 接下来继续收取消息
    return;
}


/**
 * @brief EPOLLOUT 的响应函数
 * @param lp_conn 
 */
void CSocket::event_writable_request_handler(lp_connection_t lp_conn) {
    int suppose_size = lp_conn->sendlen;
    ssize_t real_size = sendproc(lp_conn, lp_conn->p_sendbuf, lp_conn->sendlen);
    if (real_size >= 0) {
        if (real_size < suppose_size) {
            lp_conn->sendlen_already += real_size;
            lp_conn->p_sendbuf = lp_conn->p_sendbuf + real_size;
            lp_conn->sendlen = lp_conn->sendlen - real_size;
            return;  // 返回，等待下一次监听唤醒
        } else {  // 数据发送完毕
            lp_conn->sendlen_already += real_size;
            if (lp_conn->sendlen_already != lp_conn->sendlen_suppose) {  // 额外检验是否和一开始要发送的长度相等
                log_error_core(LOG_ALERT, 0, "发送长度不符 already: [%d] suppose: [%d]", lp_conn->sendlen_already, lp_conn->sendlen_suppose);
                return;
            }
            // log_error_core(LOG_INFO, 0, "发送成功，已发送长度 [%d]", lp_conn->sendlen_already);
            epoll_oper_event(lp_conn->fd, EPOLL_CTL_MOD, EPOLLOUT, 1, lp_conn);  // 去掉监听
        }
    }
    // 发送完成或发送出错
    int errnum = sem_post(&m_sendsem);
    if (errnum == -1) {
        log_error_core(LOG_ERR, 0, "writable_request_handler sem_post 失败");
    }
    lp_conn->send_str        = nullptr;
    lp_conn->p_sendbuf       = nullptr;
    lp_conn->p_msgsend       = nullptr;
    lp_conn->iThrowsendCount = 0;
    return;
}