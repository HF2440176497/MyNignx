
#ifndef COMM_H
#define COMM_H

#include <sys/epoll.h> // epoll_event 等结构体
#include <sys/socket.h>
#include <cstdint>

// 收包状态定义 对应 connection_t 中的 m_curstat
// 这些宏与类声明 or 结构体关系紧密，放在一个文件中

#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态

#define _PKG_MAX_LENGTH      30000  // 包头中包长度的最大值

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
    int             fd;
    int             port;
    lp_connection_t s_lpconnection;
};

// 连接对象
struct connection_s {
    int               fd;
    uint64_t          s_cursequence;  // 序号，标记取用次数，可以经过几次取用，get_item 中 ++
    lp_event_handler  rhandler;       // 读操作时的函数句柄
    lp_connection_t   next;

    u_char            s_curstat;      // 表示收包状态
    LPCOMM_PKG_HEADER s_headerinfo;   // 指向包头结构体，初始化时应当 nullptr
    char*             s_msgmem;       // 指向为整个消息开辟的内存，待传入消息队列
    char*             s_precvbuf;     // 接收数据的缓冲区的头指针，对收到不全的包非常有用，看具体应用的代码
    unsigned int      s_recvlen;      // 要收到多少数据，由这个变量指定，和precvbuf配套使用，看具体应用的代码

    struct sockaddr   s_sockaddr;     // 这里用 sockaddr 类型
    lp_listening_t    s_lplistening;  // 始终指向被监听对象 m_lplistenitem

    // 以下成员来自自己提出的包缓冲机制
};

#endif