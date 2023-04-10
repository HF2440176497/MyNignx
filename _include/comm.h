
#ifndef COMM_H
#define COMM_H

#include <cstdint>
// 收包状态定义 对应 connection_t 中的 m_curstat
// 这些宏与类声明 or 结构体关系紧密，放在一个文件中

#define _PKG_HD_INIT         0  //初始状态，准备接收数据包头
#define _PKG_HD_RECVING      1  //接收包头中，包头不完整，继续接收中
#define _PKG_BD_INIT         2  //包头刚好收完，准备接收包体
#define _PKG_BD_RECVING      3  //接收包体中，包体不完整，继续接收中，处理后直接回到_PKG_HD_INIT状态

#define _PKG_MAX_LENGTH      30000  // 包头中包长度的最大值

#pragma pack (1)

typedef struct _COMM_PKG_HEADER {
    uint16_t        pkgLen;  // 两字节无符号整型，最大可表示 2^16 
    uint16_t        msgCode;
    int             crc32;
}COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;

#pragma pack()

#endif