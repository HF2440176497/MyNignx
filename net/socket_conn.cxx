
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "macro.h"
#include "global.h"
#include "comm.h"
#include "c_socket.h"
#include "func.h"
#include "c_lock.h"

listening_s::listening_s(int listenfd, int port_value): fd(listenfd), port(port_value){
    s_lpconnection = nullptr;
}

/**
 * @brief 创建待命连接时会传入参数
 */
connection_s::connection_s(int fd): fd(fd) {
    s_cursequence = 0;
    pthread_mutex_init(&s_connmutex, NULL);
}

connection_s::~connection_s() {
    pthread_mutex_destroy(&s_connmutex);
}

/**
 * @brief 连接重新进入收取状态机的成员设置
 */
void connection_s::PutToStateMach() {
    s_curstat = _PKG_HD_INIT;
    s_recvlen = PKG_HEADER_LEN;
    return;
}

/**
 * @brief 作为有效连接启用，接着进入状态机
 * 需要清空之前处理线程对连接的作用：
 */
void connection_s::GetOneToUse() {
    // 收取消息的相关成员
    s_cursequence++;
    PutToStateMach();

    // 回收队列
    s_inrecyList = 0;   // 不在回收队列内
    s_inrevy_time = 0;  // 此时不进行标记时间

    // 发送消息的相关成员
    s_continuesend = 0;
    return;
}

/**
 * @brief 连接对象相关成员的设置，用于延迟回收线程
 */
void connection_s::PutOneToFree() {
    if (fd != -1) {
        log_error_core(LOG_ALERT, 0, "非法调用 PutOneToFree 此连接对象未正确关闭");
        return;
    }
    if (s_inrecyList != 1) {
        log_error_core(LOG_ALERT, 0, "非法调用 PutOneToFree 此连接对象未进入回收队列");
    }
    // 清空收取消息指针
    s_msgstr = nullptr;
    s_precvbuf = nullptr;

    // 清空发送消息指针
    s_sendbuf = nullptr;
    s_msgsendmem = nullptr;

    // 标记序号
    s_cursequence++;
    return;
}

/**
 * @brief 判断连接对象是否过期失效
 */
bool connection_s::JudgeOutdate(int sequence) {
    if (fd <= 0) {
        log_error_core(LOG_STDERR, 0, "线程当前处理连接对象失效");
        return false;
    }
    if (s_inrecyList == 1) {
        log_error_core(LOG_ALERT, 0, "线程当前处理连接对象已进入回收队列，不再处理");
        return false;
    }
    if (s_cursequence != sequence) {
        log_error_core(LOG_STDERR, 0, "线程当前处理连接可能已回收");
        return false;
    }
    return true;
}


/**
 * @brief 获取有效连接时调用此函数 配合 GetOneToUse
 * m_lplistenitem 已在创建线程池中设置
 * @return lp_connection_t 
 */
lp_connection_t CSocket::get_connection_item() {
    CLock lock(&m_socketmutex);
    lp_connection_t lp_getconn;
    if (!m_free_connectionList.empty()) {
        lp_getconn = m_free_connectionList.front();
        m_free_connectionList.pop();
        m_free_connection_count--;
    } else {  // 空闲列表中已没有连接，需要创建更多连接
        log_error_core(LOG_INFO, 0, "空闲连接不足，创建一个额外连接对象 当前连接总数[%d]", m_connection_count+1);
        lp_getconn = new connection_t();  // 对于 shared_ptr push 进了 connectionList，引用计数++
        m_connectionList.push(lp_getconn);
        m_connection_count++;  // 总连接数
    }
    lp_getconn->s_lplistening = m_lplistenitem;
    return lp_getconn;
}


/**
 * @brief 连接回到空闲连接池以备，关闭连接时调用
 */
void CSocket::free_connection_item(lp_connection_t lp_conn) {
    log_error_core(LOG_INFO, 0, "free_connection_item");
    CLock lock(&m_socketmutex);    
    m_free_connectionList.push(lp_conn);
    m_free_connection_count++;
    return;
}

/**
 * @brief 此时的连接对象不在监听树上，在树上意味着随时可能来数据
 * 因此是添加到监听树时或之前出错，则调用此函数
 * @param lp_conn 
 */
void CSocket::close_connection(lp_connection_t lp_conn) {
    CLock lock(&lp_conn->s_connmutex);
    int fd_close = lp_conn->fd;
    if (close(fd_close) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd of current conn_t has failed at [%s]", "CSocket::close_connection");
        exit(-1);
    }
    lp_conn->fd = -1;
    lp_conn->s_cursequence++;
    free_connection_item(lp_conn);
    return;
}

/**
 * @brief 6.3 更新：延迟回收机制 以异步的方式交给线程去处理
 * 需要判断，lp_conn 是否已经在 InRecyQueue 中，是的话不需要再添加进去了
 * @param lp_curconn 此时传入的 lp_curconn != nullptr 调用之后 lp_curconn 重新回到初始化状态
 */
void CSocket::close_accepted_connection(lp_connection_t lp_conn) {
    log_error_core(0, 0, "关闭了 connfd = [%d]，开始放入回收队列", lp_conn->fd);
    CLock lock(&lp_conn->s_connmutex);
    int fd_toclose = lp_conn->fd;
    epoll_oper_event(fd_toclose, EPOLL_CTL_DEL, 0, 0, lp_conn);
    if (close(fd_toclose) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd of current conn_t has failed at [%s]", "CSocket::close_accepted_connection");
        exit(-1);
    }
    lp_conn->fd = -1;
    InRecyConnQueue(lp_conn);
    return;
}
