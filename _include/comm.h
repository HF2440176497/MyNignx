
#ifndef COMM_H
#define COMM_H

#include <sys/epoll.h> // epoll_event 等结构体
#include <sys/socket.h>
#include <cstdint>
#include <memory>
#include <atomic>

// 收包状态定义 对应 connection_t 中的 m_curstat
// 这些宏与类声明 or 结构体关系紧密，放在一个文件中

#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态

#define _PKG_MAX_LENGTH      30000  // 包头中包长度的最大值

#define LEN_STRUCT_REGISTER  sizeof(STRUCT_REGISTER)
#define LEN_STRUCT_LOGIN     sizeof(STRUCT_LOGIN)
#define LEN_STRUCT_PING      0

class CSocket;

typedef struct listening_s  listening_t, *lp_listening_t;
typedef struct connection_s connection_t, *lp_connection_t;
typedef void (CSocket::*lp_event_handler)(lp_connection_t);  // 成员函数指针

#pragma pack(1)
typedef struct _STRUC_MSG_HEADER {
    lp_connection_t lp_curconn;       // 指向发包的连接对象
    uint64_t        msg_cursequence;  // 记录收到包时 lp_curcon 后续连接可能会被回收，用于检验 lp_curconn
} STRUC_MSG_HEADER, *LPSTRUC_MSG_HEADER;
#pragma pack()

#pragma pack(1)
typedef struct _COMM_PKG_HEADER {
    uint16_t        pkgLen;  // 两字节无符号整型，最大可表示 2^16
    uint16_t        msgCode;
    int             crc32;
}COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;
#pragma pack()


#pragma pack (1)
typedef struct _STRUCT_REGISTER {
    int  iType;         // 类型
    char username[56];  // 用户名
    char password[40];  // 密码
} STRUCT_REGISTER, *LPSTRUCT_REGISTER;
#pragma pack()

#pragma pack(1)
typedef struct _STRUCT_LOGIN {
    char username[56];  // 用户名
    char password[40];  // 密码
} STRUCT_LOGIN, *LPSTRUCT_LOGIN;
#pragma pack()

struct listening_s {
public:
    listening_s();
    listening_s(int listenfd, int port_value);

public:
    int             fd;
    int             port;
    lp_connection_t p_connitem;  // 指向待命连接
};

// 连接对象
struct connection_s {
public:
    connection_s(int);
    virtual ~connection_s();
    void GetOneToUse();
    void PutOneToFree();
    void PutToStateMach();
    bool JudgeOutdate(int sequence);  // 判断连接是否过期

public:
    // 连接信息相关
    int               fd;
    struct sockaddr   s_sockaddr;     // 这里用 sockaddr 类型
    lp_event_handler  rhandler;       // 可读时的函数句柄
    lp_event_handler  whandler;       // 可写时的函数句柄
    uint64_t          sequence;  // 序号，标记取用次数
    uint32_t          events;         // 记录 epoll 监听事件类型

    // 收取状态机相关
    u_char                  s_curstat;     // 表示收包状态
    LPCOMM_PKG_HEADER       p_headerinfo;  // 指向包头结构体，初始化时应当 nullptr

    std::shared_ptr<char> p_msgrecv;          // 传入 的智能指针，指向分配的内存
    char*                 recv_str;           // 指向分配的内存，用于构造队列中的 shared_ptr
    char*                 p_recvbuf;          // 接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
    size_t                s_recvlen;          // 要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码
    size_t                s_recvlen_already;  // 每次发完一个包，用于最后校验收到每个包的长度
    // 发送消息相关 
    // 对于发送线程，待发送消息只被 m_send_msgList 的指针指向，且并不是开始时分配内存得到的指针
    // 因此释放时根据发送线程的逻辑有序置空即可
    std::shared_ptr<char>   p_msgsend;          // 从发送队列中获得
    char*                   send_str;           // 指向分配的内存 由 p_msgsend 获得
    
    std::atomic_int         iThrowsendCount;    // 标记发送缓冲区满，则需要通过epoll事件来驱动消息的继续发送
    char*                   p_sendbuf;          // 当前需要发送的消息指向
    size_t                  s_sendlen;          // 当前需要发送的消息长度
    size_t                  s_sendlen_suppose;  // 连接对象应当发送的长度，各连接暂时都为相同值
    size_t                  s_sendlen_already;  // 连接对象已发送长度
    std::atomic_int         isend_count;        // 连接对象对应的发送数目

    lp_listening_t          p_listenitem;      // 始终指向监听结构体 m_lplistenitem
    pthread_mutex_t         s_connmutex;        // 连接对象互斥量

    time_t                  s_inrevy_time;      // 当前连接进入延迟回收队列的时间
    int                     s_inrecyList;       // 表示是否已经在延迟回收队列中，0 表示不在，1 表示在

    time_t                  ping_update_time;   // 心跳包更新的时间
    bool                    istimeout;          // 是否被超时踢出
};

#endif