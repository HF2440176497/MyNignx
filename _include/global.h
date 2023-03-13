
#ifndef GLOBAL_H
#define GLOBAL_H

// 此头文件用来保存通用的定义

// 配置项相关结构体
typedef struct {
    char itemname[30];
    char itemvalue[60];
}ConfItem, *LPConfItem;


// 日志相关结构体
typedef struct {
    int log_level;  // 日志级别
    int fd;         // 日志文件描述符
}log_t;


// 所有源文件的全局变量
extern int g_argc;
extern char** g_argv;
extern char** g_init_argv;

extern char* g_p_envmem;  // 指向手动开辟内存，保存的环境变量的首地址
extern int g_environlen;  // 为环境变量开辟的内存空间大小

extern char* g_p_argmem;
extern int g_arglen;     // 为命令行参数开辟的内存空间总大小
extern int* g_p_arglen;  // 保存各参数的长度的数组，分配argc*int空间



#endif