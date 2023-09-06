

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
    m_port_count               = 2;
    m_create_connections_count = 20;
    m_delay_time               = 60;  // 延迟回收时间
    m_waittime                 = 10;  // 监视检查间隔时间

    m_total_connection_count         = 0;
    m_free_connection_count    = 0;
    m_online_count             = 0;
    m_worker_connections       = 1000;  // 默认允许最大连入数 1000

    m_recy_connection_count    = 0;
    m_msgtosend_count          = 0;

    m_timermap_size            = 0;
    m_timer_value              = 0;
    m_lastprinttime            = 0;
    return;
}

CSocket::~CSocket() {
    event_close_listen();
    // 后续补充。。。

    return;
}

void CSocket::ReadConf() {
    CConfig* p_config          = CConfig::GetInstance();
    m_worker_connections       = p_config->GetInt("worker_connections", m_worker_connections);
    m_port_count               = p_config->GetInt("ListenPortCount", m_port_count);
    m_create_connections_count = p_config->GetInt("ConnectionsToCreate", m_create_connections_count);
    m_delay_time               = p_config->GetInt("Sock_RecyConnectionWaitTime", m_delay_time);

    m_TimeEnable               = p_config->GetInt("Sock_WaitTimeEnable", 1);
    if (m_TimeEnable == 1) {
        m_waittime                 = p_config->GetInt("Sock_MaxWaitTime", m_waittime);
        m_waittime                 = (m_waittime > 5) ? m_waittime : 5;
    }
    return;
}

/**
 * @brief g_socket 有关的初始化工作，在父进程初始化中调用
 * 需要完成：配置文件读取
 * 参考代码还调用 event_open_listen 打开监听端口 我们并没有
 * 
 */
bool CSocket::Initialize() {
    ReadConf();
    if (open_listen_sockets() == false)
        return false;
    return true;
}


bool CSocket::open_listen_sockets() {
    struct sockaddr_in serv_addr;  // serve_addr 要绑定到 connection_t 的 s_sockaddr 成员
    int                isock;
    int                iport;
     
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;                
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    CConfig* p_config = CConfig::GetInstance();
    for (int i = 0; i < m_port_count; i++) {
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if (isock == -1) {
            log_error_core(LOG_ALERT, errno, "Creating socket has failed");
            return false;
        }
        int en_reuseaddr = 1;
        int en_reuseport = 1;
        // SO_REUSEADDR SO_REUSEPORT 是有所区别的，前者允许 TIME_WAIT 重用，后者允许多个绑定
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*)&en_reuseaddr, sizeof(int)) == -1) {
            log_error_core(LOG_ALERT, errno, "Setting SO_REUSEADDR has failed at [%s]", "CSocket::event_open_listen");
            close(isock);
            return false;
        }
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT, (const void*)&en_reuseport, sizeof(int))== -1) {
            log_error_core(LOG_ALERT, errno, "Setting SO_REUSEPORT has failed at [%s]", "CSocket::event_open_listen");
            close(isock);
            return false;
        }
        if (setnonblocking(isock) == -1) {
            log_error_core(LOG_ALERT, errno, "Setting NONBLOCKING has failed at [%s]", "CSocket::event_open_listen");
            close(isock);
            return false;
        }
        char strinfo[100]; strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetInt(strinfo, (9000 + i));
        serv_addr.sin_port = htons((in_port_t)iport);   //in_port_t其实就是uint16_t

        if (bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
            log_error_core(LOG_ALERT, errno, "Binding has failed at [%s]", "CSocket::event_open_listen");
            close(isock);
            return false;
        }
        if (listen(isock, LISTEN_BACKLOG) == -1) {
            log_error_core(LOG_ALERT, errno, "Listening has failed at [%s]", "CSocket::event_open_listen");
            close(isock);
            return false;          
        }
        lp_listening_t p_listenitem = new listening_t();
        p_listenitem->fd = isock;
        p_listenitem->port = iport;

        log_error_core(LOG_INFO, 0, "监听 [%d] 端口成功", iport);
        m_ListenSocketList.push_back(p_listenitem);
    }
    if (m_ListenSocketList.size() <= 0)
        return false;
    return true; 
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
    if (pthread_mutex_init(&m_recymutex, NULL) != 0) {
        log_error_core(LOG_ALERT, errno, "m_sendmutex 初始化失败");
        exit(-1);
    }
    if (pthread_mutex_init(&m_timer_mutex, NULL) != 0) {
        log_error_core(LOG_ALERT, 0, "m_timermutex 初始化失败");
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
    // 超时处理线程
    if (m_TimeEnable == 1) {  // 开启计时监视机制
        ThreadItem* lp_timerthread = new ThreadItem(this);
        m_threadVector.push_back(lp_timerthread);
        errnum = pthread_create(&(lp_timerthread->_handle), NULL, &CSocket::ServerTimerQueueThreadFunc, lp_timerthread);
        if (errnum != 0) {
            log_error_core(LOG_ALERT, 0, "创建监视线程失败，返回的错误码为 [%d]", errnum);
            exit(-1);
        }
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
    std::shared_ptr<char> msg_toclean;
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
    for (auto lp_thread:m_threadVector) {
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
    timermap_clean();

    // 互斥量与信号量
    pthread_mutex_destroy(&m_socketmutex);
    pthread_mutex_destroy(&m_recymutex);
    pthread_mutex_destroy(&m_sendmutex);
    pthread_mutex_destroy(&m_timer_mutex);
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
        if (lp_socket->m_recy_connection_count > 0) {
            errnum = pthread_mutex_lock(&lp_socket->m_recymutex);
            if (errnum != 0) {
                log_error_core(LOG_INFO, 0, "延迟回收线程，加锁失败 at RecyConnThreadFunc");
                break;
            }
            if (g_stopEvent == 1 || lp_thread->ifshutdown == true) {  // 进程退出或是回收线程需要退出
                while (!m_recy_connectionList.empty()) {
                    for (auto it = m_recy_connectionList.begin(); it != m_recy_connectionList.end();) {
                        lp_connection_t lp_conn = *it;
                        it = m_recy_connectionList.erase(it);  // list 作为链表，erase 之后其余迭代器应当
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
            if (lp_thread->running == false) {
                lp_thread->running = true;
            }
            if (!m_recy_connectionList.empty()) {  // 进程非退出，且队列非空
                for (auto it = m_recy_connectionList.begin(); it != m_recy_connectionList.end();) {
                    cur_time = time(NULL);
                    lp_connection_t lp_conn = *it;
                    if ((lp_socket->m_delay_time + lp_conn->inrevy_time) < cur_time && g_stopEvent == 0) {  // 说明此时满足时间要求
                        // log_error_core(LOG_INFO, 0, "RecyConnThreadFunc 执行，地址：[%d] 连接对象被归还", lp_conn);
                        it = m_recy_connectionList.erase(it);
                        --lp_socket->m_recy_connection_count;
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
        }  // end if (m_recy_connection_count > 0)
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
    if (lp_conn->inrecyList == 1) {
        return;
    }
    bool iffind = false;
    CLock lock(&m_recymutex);
    for (auto it = m_recy_connectionList.begin(); it != m_recy_connectionList.end(); ++it) {
        if ((*it) == lp_conn) {
            iffind = true;
            break;
        }
    }
    if (iffind == true) {
        return;
    }
    lp_conn->inrecyList  = 1;  // 标记进入回收队列
    lp_conn->inrevy_time = time(NULL);
    ++m_recy_connection_count;
    m_recy_connectionList.push_back(lp_conn);
    return;
}

/**
 * @brief 发送消息线程的入口函数
 * 对方关闭连接，连接对应的消息发送失败，但此线程不负责调用
 * @param lp_item 
 * @return void* 
 */
void* CSocket::SendMsgThreadFunc(void* lp_item) {
    log_error_core(LOG_INFO, 0, "发送消息线程开始运行。。。");
    ThreadItem* lp_thread = static_cast<ThreadItem*>(lp_item);
    CSocket* lp_socket = lp_thread->lp_socket;
    int errnum;
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
        if (lp_socket->m_msgtosend_count > 0) {  // 原子变量
            errnum = pthread_mutex_lock(&lp_socket->m_sendmutex);  // 保护发送队列
            if (errnum != 0) {
                log_error_core(LOG_ERR, 0, "SendMsgThreadFunc 发送队列加锁失败");
                break;
            }
            if (!lp_socket->m_send_msgList.empty()) {
                for (auto it = lp_socket->m_send_msgList.begin(); it != lp_socket->m_send_msgList.end(); ) {
                    // 未发送完整的连接对象，也不会在队列中移除，因此只要未在队列中移除，那么就可以取到消息头、包头
                    char* msg_send = it->get();  // 首先获得容器元素，再获得指向对象 小心使用
                    LPSTRUC_MSG_HEADER lp_msghead_tosend = (LPSTRUC_MSG_HEADER)(msg_send);
                    LPCOMM_PKG_HEADER lp_pkghead_tosend = (LPCOMM_PKG_HEADER)(msg_send + MSG_HEADER_LEN);
                    lp_connection_t lp_conn = lp_msghead_tosend->lp_curconn;
                    msg_send = nullptr;

                    if (lp_conn->JudgeOutdate(lp_msghead_tosend->msg_cursequence) == false) {    
                        lp_conn->send_str = nullptr;
                        lp_conn->p_sendbuf = nullptr;
                        lp_conn->p_msgsend = nullptr;
                        log_error_core(LOG_INFO, 0, "SendMsgThreadFunc 连接失效");
                        it = lp_socket->m_send_msgList.erase(it);
                        continue;
                    }
                    if (lp_conn->iThrowsendCount > 0) {  // 重点：保证对应连接按顺序发送
                        ++it;
                        continue;
                    }
                    lp_conn->p_msgsend = *it;  // 将 shared_ptr 保存下来，此时放心删除迭代器，it 获取的成员仍然有效
                    lp_conn->send_str = lp_conn->p_msgsend.get();
                    it = lp_socket->m_send_msgList.erase(it);
                    --lp_conn->isend_count;
                    --lp_socket->m_msgtosend_count;
        
                    lp_conn->sendlen_already = 0;
                    lp_conn->p_sendbuf = lp_conn->send_str + MSG_HEADER_LEN;
                    lp_conn->sendlen = ntohs(lp_pkghead_tosend->pkgLen);  // 发送的消息已设置为网络字节序，这里需要转换回来
                
                    suppose_size = lp_conn->sendlen;
                    int real_size = lp_socket->sendproc(lp_conn, lp_conn->p_sendbuf, lp_conn->sendlen);
                    if (real_size == -1) {  // 发送出错，移除此消息
                        lp_conn->send_str = nullptr;
                        lp_conn->p_sendbuf = nullptr;
                        lp_conn->p_msgsend = nullptr;
                        lp_conn->iThrowsendCount = 0;
                        continue;
                    } else if (real_size < suppose_size) {  // 实际长度小于期望发送长度 或者缓冲区已满 real_size == 0
                        ++lp_conn->iThrowsendCount;
                        lp_conn->p_sendbuf += real_size;
                        lp_conn->sendlen -= real_size;
                        lp_conn->sendlen_already += real_size;
                        lp_socket->epoll_oper_event(lp_conn->fd, EPOLL_CTL_MOD, EPOLLOUT, 0, lp_conn);  // 增加 EPOLLOUT 监听
                        // 当前连接的发送未发送完，遍历到同连接的新消息，到此连接时 s_continuesend == 1，sendbuf 仍指向先前消息
                        // 直到发送完原消息，更新对应连接 sendbuf，不会导致一个连接的发送被拆分

                    } else {
                        // 06.04 更新：这里的判断是合理的，走到这里的肯定是发送线程全权负责的发送
                        lp_conn->sendlen_already += real_size;
                        if (lp_conn->sendlen_already != lp_conn->sendlen_suppose) {  // 额外检验是否和一开始要发送的长度相等
                            log_error_core(LOG_ALERT, 0, "发送长度不符 already: [%d] suppose: [%d]", lp_conn->sendlen_already, lp_conn->sendlen_suppose);
                            return (void*)(0);
                        }
                        // log_error_core(LOG_INFO, 0, "发送成功，已发送长度 [%d]", lp_conn->sendlen_already);
                        lp_conn->send_str = nullptr;
                        lp_conn->p_sendbuf = nullptr;
                        lp_conn->p_msgsend = nullptr;
                        lp_conn->iThrowsendCount = 0;  // 标记不需要 epoll 驱动机制
                    }
                } // end for
            }  // end if(!empty())
            pthread_mutex_unlock(&lp_socket->m_sendmutex);        
        }  // end if (m_count > 0)
    }  // end while (g_stopEvent == 0)
    lp_thread->running = false;
    return (void*)(0);
}


/**
 * @brief 初始化连接池 创建足够的连接对象
 * @details 分配连接对象的内存时，6.3 改为每次分配一个对象的空间
 */
void CSocket::connectpool_init() {
    lp_connection_t lp_conn_alloc;
    for (int i = 0; i < m_create_connections_count; i++) {
        lp_conn_alloc = new connection_t(-1);
        m_connectionList.push(lp_conn_alloc);
        m_free_connectionList.push(lp_conn_alloc);
    }
    m_total_connection_count = m_free_connection_count = m_create_connections_count;
    return;
}

/**
 * @brief 创建并初始化监听对象 init_worker_process 中调用
 */
void CSocket::epoll_event_init() {
    m_epfd = epoll_create(m_create_connections_count);
    if (m_epfd == -1) {
        log_error_core(LOG_ALERT, errno, "epoll_create failed");
        exit(-1);
    }
    connectpool_init();
    for (auto pos: m_ListenSocketList) {  // 每一个监听对象分配一个待命连接
        lp_connection_t p_newconn = get_connection_item();
        if (p_newconn == nullptr) {
            log_error_core(LOG_ALERT, 0, "epoll_event_init p_newconn 出错");
            exit(-1);
        }
        p_newconn->fd = pos->fd;
        p_newconn->p_listenitem = pos;
        pos->p_connitem = p_newconn;

        p_newconn->rhandler = &CSocket::event_accept_handler;
        if (epoll_oper_event(pos->fd, EPOLL_CTL_ADD, EPOLLIN|EPOLLRDHUP, 0, p_newconn) == -1) {
            exit(-1);
        }
    }
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


void CSocket::event_close_listen() {
    std::vector<lp_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) {		
		delete (*pos);
	}  // end for
	m_ListenSocketList.clear();    
    return;
}

/**
 * @brief 设置相关的
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
    } else {  // EPOLL_CTL_MOD
        event.events = lp_conn->events;
        if (bcaction == 0) {
            event.events |= event_type;
        } else if (bcaction == 1) {
            event.events &= ~event_type;
        } else {  // 例如 bcaction == 2
            event.events = event_type;
        }
        lp_conn->events = event.events;  // 修改之后记录
    }
    event.data.ptr = (void*)lp_conn;
    if (epoll_ctl(m_epfd, flag, fd, p_event) == -1) {
        log_error_core(LOG_ALERT, errno, "CSocket::epoll_oper_event 中 epoll_ctl 出错");
        return -1;
    }
    return 0;
}


/**
 * @brief 外部是循环调用，因此内部无需 while
 * @param timer epoll_wait 阻塞等待的时间
 * @return int 
 */
int CSocket::epoll_process_events(int timer) {
    int events_num = epoll_wait(m_epfd, m_events, MAX_EVENTS, timer);
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
    if (events_num == 0) {
        if (timer != -1) 
            return 0;  // 阻塞到时间了，正常返回
        log_error_core(LOG_ALERT, 0, "epoll_wait wait indefinitely, but no event is returned at [%s]", "CSocekt::ngx_epoll_process_events");
        return -1;
    }
    lp_connection_t p_conn;  // lp_curconn 有可能是待命连接，也可能是有效连接
    u_int32_t events;  

    for (int i = 0; i < events_num; i++) {
        p_conn = (lp_connection_t)m_events[i].data.ptr;  // 此时 lp_curconn->handler 已赋值完成
        if (p_conn == nullptr) {  
            log_error_core(LOG_ALERT, errno, "事件编号 events [%d], 没有取到有效连接 at [%s]", i, "epoll_process_events");
            continue;
        }
        int curfd = p_conn->fd;
        int cur_sequence = p_conn->sequence;
        // 若属于同一个连接对象的不同事件在 events 中，若前面的事件致使连接失效，则会出现失效连接
        // 可以防止超时踢出机制此时关闭了连接
        // 
        // 对于刚开始在 epoll_event_init 中的 handler 为 accept 的待命连接，fd 未设置，不可以进行此判断，因此注释掉
        // if (p_conn->JudgeOutdate(cur_sequence) == false) {
        //     log_error_core(LOG_INFO, 0, "epoll_process_events JudgeOutdate");
        //     continue;
        // }
        // else {
        //     pthread_mutex_lock(&lp_curconn->conn_mutex);
        // }
        // if (lp_curconn->JudgeOutdate(cur_sequence) == false) {
        //     log_error_core(LOG_INFO, 0, "epoll_process_events JudgeOutdate");
        //     pthread_mutex_unlock(&lp_curconn->conn_mutex);
        //     continue;
        // }
        // pthread_mutex_unlock(&lp_curconn->conn_mutex);
        // 开始事件对应类型的处理
        events = m_events[i].events;  

        if (events & EPOLLIN) {  // 包含对端关闭的处理
            (this->*(p_conn->rhandler))(p_conn);
        }
        if (events & EPOLLOUT) {  // 发送缓冲区
            if (events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {  // 发送线程只负责对发送消息队列中发送，对于过期消息，不负责关闭连接
                log_error_core(LOG_INFO, 0 , "可写事件时 EPOLLERR | EPOLLHUP | EPOLLRDHUP");
                --p_conn->iThrowsendCount;
                close_accepted_connection(p_conn, false);
                continue;
            }
            (this->*(p_conn->whandler))(p_conn);
        }
        // 其他类型事件...

    }  // end for
    return 1;
}


int CSocket::ThreadRecvProc(char *msg) {
    return 0;
}


/**
 * @brief 处理收取消息的业务逻辑函数调用
 * @param msg_tosend 完整消息
 */
void CSocket::MsgSendInQueue(std::shared_ptr<char> msg_tosend) {
    int errnum = pthread_mutex_lock(&m_sendmutex);
    if (errnum != 0) {
        log_error_core(LOG_ERR, 0, "待发送消息入队列，加锁失败");
    }

    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)(msg_tosend.get());
	lp_connection_t p_conn = pMsgHeader->lp_curconn;
    
    if (m_msgtosend_count > 1000) {
        log_error_core(LOG_INFO, 0, "发送队列消息积压太多，丢弃消息", p_conn->fd);
        return;
    }
    if (p_conn->isend_count > 400) {
        log_error_core(LOG_INFO, 0, "连接[%d]对应的消息积压太多，丢弃消息", p_conn->fd);
        return;
    }
    m_send_msgList.push_back(msg_tosend);  // 相当于是浅拷贝
    errnum = pthread_mutex_unlock(&m_sendmutex);
    if (errnum != 0) {
        log_error_core(LOG_ERR, 0, "待发送消息入队列，解锁失败");
    }
    ++p_conn->isend_count;
    ++m_msgtosend_count;
    
    if (sem_post(&m_sendsem) == -1) {
        log_error_core(LOG_ALERT, errno, "MsgSendInQueue 信号量设置出错");
        return;
    }
    return;
}

/**
 * @brief 压力测试所用的打印函数
 */
void CSocket::PrintInfo() {
    time_t currtime = time(NULL);
    if ((currtime - m_lastprinttime) > 10) {
        m_lastprinttime = currtime;
        int online_count = m_online_count;
        int send_count = m_msgtosend_count;
        int runningthread = g_threadpool.m_iRunningThread;
        log_error_core(LOG_INFO, 0, "------------------------------------ begin ------------------------------------");
        log_error_core(LOG_INFO, 0, "连接池中空闲连接/总连接/要释放的连接 [%d] / [%d] / [%d]", 
                                    m_free_connectionList.size(), m_connectionList.size(), m_recy_connection_count);
        log_error_core(LOG_INFO, 0, "当前在线用户数 [%d] / 最大允许在线数 [%d]", online_count, m_worker_connections);
        log_error_core(LOG_INFO, 0, "线程池运行线程 [%d]", runningthread);
        log_error_core(LOG_INFO, 0, "收取消息队列大小 [%d]", g_threadpool.m_msgqueue_size);
        log_error_core(LOG_INFO, 0, "发送消息队列大小 [%d]", send_count);
        if (m_TimeEnable == 1) {
            log_error_core(LOG_INFO, 0, "已开启心跳包机制，计时队列大小 [%d]", m_timermap_size);
        }
        log_error_core(LOG_INFO, 0, "------------------------------------  end  ------------------------------------");
    }
    return;
}