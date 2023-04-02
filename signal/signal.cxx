// 和信号有关的函数放这里
#include <errno.h>   //errno
#include <signal.h>  //信号相关头文件
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "macro.h"
#include "global.h"
#include "func.h"


static void signal_handler(int signo, siginfo_t *siginfo, void *ucontext);  // static表示该函数只在当前文件内可见

typedef struct
{
    int signo;            // 信号对应的数字编号 ，每个信号都有对应的#define ，大家已经学过了
    const char *signame;  // 信号对应的中文名字 ，比如SIGHUP
    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);  
} signal_t;


signal_t signals[] = {
    // signo      signame             handler
    {SIGHUP, "SIGHUP", signal_handler},    // 终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    {SIGINT, "SIGINT", signal_handler},    // 标识2
    {SIGTERM, "SIGTERM", signal_handler},  // 标识15
    {SIGCHLD, "SIGCHLD", signal_handler},  // 子进程退出时，父进程会收到这个信号--标识17
    {SIGQUIT, "SIGQUIT", signal_handler},  // 标识3
    {SIGIO, "SIGIO", signal_handler},      // 指示一个异步I/O事件【通用异步I/O信号】
    {SIGUSR1, "SIGUSR1", signal_handler},
    {SIGSYS, "SIGSYS, SIG_IGN", nullptr},         // 我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                        // 所以我们把handler设置为nullptr，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    {0, nullptr, nullptr}  
};

// 信号处理函数 统一用此函数
static void signal_handler(int signo, siginfo_t *siginfo, void *ucontext) {
    std_error_core(errno, "now comes a signal");  // 测试字符函数
    
}

// 初始化信号的函数，用于注册信号处理程序
// 返回值：0成功  ，-1失败
int init_signals() {
    signal_t *sig; // 
    struct sigaction sa; 

    for (sig = signals; sig->signo != 0; sig++) {
        memset(&sa, 0, sizeof(struct sigaction));  // 先填充为 0

        if (sig->handler) {
            sa.sa_flags = SA_SIGINFO;
            sa.sa_sigaction = sig->handler;
        } else {
            sa.sa_handler = SIG_IGN;  
        }  // end if

        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, sig->signo);  // 自己实现的程序中，添加此信号的阻塞

        if (sigaction(sig->signo, &sa, nullptr) == -1) {
            log_error_core(NGX_LOG_EMERG, errno, "sigaction(%s) failed", sig->signame);  // 显示到日志文件中去的
            return -1;                                                                       // 有失败就直接返回
        } else {
            log_error_core(NGX_LOG_INFO, 0, "sigaction(%s) succed!", sig->signame);
            // std_error_core(0, "sigaction(%s) succed!", sig->signame);
        }
    }          
    return 0;  
}
