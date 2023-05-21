

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "macro.h"
#include "global.h"
#include "c_conf.h"
#include "c_socket.h"
#include "c_lock.h"
#include "func.h"

// 静态成员变量初始化
std::list<lp_connection_t>  m_recy_connectionList = {};

CSocket::CSocket() {
    // 连接池相关 配置项的默认值
    m_port_count = 2;
    m_create_connections_count = 20;
    m_delay_time = 60;

    m_recy_connection_count = 0;
    m_msgtosend_count = 0;
    return;
}

CSocket::~CSocket() {
    event_close_listen();
    if (lp_standby_connitem) {
        delete lp_standby_connitem;
    }
    return;
}

void CSocket::ReadConf() {
    CConfig* p_config = CConfig::GetInstance();
    m_port_count = p_config->GetInt("ListenPortCount", m_port_count);
    m_create_connections_count = p_config->GetInt("ConnectionsToCreate", m_create_connections_count);
    m_delay_time = p_config->GetInt("Sock_RecyConnectionWaitTime", m_delay_time);
    return;
}

/**
 * @brief g_socket 有关的初始化工作，在父进程初始化中调用
 * 需要完成：配置文件读取
 * 参考代码还调用 event_open_listen 打开监听端口 我们并没有
 * 
 */
void CSocket::Initialize() {
    ReadConf();
    // 待补充...
    return;
}

/**
 * @brief CSocket 的主要定位就是：维护连接池及其收取功能
 * 何处调用：Initialize_SubProc() init_worker_process 创建
 */
void CSocket::Initialize_SubProc() {

    // 相关互斥量的初始化
    if (pthread_mutex_init(&m_socketmutex, NULL) != 0) {
        log_error_core(LOG_ALERT, errno, "m_socketmutex 初始化失败");
        exit(-1);
    }
    if (pthread_mutex_init(&m_sendmutex, NULL) != 0) {
        log_error_core(LOG_ALERT, errno, "m_sendmutex 初始化失败");
        exit(-1);
    }
    if (sem_init(&m_sendsem, 0, 0) == -1) {  // 进程内线程共享 初始值为 0
        log_error_core(LOG_ALERT, errno, "m_sendsem 初始化失败");
        exit(-1);
    }
    // 处理连接回收的线程 
    ThreadItem* lp_recythread = new ThreadItem(this);  // 非线程池的工作线程，而是 socket 的所属线程
    m_threadVector.push_back(lp_recythread);
    int errnum = pthread_create(&(lp_recythread->_handle), NULL, &CSocket::RecyConnThreadFunc, lp_recythread);  // 创建线程，错误不返回到errno，一般返回错误码
    if (errnum != 0) {
        log_error_core(LOG_ALERT, 0, "创建延迟回收线程失败，返回的错误码为 [%d]", errnum);
        exit(-1);
    }
    // 发送消息线程的线程
    ThreadItem* lp_sendthread = new ThreadItem(this);
    m_threadVector.push_back(lp_sendthread);
    errnum = pthread_create(&(lp_sendthread->_handle), NULL, &CSocket::SendMsgThreadFunc, lp_sendthread);
    if (errnum != 0) {
        log_error_core(LOG_ALERT, 0, "创建发送消息线程失败，返回的错误码为 [%d]", errnum);
        exit(-1);
    }
    return;
}

/**
 * @brief 清理连接池
 * 
 */
void CSocket::connectpool_clean() {
    log_error_core(LOG_INFO, 0, "CSocket::connectpool_clean");
    lp_connection_t lp_conn;
    while (!m_connectionList.empty()) {
        lp_conn = m_connectionList.front();
        m_connectionList.pop();
        delete lp_conn;
    }
    return;
}


void CSocket::sendmsglist_clean() {
    std::shared_ptr<char[]> msg_toclean;
    while (!m_send_msgList.empty()) {
        msg_toclean = m_send_msgList.front();
        m_send_msgList.pop_front();
        msg_toclean = nullptr;
    }
    return;
}

/**
 * @brief 
 * 对于发送消息线程，可能阻塞在 sem_wait，我们只需要调用一次 sem_post
 */
void CSocket::Shutdown_SubProc() {
    log_error_core(LOG_INFO, 0, "子线程退出");
    for (auto lp_thread:m_threadVector) {
        lp_thread->ifshutdown = true;  // 线程强制退出
    }
    if (sem_post(&m_sendsem) == -1) {
        log_error_core(LOG_ALERT, errno, "Shutdown_SubProc 信号量设置出错");
        return;
    }
wait:
    for (auto lp_thread:m_threadVector) {  // 发送线程，延迟回收线程
        if (lp_thread->running == true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            goto wait;
        }
        pthread_join(lp_thread->_handle, NULL);  // 所有线程已停止运行后，都会调用 join，不再跳转 
    }
    // 各辅助线程释放内存 
    for (auto lp_thread:m_threadVector) {
        delete lp_thread;                       // 所有的线程对象在 Initialize_SubProc 分配
    }
    m_threadVector.clear();

    connectpool_clean();
    sendmsglist_clean();

    // 互斥量与信号量
    pthread_mutex_destroy(&m_socketmutex);
    pthread_mutex_destroy(&m_recymutex);
    pthread_mutex_destroy(&m_sendmutex);
    sem_destroy(&m_sendsem);
    return;
}

// 分析：对于延迟回收线程来说，while 结构保证了会查询到现成的额退出设置
// 对于发送消息线程

/**
 * @brief 作为延迟回收线程的入口函数
 * @return void* 
 */
void* CSocket::RecyConnThreadFunc(void* lp_item) {
    log_error_core(LOG_INFO, 0, "延迟回收线程开始运行。。。");
    ThreadItem* lp_thread = static_cast<ThreadItem*>(lp_item);
    lp_thread->running = true;
    CSocket* lp_socket = lp_thread->lp_socket;
    std::list<lp_connection_t>& m_recy_connectionList = lp_socket->m_recy_connectionList;  // 传递引用，使得可以修改 g_socket 成员
    time_t cur_time;
    int errnum;
    while (g_stopEvent == 0 && lp_thread->ifshutdown == false) {  // 线程非退出
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        errnum = pthread_mutex_lock(&lp_socket->m_recymutex);
        if (errnum != 0) {
            log_error_core(LOG_INFO, 0, "延迟回收线程，加锁失败 at RecyConnThreadFunc");
        }
        if (g_stopEvent == 1 || lp_thread->ifshutdown == true) {  // 进程退出或是回收线程需要退出
            while (!m_recy_connectionList.empty()) {
                auto begin = m_recy_connectionList.begin();
                auto end = m_recy_connectionList.end();
                for (std::list<lp_connection_t>::iterator it = begin; it != end;) {
                    lp_connection_t lp_conn = *it;
                    it = m_recy_connectionList.erase(it);
                    lp_conn->PutOneToFree();
                    lp_socket->free_connection_item(lp_conn);
                }
            } // end while
            errnum = pthread_mutex_unlock(&lp_socket->m_recymutex);
            if (errnum != 0) {
                log_error_core(LOG_INFO, 0, "延迟回收线程 stopEvent == 0，解锁失败 at RecyConnThreadFunc");
                break;  // break while (1)
            }
            break;  // break while (1)
        }
        if (!m_recy_connectionList.empty()) {  // 进程非退出，且队列非空
            auto begin = m_recy_connectionList.begin();  // 目的：for 的每轮循环，begin、end 参照不变
            auto end = m_recy_connectionList.end();
            for (std::list<lp_connection_t>::iterator it = begin; it != end;) {
                cur_time = time(NULL);
                lp_connection_t lp_conn = *it;
                if ((lp_socket->m_delay_time + lp_conn->s_inrevy_time) < cur_time && g_stopEvent == 0) {  // 说明此时满足时间要求
                    log_error_core(LOG_INFO, 0, "时间条件满足，此连接放入回收队列的时间: %d", lp_conn->s_inrevy_time);
                    log_error_core(LOG_INFO, 0, "RecyConnThreadFunc 执行，地址：[%d] 连接对象被归还", lp_conn);
                    it = m_recy_connectionList.erase(it);
                    lp_conn->PutOneToFree();  // 存在重复释放
                    lp_socket->free_connection_item(lp_conn);
                } else {  // 没到释放时间
                    ++it;
                    continue;
                }
            }  // end for
        }
        errnum = pthread_mutex_unlock(&lp_socket->m_recymutex);
        if (errnum != 0) {
            log_error_core(LOG_INFO, 0, "延迟回收线程 连接回归完毕，解锁失败 at RecyConnThreadFunc");
            break;  // break while (1)
        }
    }  // end while(1) 回到 while(1)
    log_error_core(LOG_INFO, 0, "RecyConnThreadFunc 线程结束");
    lp_thread->running = false;
    return (void*)(0);
}


/**
 * @brief 何处调用：close_accepted_connection
 * @param lp_conn 此时仍然有 conn_mutex 保护 lp_conn
 */
void CSocket::InRecyConnQueue(lp_connection_t lp_conn) {
    if (lp_conn->s_inrecyList == 1) {
        log_error_core(LOG_ERR, 0, "当前连接已经位于延迟回收队列中 InRecyConnQueue 重复调用");
        return;
    }
    CLock lock(&m_recymutex);
    lp_conn->s_inrecyList = 1;  // 标记进入回收队列
    lp_conn->s_inrevy_time = time(NULL);
    m_recy_connectionList.push_back(lp_conn);
    return;
}


void* CSocket::SendMsgThreadFunc(void* lp_item) {
    log_error_core(LOG_INFO, 0, "发送消息线程开始运行。。。");
    ThreadItem* lp_thread = static_cast<ThreadItem*>(lp_item);
    lp_thread->running = true;
    CSocket* lp_socket = lp_thread->lp_socket;
    int errnum;

    char* msg_send;
    int suppose_size;  // 应当发送的长度
    
    while (g_stopEvent == 0 && lp_thread->ifshutdown == false) {  // 非退出
        if (sem_wait(&lp_socket->m_sendsem) == -1) {
            log_error_core(LOG_ERR, 0, "sem_wait 解除阻塞失败，线程退出");
            break;
        } 
        if (g_stopEvent == 1 || lp_thread->ifshutdown == true) {  // 解除阻塞有可能是要求退出
            break;
        }
        if (lp_thread->running == false) {
            lp_thread->running = true;
        }
        errnum = pthread_mutex_lock(&lp_socket->m_sendmutex);  // 保护发送队列
        if (errnum != 0) {
            log_error_core(LOG_ERR, 0, "SendMsgThreadFunc 发送队列加锁失败");
            break;
        }
        if (!lp_socket->m_send_msgList.empty()) {
            auto begin = lp_socket->m_send_msgList.begin();
            auto end = lp_socket->m_send_msgList.end();
            for (auto it = begin; it != end; ) {
                // 未发送完整的连接对象，也不会在队列中移除，因此只要未在队列中移除，那么就可以取到消息头、包头
                char* msg_send = it->get();  // 首先获得容器元素，再获得指向对象 小心使用
                LPSTRUC_MSG_HEADER lp_msghead_tosend = (LPSTRUC_MSG_HEADER)(msg_send);
                LPCOMM_PKG_HEADER lp_pkghead_tosend = (LPCOMM_PKG_HEADER)(msg_send + MSG_HEADER_LEN);
                lp_connection_t lp_conn = lp_msghead_tosend->lp_curconn;
                errnum = pthread_mutex_lock(&lp_conn->s_connmutex);
                if (errnum != 0) {
                    log_error_core(LOG_ERR, 0, "SendMsgThreadFunc 连接对象加锁失败");
                    ++it;
                    continue;
                }
                if (lp_conn->JudgeOutdate(lp_msghead_tosend->msg_cursequence) == false) {    
                    pthread_mutex_unlock(&lp_conn->s_connmutex);
                    ++it;
                    continue;
                }
                lp_conn->s_msgsendmem = *it;
                if (lp_conn->s_continuesend == 0) {  // 待发送的消息是完整的，可以从包头中获取 sendbuf sendlen 一开始会进入到此
                    lp_conn->s_sendlen_already = 0;
                    lp_conn->s_sendbuf = msg_send + MSG_HEADER_LEN;
                    lp_conn->s_sendlen = ntohs(lp_pkghead_tosend->pkgLen);  // 发送的消息已设置为网络字节序，这里需要转换回来
                }
                suppose_size = lp_conn->s_sendlen;
                int real_size = lp_socket->sendproc(lp_conn, lp_conn->s_sendbuf, lp_conn->s_sendlen);
                if (real_size == -1) {  // 发送出错或对方关闭连接，移除此消息
                    msg_send = nullptr;
                    lp_conn->s_sendbuf = nullptr;
                    it = lp_socket->m_send_msgList.erase(it);
                    lp_conn->s_msgsendmem = nullptr;
                    --lp_socket->m_msgtosend_count;
                    pthread_mutex_unlock(&lp_conn->s_connmutex);
                    continue;
                } else if (real_size < suppose_size) {  // 实际长度小于期望发送长度
                    if (lp_conn->s_continuesend == 0) {
                        lp_conn->s_continuesend = 1;
                    }
                    lp_conn->s_sendlen_already += real_size;
                    lp_conn->s_sendbuf += real_size;
                    lp_conn->s_sendlen -= real_size;
                    log_error_core(LOG_INFO, 0, "未发送完，已发送长度: [%d] 添加监听: [%d]", lp_conn->s_sendlen_already, lp_conn->fd);
                    lp_socket->epoll_oper_event(lp_conn->fd, EPOLL_CTL_MOD, EPOLLOUT, 0, lp_conn);  // 增加 OUT 监听
                    ++it;  // 是否若跳过此消息都可以，因为缓冲区有空间监听响应的时候也是由线程接着发送
                    // 当前连接的发送未发送完，遍历到同连接的新消息，到此连接时 s_continuesend == 1，sendbuf 仍指向先前消息
                    // 直到发送完原消息，才可能更新队列，更新 sendbuf，不会导致一条消息的发送被拆分

                } else {  // 发送完毕 此连接下次发送时，需要重新获取到 lp_conn 并设置 sendbuf sendlen
                    if (lp_conn->s_continuesend == 1) {
                        lp_conn->s_continuesend = 0;
                    }
                    lp_conn->s_sendlen_already += real_size;
                    if (lp_conn->s_sendlen_already != lp_conn->s_sendlen_suppose) {  // 额外检验是否和一开始要发送的长度相等
                        log_error_core(LOG_ALERT, 0, "发送长度不符 already: [%d] suppose: [%d]", lp_conn->s_sendlen_already, lp_conn->s_sendlen_suppose);
                        return (void*)(0);
                    }
                    log_error_core(LOG_INFO, 0, "发送完整，已发送长度: [%d] 连接 [%d]", lp_conn->s_sendlen_already, lp_conn->fd);
                    msg_send = nullptr;
                    lp_conn->s_sendbuf = nullptr;
                    it = lp_socket->m_send_msgList.erase(it);
                    lp_conn->s_msgsendmem = nullptr;
                    --lp_socket->m_msgtosend_count;
                }
                pthread_mutex_unlock(&lp_conn->s_connmutex);  
            } // end for
        }  // end if(!empty())
        pthread_mutex_unlock(&lp_socket->m_sendmutex);
    }
    lp_thread->running = false;
    return (void*)(0);
}


/**
 * @brief 初始化连接池，每个进程都会有各自的连接池
 * 6.3：此函数不放在 Initialize_SubProc() 因为此函数需要 event_init 的 m_lplistenitem
 * @details 分配连接对象的内存时，6.3 改为每次分配一个对象的空间
 */
void CSocket::connectpool_init() {
    // 6.3 改动：灵活创建连接池 连接池不包含 lp_standby_connitem
    lp_connection_t lp_conn_alloc;
    for (int i = 0; i < m_create_connections_count; i++) {
        lp_conn_alloc = new connection_t();  // 对于有效连接，我们不传入参数，待 get_item 处理
        m_connectionList.push(lp_conn_alloc);
        m_free_connectionList.push(lp_conn_alloc);
    }
    m_connection_count = m_free_connection_count = m_create_connections_count;
    return;
}

/**
 * @brief 创建并初始化监听对象 init_worker_process 中调用
 */
void CSocket::event_init(int port_num, int port_value) {
    event_open_listen(port_num, port_value);  // 需要传入端口相关信息
    if (m_listenfd == -1) {
        log_error_core(LOG_ALERT, errno, "event_init failed at port_num: [%d], port: [%d]", port_num, port_value);
        exit(-1);
    }
    m_lplistenitem = new listening_t(m_listenfd, port_value);
    lp_standby_connitem = new connection_t(m_listenfd);
    lp_standby_connitem->rhandler = &CSocket::event_accept_handler;
    lp_standby_connitem->s_lplistening = m_lplistenitem;  // 作为待命连接所必须的
    m_lplistenitem->s_lpconnection = lp_standby_connitem;
    connectpool_init();
    return;
}


/**
 * @brief 此时连接池，监听对象结构体已经创建出来，我们需要（1）创建 epfd （2）创建 m_listenfd 对应的待命连接（3）加入监听
*/
void CSocket::epoll_init() {
    m_epfd = epoll_create(m_create_connections_count);
    if (m_epfd == -1) {
        log_error_core(LOG_ALERT, errno, "epoll_create failed");
        exit(-1);
    }
    epoll_oper_event(m_listenfd, EPOLL_CTL_ADD, EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR, 0, lp_standby_connitem);  // 待命连接
    return;
}


/**
 * @brief 参考《高性能编程》设置的是文件描述符的非阻塞属性，这会影响之后 recv 和 send 的行为
 * 对于非阻塞套接字，将要发生“阻塞”时会直接返回 EAGAIN|EWOULDBLOCK 见 man page
 * @param sockfd 
 * @return int 
 */
int CSocket::setnonblocking(int sockfd) {
    int old_option = fcntl(sockfd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, new_option) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting NONBLOCK has failed at [%s]", "setnonblocking");
        return -1;
    }
    return 0;
}

/**
 * @brief event_init 中调用 
 * @param port_num 端口序号
 * @param port_value 端口值
 * @return 返回绑定的 socket 句柄，出错返回 -1
*/
void CSocket::event_open_listen(int port_num, int port_value) {

    struct sockaddr_in serv_addr;  // serve_addr 要绑定到 connection_t 的 s_sockaddr 成员
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;                
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd == -1) {
        log_error_core(LOG_ALERT, errno, "Creating socket has failed");
        exit(-2);
    }
    int en_reuseaddr = 1;
    int en_reuseport = 1;

    // SO_REUSEADDR SO_REUSEPORT 是有所区别的，前者允许 TIME_WAIT 重用，后者允许多个绑定
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&en_reuseaddr, sizeof(int)) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting SO_REUSEADDR has failed at [%s]", "CSocket::event_open_listen");
        exit(-2);
    }
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, (const void*)&en_reuseport, sizeof(int))== -1) {
        log_error_core(LOG_ALERT, errno, "Setting SO_REUSEPORT has failed at [%s]", "CSocket::event_open_listen");
        exit(-2);
    }
    if (setnonblocking(m_listenfd) == -1) {
        log_error_core(LOG_ALERT, errno, "Setting NONBLOCKING has failed at [%s]", "CSocket::event_open_listen");
        exit(-2);
    }
    serv_addr.sin_port = htons((in_port_t)port_value);  // 转换为网络字节序

    if (bind(m_listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
        log_error_core(LOG_ALERT, errno, "Binding has failed at [%s]", "CSocket::event_open_listen");
        exit(-2);
    }
    if (listen(m_listenfd, LISTEN_BACKLOG) == -1) {
        log_error_core(LOG_ALERT, errno, "Listening has failed at [%s]", "CSocket::event_open_listen");
        exit(-2);            
    }
    return;
}

/**
 * @brief 关闭监听对象 m_lplistenitem 不处理连接池
 * 此函数被 CSocket::~CSocket() 调用
 * 守护进程的父进程退出时，CSocket 析构，m_lplistenitem == nullptr 不用关闭套接字
 * 每个工作线程的对应有一个 m_lplistenitem 用来处理新连接
 */
void CSocket::event_close_listen() {
    if (m_lplistenitem != nullptr) {
        int listenfd = m_lplistenitem->fd;
        if (listenfd > 0) {
            log_error_core(LOG_INFO, 0, "Closing listen port at [%d]", m_lplistenitem->port);
            close(listenfd);
        } else {
            log_error_core(LOG_INFO, 0, "试图关闭监听 fd，但 listen socket fd 已无效");
        } 
        delete m_lplistenitem;
    }
    
    return;
}

/**
 * @brief 
 * @param fd 
 * @param flag 修改类型 ADD MOD DEL
 * @param event_type 事件类型
 * @param bcaction 补充动作，当 EPOLL_CTL_MOD 时有效，0：增加 1：去掉 2：完全覆盖
 * @return int -1 表示出错
 */
int CSocket::epoll_oper_event(int fd, uint32_t flag, uint32_t event_type, int bcaction, lp_connection_t lp_conn) {
    struct epoll_event event;
    struct epoll_event* p_event = &event;
    memset(&event, 0, sizeof(event));
    if (flag == EPOLL_CTL_DEL) {
        p_event = NULL;
    } else if (flag == EPOLL_CTL_ADD) {
        event.events = event_type;
        lp_conn->events = event_type;
    } else {  // EPOLL_CTL_DEL
        event.events = lp_conn->events;
        if (bcaction == 0) {
            event.events |= event_type;
        } else if (bcaction == 1) {
            event.events &= ~event_type;
        } else {  // 例如 bcaction == 2
            event.events = event_type;
        }
    }
    event.data.ptr = lp_conn;
    if (epoll_ctl(m_epfd, flag, fd, p_event) == -1) {
        log_error_core(LOG_ALERT, errno, "CSocket::epoll_oper_event 中 epoll_ctl 出错");
        return -1;
    }
    return 0;
}


/**
 * @brief 将 fd 以 ET 模式添加监听
 * @param fd 可以是 listenfd 也可以是 connfd
 * @param event_type 可以是 ADD MOD DEL，并不是事件类型
 * @param lp_curconn 作为 ptr，后续取到 connection_t 调用其 handler，lp_curconn 可以是待命连接 or 一般连接
*/
// void CSocket::epoll_add_event(int fd, int readevent, int writeevent, uint32_t event_type, lp_connection_t lp_curconn) {
    
//     struct epoll_event event;
//     memset(&event, 0, sizeof(event));
//     event.data.ptr = lp_curconn;  // 注意：data 是 union 类型

//     if (readevent == 1) {
//         event.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLERR;  // EPOLLRDHUP 对端正常关闭，EPOLLERR 这是检测自己（服务端出错）
//     } else if (writeevent == 1) {  

//     } else {  // 其他监听事件类型

//     }
//     if (epoll_ctl(m_epfd, event_type, fd, &event) == -1) {
//         log_error_core(LOG_ALERT, errno, "epoll_ctl has failed at [%s]", "epoll_add_event");
//         exit(-1);
//     }
//     return;
// }


/**
 * @brief 循环调用 epoll_wait
 * @param timer 阻塞时间 -1: 无限阻塞
 * @return int -1 说明非正常返回，未来可拓展返回值的用途，此时外部未用到返回值
 */
int CSocket::epoll_process_events(int port_num, int port_value, int timer) {
    while (g_stopEvent == 0) {
        log_error_core(0, 0, "开始等待 epoll_wait 响应");
        int events_num = epoll_wait(m_epfd, m_events, MAX_EVENTS, timer);

        // 意外情况处理
        if (events_num == -1) {
            if (errno == EINTR) {
                log_error_core(LOG_INFO, errno, "epoll_wait has failed at [%s]", "CSocekt::ngx_epoll_process_events");
                return 0;  // 正常返回，再次 epoll_wait
            } else {
                log_error_core(LOG_ALERT, errno, "epoll_wait has failed at [%s]", "CSocekt::ngx_epoll_process_events");
                return -1;  // 非正常返回
            }
        }
        // 超时情况处理
        if (events_num == 0)  {
            if (timer != -1) 
                return 0;
            log_error_core(LOG_ALERT, 0, "epoll_wait wait indefinitely, but no event is returned at [%s]", "CSocekt::ngx_epoll_process_events");
            return -1;
        }
        log_error_core(0, 0, "epoll_wait 有响应事件，数量 events = [%d]", events_num);
        lp_connection_t lp_curconn;  // lp_curconn 有可能是待命连接，也可能是有效连接
        u_int32_t events;  
        
        for (int i = 0; i < events_num; i++) {
            lp_curconn = (lp_connection_t)m_events[i].data.ptr;  // 此时 lp_curconn->handler 已赋值完成
            
            // 说明没有取到连接
            if (lp_curconn == nullptr) {  
                log_error_core(LOG_ALERT, errno, "事件编号 events [%d], 没有取到有效连接 at [%s]", i, "epoll_process_events");
                continue;
            }
            int curfd = lp_curconn->fd;
            int cur_sequence = lp_curconn->s_cursequence;
            // 说明是已失效的连接对象
            // 若属于同一个连接对象的不同事件在 events 中，若前面的事件致使连接失效，则会出现失效连接
            if (lp_curconn->JudgeOutdate(cur_sequence) == false) {
                log_error_core(LOG_ERR, 0, "CSocket::epoll_process_events 当前监听事件失效 continue");
                continue;
            }
            events = m_events[i].events;  
            if (events & (EPOLLERR | EPOLLHUP)) {  // 出错，个人理解应当是关闭连接
                log_error_core(LOG_ALERT, 0, "EPOLLERR | EPOLLHUP，关闭连接: [%d]", curfd);
                close_accepted_connection(lp_curconn);
                continue;
            }
            if (events & EPOLLRDHUP) {  // 对方关闭连接
                log_error_core(0, 0, "对端已退出，关闭连接: [%d]", curfd);
                close_accepted_connection(lp_curconn);
                continue;
            }
            if (events & EPOLLIN) {
                log_error_core(LOG_ALERT, 0, "监听到可读事件，调用 rhandler");
                (this->*(lp_curconn->rhandler))(lp_curconn);
            }
            if (events & EPOLLOUT) {  // 发送缓冲区
                if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    close_accepted_connection(lp_curconn);
                    continue;
                }
                log_error_core(LOG_ALERT, 0, "监听到可写事件，调用 whandler");
                (this->*(lp_curconn->whandler))(lp_curconn);
            }
            // 其他类型事件...

        }  // end for
    }  // end while (g_stopEvent == 0)
    return -1;
}

int CSocket::ThreadRecvProc(char *msg) {
    return 0;
}


/**
 * @brief 处理收取消息的业务逻辑函数调用
 * @param msg_tosend 
 */
void CSocket::MsgSendInQueue(std::shared_ptr<char[]> msg_tosend) {
    int errnum = pthread_mutex_lock(&m_sendmutex);
    if (errnum != 0) {
        log_error_core(LOG_ERR, 0, "待发送消息入队列，加锁失败");
    }
    ++m_msgtosend_count;
    m_send_msgList.push_back(msg_tosend);  // 相当于是浅拷贝
    errnum = pthread_mutex_unlock(&m_sendmutex);
    if (errnum != 0) {
        log_error_core(LOG_ERR, 0, "待发送消息入队列，解锁失败");
    }
    if (sem_post(&m_sendsem) == -1) {
        log_error_core(LOG_ALERT, errno, "信号量设置出错");
        return;
    }
    return;
}