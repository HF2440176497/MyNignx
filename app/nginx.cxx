
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include "c_conf.h"
#include "func.h"


int    g_argc = 0;
char** g_argv = NULL;     // 指向新分配内存中保存环境变量的各处，类似于 environ[i]
char** g_init_argv = NULL;// 指向原环境变量的所在处

char* g_p_envmem = NULL;  // 指向开辟内存的首地址，保存的环境变量的首地址
int   g_environlen = 0;   // 为环境变量开辟的内存空间大小

char* g_p_argmem = NULL;  // 指向开辟内存的首地址
int   g_arglen = 0;       // 为main参数开辟的内存空间大小
int*  g_p_arglen = NULL;  // 保存各参数的长度的数组，分配argc*int空间

pid_t cur_pid;  // 当前进程的pid

CConfig* p_config;  // 配置文件相关的全局变量

int main(int argc, char *const *argv) {

    
    g_argc = argc;
    g_init_argv = (char**)argv;

    int exitcode = 0;

    log_init();
    cur_pid = getpid();

    CConfig* p_config = CConfig::GetInstance();
    const char* conffile = "nginx.conf";

    if (p_config->Load(conffile) == false) {
        log_error_core(0, 0, "Configuration file [%s] loading failed, Exit", conffile);  // errnum == 0 
        exitcode = 2;  // 标记找不到文件
        // goto normexit;
    }
        
  
    p_config->Test();

    init_setproctitle();
    setproctitle("MyNginx");

    
    // 日志系统 测试
    int level = 0; int errnum = 7;
    const char* fmt = "测试输出 print a num ：%d";
    log_error_core(1, errnum, fmt, 1000);

    // 理想输出：日期  PID: 线程ID  [错误等级]:8  错误信息

    // for(;;) {
    //    sleep(1); //休息1秒
    //    printf("休息1秒\n");
    // }

normexit:
    close(log_s.fd);
    return exitcode;
}
