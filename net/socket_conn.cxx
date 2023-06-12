
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "c_lock.h"
#include "c_socket.h"
#include "comm.h"
#include "func.h"
#include "global.h"
#include "macro.h"

listening_s::listening_s() {
    fd         = -1;
    port       = 0;
    p_connitem = nullptr;
}

listening_s::listening_s(int listenfd, int port_value) : fd(listenfd), port(port_value) {
    p_connitem = nullptr;
}

/**
 * @brief 待命连接 fd = listenfd，有效连接 fd = -1 后赋值 connfd
 */
connection_s::connection_s(int fd) : fd(fd) {
    sequence = 0;
    pthread_mutex_init(&conn_mutex, NULL);
}

/**
 * @brief Destroy the connection s::connection s object
 * @details 消息相关指针需要确保置空
 */
connection_s::~connection_s() {
    // 收取消息
    recv_str  = nullptr;
    p_msgrecv = nullptr;

    // 发送消息
    send_str  = nullptr;
    p_msgsend = nullptr;
    pthread_mutex_destroy(&conn_mutex);
}

/**
 * @brief 连接重新进入收取状态机的成员设置
 */
void connection_s::PutToStateMach() {
    p_headerinfo    = nullptr;
    p_recvbuf       = nullptr;
    recvlen         = PKG_HEADER_LEN;
    recv_str        = nullptr;
    p_msgrecv       = nullptr;
    curstat         = _PKG_HD_INIT;
    recvlen_already = 0;
}

/**
 * @brief 作为有效连接启用，有效连接来自 空闲连接列表 或者新构造的 conn_t
 */
void connection_s::GetOneToUse() {
    ++sequence;
    fd              = -1;
    events          = 0;

    iThrowsendCount = 0;
    sendlen_already = 0;
    isend_count     = 0;

    inrecyList       = 0;
    istimeout        = false;
    ping_update_time = time(NULL);
    PutToStateMach();
}

/**
 * @brief 连接对象相关成员的设置，用于延迟回收线程，收发消息相关指针需要置空
 * 这里可以额外进行判断 conn_t.fd 和 inrecyList
 */
void connection_s::PutOneToFree() {
    ++sequence;

    // 收取消息相关指针
    p_recvbuf = nullptr;
    recv_str  = nullptr;
    p_msgrecv = nullptr;

    // 发送相关指针
    p_sendbuf = nullptr;
    send_str  = nullptr;
    p_msgsend = nullptr;

    iThrowsendCount = 0;
}

/**
 * @brief 判断连接对象是否过期失效
 */
bool connection_s::JudgeOutdate(int cur_sequence) {
    if (fd <= 0 || inrecyList == 1 || sequence != cur_sequence) {
        // log_error_core(LOG_INFO, 0, "连接已失效 [%d] [%d] [%d] [%d]", fd, inrecyList, sequence, cur_sequence);
        return false;
    }
    return true;
}

/**
 * @brief 获取有效连接时调用此函数
 * @return lp_connection_t 返回有效连接
 */
lp_connection_t CSocket::get_connection_item() {
    CLock           lock(&m_socketmutex);
    lp_connection_t lp_getconn = nullptr;
    if (!m_free_connectionList.empty()) {
        lp_getconn = m_free_connectionList.front();
        m_free_connectionList.pop();
        --m_free_connection_count;
    } else {                                // 空闲列表中已没有连接，需要创建更多连接
        lp_getconn = new connection_t(-1);  // 对于 shared_ptr push 进了 connectionList，引用计数++
        m_connectionList.push(lp_getconn);
        ++m_total_connection_count;  // 总连接数
    }
    return lp_getconn;
}

/**
 * @brief 连接回到空闲连接池以备，关闭连接时调用
 */
void CSocket::free_connection_item(lp_connection_t lp_conn) {
    CLock lock(&m_socketmutex);
    m_free_connectionList.push(lp_conn);
    ++m_free_connection_count;
    return;
}

/**
 * @brief 此时的连接对象不在监听树上，在树上意味着随时可能来数据
 * 因此是添加到监听树时或之前出错，则调用此函数，此时也应当检查 fd_close 和 istimeout
 * @param lp_conn
 */
void CSocket::close_connection(lp_connection_t lp_conn) {
    CLock lock(&lp_conn->conn_mutex);
    int   fd_close = lp_conn->fd;
    if (fd_close == -1 || lp_conn->istimeout == true) {  // 此种情况理论上不应该出现
        log_error_core(LOG_ALERT, 0, "close_connection has failed");
        exit(-1);
    }
    if (close(fd_close) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd failed at CSocket::close_connection");
        exit(-1);
    }
    lp_conn->fd = -1;
    ++lp_conn->sequence;
    free_connection_item(lp_conn);
    return;
}

/**
 * @brief 延迟回收机制 以异步的方式交给线程去处理
 * 需要判断，是否已经过超时踢出，若是则不再重复关闭
 * @param lp_curconn 此时传入的 lp_curconn != nullptr 调用之后 lp_curconn 重新回到初始化状态
 */
void CSocket::close_accepted_connection(lp_connection_t lp_conn, bool istimeout_close) {
    // log_error_core(0, 0, "关闭了 connfd = [%d]，开始放入回收队列", lp_conn->fd);
    CLock lock(&lp_conn->conn_mutex);
    int   fd_toclose = lp_conn->fd;
    if (fd_toclose == -1) {
        log_error_core(LOG_EMERG, 0, "连接对象的已进行关闭，close_connection 返回");
        return;
    }
    if (istimeout_close == false) {
        if (lp_conn->istimeout == true) {
            log_error_core(LOG_EMERG, 0, "非超时踢出情况下调用，但连接对象已被超时踢出，close_connection 返回");
            return;
        }
    }
    epoll_oper_event(fd_toclose, EPOLL_CTL_DEL, 0, 0, lp_conn);  // 移除监听，避免 fd 冲突
    if (close(fd_toclose) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd of current conn_t has failed at [%s]", "CSocket::close_accepted_connection");
        exit(-1);
    }
    lp_conn->fd = -1;
    if (lp_conn->iThrowsendCount > 0) {
        --lp_conn->iThrowsendCount;
    }
    --m_online_count;
    InRecyConnQueue(lp_conn);
    return;
}
