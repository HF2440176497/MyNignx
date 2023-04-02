
#ifndef GBLDEF_H
#define GBLDEF_H

#include "c_socket.h"

//结构定义
typedef struct {
    char itemname[30];
    char itemvalue[60];
}ConfItem, *LPConfItem;


//和运行日志相关 
typedef struct {
	int    log_level;   //日志级别 或者日志类型，ngx_macro.h里分0-8共9个级别
	int    fd;          //日志文件描述符
}log_t;

//外部全局量声明
extern size_t      g_arglen;
extern size_t      g_environlen;

extern int    g_argc;
extern char** g_argv;
extern char** g_init_argv;

extern char*  g_p_argmem;
extern char*  g_p_envmem; 

extern pid_t 	cur_pid;
extern pid_t 	parent_pid;

extern log_t 	log_s;  // 在 func_log.cxx 中定义的全局变量

extern int g_worker_process_num;  // 子进程数量

extern CSocket* p_socket;

#endif
