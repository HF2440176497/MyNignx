

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
    // 处理连接回收的线程 
    ThreadItem* lpthread_item = new ThreadItem(this);  // 非线程池的工作线程，而是 socket 的所属线程
    m_threadVector.push_back(lpthread_item);
    int errnum = pthread_create(&(lpthread_item->_handle), NULL, &CSocket::RecyConnThreadFunc, lpthread_item);  // 创建线程，错误不返回到errno，一般返回错误码
    if (errnum != 0) {
        log_error_core(LOG_ALERT, 0, "CSocket::Initialize_SubProc 创建[延迟回收线程]失败，返回的错误码为 [%d]", errnum);
        exit(-2);
    }

    // 处理其他事物的线程

    return;
}


void CSocket::connectpool_clean() {
    log_error_core(LOG_INFO, 0, "CSocket::connectpool_clean");
    lp_connection_t lp_conn;
    while (!m_connectionList.empty()) {
        lp_conn = m_connectionList.front();
        m_connectionList.pop_front();
        delete lp_conn;
    }
    m_connectionList.clear();  // 删除所有元素
    return;
}

void CSocket::Shutdown_SubProc() {
    log_error_core(LOG_INFO, 0, "子线程退出");
    connectpool_clean();
    for (auto lp_thread:m_threadVector) {
        lp_thread->ifshutdown = true;
    }
wait:
    for (auto lp_thread:m_threadVector) {
        if (lp_thread->running == true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            goto wait;
        }
        pthread_join(lp_thread->_handle, NULL);  // 所有线程已停止运行后，都会调用 join，不再跳转 
    }
    for (auto lp_thread:m_threadVector) {
        delete lp_thread;                       // 所有的线程对象在 Initialize_SubProc 分配
    }
    m_threadVector.clear();

    // 互斥量与信号量
    pthread_mutex_destroy(&m_socketmutex);
    pthread_mutex_destroy(&m_recymutex);
    return;
}


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
    while (1) {  
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
                    lp_conn->PutOneToFree();  // PutOneToFree() 已设置互斥量
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
                    lp_conn->PutOneToFree();
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
 * @brief 
 * 何处调用：close_accepted_connection
 * @param lp_conn
 */
void CSocket::InRecyConnQueue(lp_connection_t lp_conn) {
    if (lp_conn->s_inrecyList == 1) {
        log_error_core(LOG_INFO, 0, "当前连接已经位于延迟回收队列中");
        return;
    }
    CLock lock(&m_recymutex);
    lp_conn->s_inrecyList = 1;  // 标记进入回收队列
    lp_conn->s_inrevy_time = time(NULL);
    m_recy_connectionList.push_back(lp_conn);
    return;
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
        m_connectionList.push_back(lp_conn_alloc);
        m_free_connectionList.push_back(lp_conn_alloc);
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
    epoll_oper_event(m_listenfd, EPOLL_CTL_ADD, EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLERR, 0, lp_standby_connitem);
    return;
}


/**
 * @brief 参考《高性能编程》
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
    // 

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
 * @param event_type 事件类型 与 flag 相关
 * @param bcaction 
 * @param pconn 
 * @return int 
 */
int CSocket::epoll_oper_event(int fd, uint32_t flag, uint32_t event_type, int bcaction, lp_connection_t lp_conn) {
    struct epoll_event event;
    struct epoll_event* p_event = &event;
    memset(&event, 0, sizeof(event));

    if (flag == EPOLL_CTL_ADD || EPOLL_CTL_MOD) {
        event.data.ptr = lp_conn;
        event.events = event_type;
        lp_conn->events = event_type;
    } else {  // EPOLL_CTL_DEL
        p_event = NULL;
    }
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
 * @brief 调用 epoll_wait
 * @param timer 阻塞时间 -1: 无限阻塞
 * @details 此函数在 worker_process_cycle 中调用，且一直循环，此函数不再循环调用 epoll_wait
 * @return int -1 说明非正常返回，在 worker_process_cycle 中退出；0 说明正常返回，在 worker_process_cycle 中再次循环
 */
int CSocket::epoll_process_events(int port_num, int port_value, int timer) {
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

        // 有没有可读且可写的情况？
        if (events & EPOLLOUT) {
            log_error_core(LOG_ALERT, 0, "监听到可写事件，调用 whandler");
        }
        // 其他类型事件...
    }
    log_error_core(LOG_STDERR, 0, "离开 events 的 for 循环，epoll_process_events 结束");  // 此时说明 handler 正常返回，可以继续监听
    return 0;
}

/**
 * @brief 6.3 更新：延迟回收机制 以异步的方式交给线程去处理
 * 需要判断，lp_conn 是否已经在 InRecyQueue 中，是的话不需要再添加进去了
 * @param lp_curconn 此时传入的 lp_curconn != nullptr 调用之后 lp_curconn 重新回到初始化状态
 */
void CSocket::close_accepted_connection(lp_connection_t lp_conn) {
    log_error_core(0, 0, "关闭了 connfd = [%d]，开始放入回收队列", lp_conn->fd);
    int fd_toclose = lp_conn->fd;
    epoll_oper_event(fd_toclose, EPOLL_CTL_DEL, 0, 0, lp_conn);
    if (close(fd_toclose) == -1) {
        log_error_core(LOG_ALERT, errno, "Closeing fd of current conn_t has failed at [%s]", "CSocket::close_accepted_connection");
        exit(-1);
    }
    InRecyConnQueue(lp_conn);
    return;
}

int CSocket::ThreadRecvProc(char *msg) {
    return 0;
}