
#include <errno.h>
#include <string.h>

#include "macro.h"
#include "global.h"
#include "c_socket.h"
#include "func.h"

/**
 * @brief 将连接对象 lp_curconn 成员恢复至初始状态，next 成员需外部赋值处理
 * @param lp_curconn 
 */
void CSocket::init_connection_item(lp_connection_t lp_curconn) {
    memset(lp_curconn, 0, sizeof(connection_t));
    lp_curconn->fd = -1;
    lp_curconn->s_cursequence = 0;
    lp_curconn->rhandler = nullptr;
    lp_curconn->s_lplistening = m_lplistenitem;

    lp_curconn->s_headerinfo = nullptr;
    lp_curconn->s_msgmem = nullptr;
    lp_curconn->s_precvbuf = nullptr;
    lp_curconn->s_curstat = _PKG_HD_INIT;
    return;
}

/**
 * @brief 获取、分配空闲链表中的元素 此函数只负责结构体成员 s_lplistening s_cursequence 的赋值
 * @details 具体操作是链表指针移动
 * @return 返回结构体指针 若返回 nullptr 说明此次没有分配
*/
lp_connection_t CSocket::get_connection_item() {

    if (m_free_lpconnections == nullptr) {
        log_error_core(NGX_LOG_ALERT, errno, "connection pool has exhausted at [%s]", "get_connection_item()");  // 连接池已耗尽
        return nullptr;
    }
    lp_connection_t lp_free_curconn = m_free_lpconnections;  // 保存返回值
    lp_connection_t lp_free_nextconn = lp_free_curconn->next;

    // 我们目的一直是返回 m_free_lpconnections 
    // 并且使 m_free_lpconnections 更新到后续的可行的节点
    // 因此返回时 lp_free_curconn 不会更新，更新 m_free_lpconnections

    while (lp_free_nextconn != nullptr) {
        if (lp_free_nextconn->fd == -1) 
            break;
        lp_free_nextconn = lp_free_nextconn->next;  // != -1 继续向后找
    }
    // 若 lp_free_nextconn == nullptr 不必再判断 lp_free_nextconn->fd
    // m_free_lpconnections = lp_free_nextconn = nullptr 此时返回的 lp_free_curconn 是最后一个结点

    m_free_lpconnections = lp_free_nextconn;
    ++lp_free_curconn->s_cursequence;  // 取用次数自增

    return lp_free_curconn;
}

/**
 * @brief 释放连接池的连接，该连接重新回到初始化的状态，但 s_cursequence 保留为原值
 */
void CSocket::free_connection_item(lp_connection_t lp_curconn) {

    uint64_t cursequence = lp_curconn->s_cursequence;
    init_connection_item(lp_curconn);
    lp_curconn->next = m_free_lpconnections;
    lp_curconn->s_cursequence = cursequence; ++lp_curconn->s_cursequence;

    m_free_lpconnections = lp_curconn;
    return;
}