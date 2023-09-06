// 和信号有关的函数放这里
#include <errno.h>   //errno
#include <signal.h>  //信号相关头文件
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "func.h"
#include "global.h"
#include "macro.h"

static void signal_handler(int signo, siginfo_t *siginfo, void *ucontext);  // static表示该函数只在当前文件内可见
static void process_get_status();

typedef struct
{
    int         signo;    // 信号对应的数字编号 ，每个信号都有对应的#define ，大家已经学过了
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
    {SIGSYS, "SIGSYS, SIG_IGN", nullptr},  // 我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                           // 所以我们把handler设置为nullptr，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    {0, nullptr, nullptr}};

/**
 * @brief 信号处理函数 统一用此函数
 */
static void signal_handler(int signo, siginfo_t *siginfo, void *ucontext) {
    signal_t *sig;     // 自定义结构
    char     *action;  // 一个字符串，用于记录一个动作字符串以往日志文件中写

    for (sig = signals; sig->signo != 0; sig++) {
        if (sig->signo == signo) {
            break;
        }
    }
    action = (char *)"";  // 指向字符串常量的指针
    if (process_form == PROCESS_MASTER) {
        switch (signo) {
        case SIGCHLD:
            reap = 1;  // 进行标记
            break;

        default:
            break;
        }  // end switch
    } else if (process_form == PROCESS_WORKER) {
        // worker进程的往这里走
        //......以后再增加
        //....
    } else {
    }  // end if(process == NGX_PROCESS_MASTER)

    // 记录日志信息
    if (siginfo && siginfo->si_pid) {
        log_error_core(LOG_INFO, 0, "signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action);
    } else {
        log_error_core(LOG_INFO, 0, "signal %d (%s) received %s", signo, sig->signame, action);  // 没有发送该信号的进程id，所以不显示发送
    }

    //.......其他需要扩展的将来再处理；

    // 子进程状态有变化，通常是意外退出
    if (signo == SIGCHLD) {
        process_get_status();  // 获取子进程的结束状态
    }                          // end if
    return;
}

// 获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
static void process_get_status() {
    pid_t pid;
    int   status;
    int   err;
    int   one = 0;  // 抄自官方nginx，应该是标记信号正常处理过一次

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);  // 第一个参数为-1，表示等待任何子进程，
        if (pid == 0) {
            return;
        }  
        if (pid == -1) {
            err = errno;
            if (err == EINTR)  { // 调用被某个信号中断
                continue;
            }
            if (err == ECHILD && one)  { // 表示没有需要 wait 的子进程
                return;
            }
            if (err == ECHILD)  {  // 可能是 pid 设置问题，pid 不是当前的子进程
                log_error_core(LOG_INFO, err, "waitpid() failed!");
                return;
            }
            log_error_core(LOG_INFO, err, "waitpid() failed!");
            return;
        }  
        one = 1;  // 标记waitpid()返回了正常的返回值
        if (WTERMSIG(status)) {  // 表示 child 确实是被信号终止的
            log_error_core(LOG_ALERT, 0, "pid = %P exited on signal %d!", pid, WTERMSIG(status));  // 获取使子进程终止的信号编号
        } else {
            log_error_core(LOG_NOTICE, 0, "pid = %P exited with code %d!", pid, WEXITSTATUS(status));  // WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }
    }  // end for
    return;
}

// 初始化信号的函数，用于注册信号处理程序
// 返回值：0成功  ，-1失败
int init_signals() {
    signal_t        *sig;
    struct sigaction sa;

    for (sig = signals; sig->signo != 0; sig++) {
        memset(&sa, 0, sizeof(struct sigaction));  // 先填充为 0

        if (sig->handler) {
            sa.sa_flags     = SA_SIGINFO;  // 此时的 handler 需要三个参数，handler 能处理更多的信息
            sa.sa_sigaction = sig->handler;
        } else {
            sa.sa_handler = SIG_IGN;
        }  // end if
        sigemptyset(&sa.sa_mask);  // 阻塞信号集清空
        if (sigaction(sig->signo, &sa, nullptr) == -1) {
            log_error_core(LOG_EMERG, errno, "sigaction(%s) failed", sig->signame);  // 显示到日志文件中去的
            return -1;                                                               // 有失败就直接返回
        } else {
            log_error_core(LOG_INFO, 0, "sigaction(%s) succed!", sig->signame);
        }
    }
    return 0;
}
