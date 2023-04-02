
//整个程序入口函数放这里

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "macro.h"
#include "c_conf.h"  //和配置文件处理相关的类,名字带c_表示和类有关
#include "c_socket.h"
#include "func.h"    //各种函数声明

//本文件用的函数声明
static void freeresource();

size_t   g_arglen = 0;       // 为main参数开辟的内存空间大小
size_t   g_environlen = 0;   // 为环境变量开辟的内存空间大小

int    g_argc = 0;        // 参数数量
char** g_argv = nullptr;     // 指向新分配内存中保存环境变量的各处，类似于 environ[i]
char** g_init_argv = nullptr;// 指向原环境变量的所在处，用于放置新的标题字符串

char* g_p_argmem = nullptr;  // 指向开辟内存的首地址
char* g_p_envmem = nullptr;  // 指向开辟内存的首地址，保存的环境变量的首地址

pid_t cur_pid;               //当前进程的pid
pid_t parent_pid;            //父进程的pid


CSocket* p_socket = new CSocket();

int main(int argc, char *const *argv)
{       
    int exitcode = 0;           //退出代码，先给0表示正常退出
    int i;                      //临时用

    // 和进程本身有关的全局变量量
    cur_pid    = getpid();      //取得进程pid
    parent_pid = getppid();     //取得父进程的id 

    // 参数及环境变量的全局变量
    for(i = 0; i < argc; i++)
        g_arglen += strlen(argv[i]) + 1;
 
    for(i = 0; environ[i]; i++) 
        g_environlen += strlen(environ[i]) + 1;

    g_argc = argc;        
    g_init_argv = (char **) argv; 

    CConfig *p_config = CConfig::GetInstance(); //单例类
    if(p_config->Load("nginx.conf") == false) {        
        log_error_core(NGX_LOG_ALERT, 0, "Failed to load the config file [%s], Now exit.", "nginx.conf");
        exitcode = 2; //标记找不到文件
        goto lblexit;
    }

    //(3)一些初始化函数，准备放这里    
    init_log();             //日志初始化(创建/打开日志文件)
    if(init_signals() != 0) {
        // 打印日志
        exitcode = 1;
        goto lblexit;
    }
    
    init_setproctitle();    //把环境变量搬家

    if (p_config->GetInt("Daemon", 1)) {
        int dae = daemon_process();  // 三种情况：-1 1 0 s
        if (dae == -1) {
            exitcode = 1;
            goto lblexit;
        } else if (dae == 1) {
            exitcode = 0;
            goto lblexit;
        } else { }  
    }  
    master_process_cycle();
            
lblexit:
    freeresource();  
    return exitcode;
}

void freeresource() {
    // (1)对于因为设置可执行程序标题导致的环境变量分配的内存，我们应该释放
    if (g_argv) {
        delete[] g_argv;
        g_argv = nullptr;
    }

    if (g_p_argmem) {
        delete[] g_p_argmem;
        g_p_argmem = nullptr;
    }

    if (g_p_envmem) {
        delete[] g_p_envmem;
        g_p_envmem = nullptr;
    }

    // CConfig 中的 list 会自动释放

    // (2)关闭日志文件
    if (log_s.fd != STDERR_FILENO && log_s.fd != -1) {
        close(log_s.fd);  // 不用判断结果了
        log_s.fd = -1;    // 标记下，防止被再次close吧
    }
}
