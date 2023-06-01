// 与心跳包相关的程序
// 
// 
// 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include <func.h>
#include <c_lock.h>
#include <c_socket.h>
#include <comm.h>
#include <macro.h>
#include <global.h>
#include <c_socketlogic.h>

/**
 * @brief 新连接加入 map，更新连接的 time_t 成员
 * 
 * @param lp_conn 
 */
void CSocket::AddToTimerQueue(lp_connection_t lp_conn) {
    time_t cur_time = time(NULL);
    lp_conn->ping_update_time = cur_time;
    CLock lock(&m_timer_mutex);

    // 构造消息头
    LPSTRUC_MSG_HEADER p_mhead = (LPSTRUC_MSG_HEADER)(p_mem_manager->AllocMemory(MSG_HEADER_LEN, true));
    p_mhead->lp_curconn = lp_conn;
    p_mhead->msg_cursequence = lp_conn->s_cursequence;

    // 插入到计时队列
    m_timermap.insert(std::make_pair(cur_time + m_waittime, std::shared_ptr<STRUC_MSG_HEADER>(p_mhead)));
    p_mhead = nullptr;
    m_timer_value = m_timermap.begin()->first;
    ++m_timermap_size;  // 互斥量保护
    log_error_core(LOG_INFO, 0, "连接 [%d] 加入到计时队列，目前计时队列大小 [%d]", m_timermap.size());
    return;
}

/**
 * @brief 将 shared_ptr 置空，清空所有 map 元素
 */
void CSocket::timermap_clean() {
    for (auto it = m_timermap.begin(); it != m_timermap.end(); ) {
        it->second = nullptr;
        it = m_timermap.erase(it);
        --m_timermap_size;
    }
    m_timermap.clear();
}

/**
 * @brief 线程并非时刻监视，sleep 之后继续循环
 * 仍然是 while 结构
 */
void* CSocket::ServerTimerQueueThreadFunc(void* lp_item) {
    log_error_core(LOG_INFO, 0, "监视线程开始运行。。。");
    ThreadItem* lp_thread = static_cast<ThreadItem*>(lp_item);
    lp_thread->running = true;
    CSocket* lp_socket = lp_thread->lp_socket;

    while (g_stopEvent == 0 && lp_thread->ifshutdown == false) {
        if (lp_socket->m_TimeEnable == 0) {
            log_error_core(LOG_INFO, 0, "未开启定时监视功能，线程退出");
            break;
        }
        time_t cur_time = time(NULL);
        time_t maptime  = lp_socket->m_timer_value;

        if (maptime > cur_time || lp_socket->m_timermap_size == 0) {  // 简单判断：未到时间或队列为空
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }
        std::shared_ptr<STRUC_MSG_HEADER> ptmp_ptr = nullptr;      // 保存 GetOverTime 返回的结果
        std::list<std::shared_ptr<STRUC_MSG_HEADER>> list_toexam;  // 检查时间已到，待检查队列
        
        pthread_mutex_lock(&lp_socket->m_timer_mutex);
        while (ptmp_ptr = lp_socket->GetOverTime(cur_time)) {  // 直到返回 nullptr
            list_toexam.push_back(ptmp_ptr);
        }
        ptmp_ptr = nullptr;  // 类似于 ptmp_ptr_toexam = nullptr; 将智能指针彻底移交
        std::shared_ptr<STRUC_MSG_HEADER> ptmp_ptr_toexam = nullptr;
        // log_error_core(LOG_INFO, 0, "监视线程，此轮检查数目 [%d]", list_toexam.size());  
        for (auto it = list_toexam.begin(); it != list_toexam.end(); ) {
            ptmp_ptr_toexam = *it;
            lp_socket->TimeCheckingProc(ptmp_ptr_toexam.get(), cur_time);  // 此时
            // 如果计时队列为空，GetOverTime 返回 nullptr，list 就会为空，因此 TimeCheckingProc 函数体内 map 应当非空
            // TimeCheckingProc 内部必须将 it->get() 彻底处理完，否则在外部 ptmp_ptr_toexam 就失效
            it = list_toexam.erase(it);
        }
        ptmp_ptr_toexam = nullptr;
        pthread_mutex_unlock(&lp_socket->m_timer_mutex);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    } // end while
    lp_thread->running = false;
    return (void*)(0);
}

/**
 * @brief 根据队首元素返回指针，若当前都没到检查时间，则返回 nullptr
 * 
 * 调用者负责互斥量保护
 * 需要完成：更新 m_timer_value
 * @param time 
 * @return 当没有达到检验时间，或计时队列为空则返回 nullptr
 */
std::shared_ptr<STRUC_MSG_HEADER> CSocket::GetOverTime(time_t time) {
    if (m_timermap_size == 0 || m_timermap.empty()) {
        return nullptr;
    }
    time_t fristit_time = m_timermap.begin()->first;
    std::shared_ptr<STRUC_MSG_HEADER> p_mhead_ptr = nullptr;

    if (fristit_time <= time) {
        p_mhead_ptr = RemoveTimerFrist();
        LPSTRUC_MSG_HEADER p_get_mhead = p_mhead_ptr.get();
        LPSTRUC_MSG_HEADER p_new_mhead = (LPSTRUC_MSG_HEADER)(p_mem_manager->AllocMemory(MSG_HEADER_LEN, true));
        memcpy(p_new_mhead, p_get_mhead, MSG_HEADER_LEN);
        m_timermap.insert(std::make_pair(time + m_waittime, std::shared_ptr<STRUC_MSG_HEADER>(p_new_mhead)));
        ++m_timermap_size;
        p_get_mhead = nullptr;  // 不再使用时置空
        p_new_mhead = nullptr;  // 移交给计时队列中的元素
    } else {
        return nullptr;
    }
    if (m_timermap_size > 0) {
        m_timer_value = m_timermap.begin()->first;
    }
    return p_mhead_ptr;
}

/**
 * @brief 根据待检查队列中消息头，取得连接对象，
 * 若超时，则调用 TimeOutProc；若未超时，不会处理
 * @param p_mhead_inlist 
 */
void CSocket::TimeCheckingProc(LPSTRUC_MSG_HEADER p_mhead_inlist, time_t cur_time) {
    lp_connection_t lp_conn = p_mhead_inlist->lp_curconn;
    pthread_mutex_lock(&lp_conn->s_connmutex);  // 加锁是为了进行过期判断
    if (lp_conn->JudgeOutdate(p_mhead_inlist->msg_cursequence) == false) {
        pthread_mutex_unlock(&lp_conn->s_connmutex);
        DeleteFromTimerQueue(p_mhead_inlist);  
        log_error_core(LOG_ERR, 0, "JudgeOutdate 返回，TimeCheckingProc 返回");
        return;
    }
    int fd_toclose = lp_conn->fd;
    // log_error_core(LOG_INFO, 0, "连接 [%d] 距离上次心跳包时间长度 [%d]", fd_toclose, (cur_time - lp_conn->ping_update_time));
    if ((cur_time - lp_conn->ping_update_time) > (3 * m_waittime)) {
        lp_conn->istimeout = true;  // 标记进行了超时踢出
        pthread_mutex_unlock(&lp_conn->s_connmutex);
        log_error_core(LOG_INFO, 0, "连接 [%d] 已经过时间 [%d] 未及时发送心跳包，关闭并延迟回收", fd_toclose, (cur_time - lp_conn->ping_update_time));
        TimeOutProc(p_mhead_inlist); 
    } else { 
        pthread_mutex_unlock(&lp_conn->s_connmutex);
    }
    return;
}

/**
 * @brief 负责将 map 中的对应项删除，然后关闭连接
 * 我们的设想是：当设置 istimeout = true 进程中的关闭，应当判别 istimeout 为 false 时才可以关闭
 * 这样就要求 lp_conn 在超时踢出之后才
 * 但是这样的话，超时踢出的关闭
 * @param lp_conn 
 */
void CSocket::TimeOutProc(LPSTRUC_MSG_HEADER lp_mhead) {
    DeleteFromTimerQueue(lp_mhead);
    close_accepted_connection(lp_mhead->lp_curconn, true);
    return;
}

/**
 * @brief 根据计时队列项的消息头信息，准确识别剔除完全相同的项
 * @param lp_mhead 是
 * 出现了问题
 */ 
void CSocket::DeleteFromTimerQueue(LPSTRUC_MSG_HEADER lp_mhead) {
    if (m_timermap_size == 0 || m_timermap.empty()) {
        log_error_core(LOG_ALERT, 0, "出现错误，TimeOutProc 函数内计时队列为空");
        return;
    }
    for (auto it = m_timermap.begin(); it != m_timermap.end(); ) {
        std::shared_ptr<STRUC_MSG_HEADER> ptmp_ptr = it->second;
        auto ptmp = ptmp_ptr.get();  // 表示计时队列中的项 检验完全相同的才能剔除
        if (ptmp->lp_curconn == lp_mhead->lp_curconn && ptmp->msg_cursequence == lp_mhead->msg_cursequence) {
            it = m_timermap.erase(it);
            --m_timermap_size;
            log_error_core(LOG_INFO, 0, "计时队列删除一项 目前含有数目 [%d]", m_timermap_size);
        } 
        else { ++it; }
    }
    if (m_timermap_size > 0) {
        m_timer_value = m_timermap.begin()->first;
    }
    return;
}

/**
 * @brief 这里保证计时队列不为空
 */
std::shared_ptr<STRUC_MSG_HEADER> CSocket::RemoveTimerFrist() {
    auto it = m_timermap.begin();
    auto ptmp = it->second;
    m_timermap.erase(it);
    --m_timermap_size;
    if (m_timermap_size > 0) {
        m_timer_value = m_timermap.begin()->first;
    }
    return ptmp;
}

