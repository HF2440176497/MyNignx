
#include <errno.h>
#include <string.h>
#include <pthread.h>

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
    memset(&s_sockaddr, 0, ADDR_LEN);
    s_inrecyList = 0;
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
    s_msgmem = nullptr;
    s_headerinfo = nullptr;
    s_precvbuf = nullptr;
    s_recvlen = PKG_HEADER_LEN;
    return;
}

/**
 * @brief 作为有效连接启用，接着进入状态机
 * 需要清空之前处理线程对连接的作用：
 */
void connection_s::GetOneToUse(const int connfd, struct sockaddr* lp_connaddr) {
    fd = connfd;
    memcpy(&s_sockaddr, lp_connaddr, ADDR_LEN);

    s_cursequence++;
    PutToStateMach();  // 收取消息的相关成员

    // 回收队列
    s_inrecyList = 0;   // 不在回收队列内
    s_inrevy_time = 0;  // 此时不进行标记时间
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
        m_free_connectionList.pop_front();
        m_free_connection_count--;
    } else {  // 空闲列表中已没有连接，需要创建更多连接
        log_error_core(LOG_INFO, 0, "空闲连接不足，创建一个额外连接对象 当前连接总数[%d]", m_connection_count+1);
        lp_getconn = new connection_t();
        m_connectionList.push_back(lp_getconn);
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
    m_free_connectionList.push_back(lp_conn);
    m_free_connection_count++;
    return;
}