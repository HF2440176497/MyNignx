
// 整个程序入口函数放这里

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "c_conf.h"  // 和配置文件处理相关的类
#include "c_crc32.h"
#include "c_memory.h"
#include "c_socketlogic.h"
#include "c_threadpool.h"
#include "func.h"  // 各种函数声明
#include "macro.h"

// 本文件用的函数声明
static void freeresource();

size_t g_arglen     = 0;  // 为main参数开辟的内存空间大小
size_t g_environlen = 0;  // 为环境变量开辟的内存空间大小

int    g_argc      = 0;        // 参数数量
char** g_argv      = nullptr;  // 指向新分配内存中保存环境变量的各处，类似于 environ[i]
char** g_init_argv = nullptr;  // 指向原环境变量的所在处，用于放置新的标题字符串

char* g_p_argmem = nullptr;  // 指向开辟内存的首地址
char* g_p_envmem = nullptr;  // 指向开辟内存的首地址，保存的环境变量的首地址

int g_daemonized = 0;  // 守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了
int g_stopEvent;       // 标志当前进程退出 1 表进程退出

pid_t        master_pid;    // 作为守护进程的 master process
pid_t        cur_pid;       // 当前进程的pid
pid_t        parent_pid;    // 父进程的pid
int          process_form;  // 进程类别 master 或 worker
sig_atomic_t reap;

CMemory*     p_mem_manager = CMemory::GetInstance();  // 单例类管理堆区上分配的内存
CThreadPool  g_threadpool;
CSocketLogic g_socket;

int main(int argc, char* const* argv) {
    int exitcode = 0;  // 退出代码，先给0表示正常退出
    int i;             // 临时用

    // 标记程非退出
    g_stopEvent = 0;

    // 和进程本身有关的全局变量量
    cur_pid    = getpid();   // 取得进程pid
    parent_pid = getppid();  // 取得父进程的id

    // 参数及环境变量的全局变量
    for (i = 0; i < argc; i++)
        g_arglen += strlen(argv[i]) + 1;

    for (i = 0; environ[i]; i++)
        g_environlen += strlen(environ[i]) + 1;

    g_argc       = argc;
    g_init_argv  = (char**)argv;
    process_form = PROCESS_MASTER;

    CConfig* p_config = CConfig::GetInstance();
    if (p_config->Load("nginx.conf") == false) {
        log_error_core(LOG_ALERT, 0, "Failed to load the config file [%s], Now exit.", "nginx.conf");
        exitcode = 2;  // 标记找不到文件
        goto lblexit;
    }

    // 初始化，不用返回
    CMemory::GetInstance();
    CCRC32::GetInstance();

    // (3)一些初始化函数，准备放这里
    init_log();
    if (init_signals() != 0) {
        exitcode = 1;
        goto lblexit;
    }
    if (g_socket.Initialize() == false) {
        exitcode = 1;
        goto lblexit;
    }
    init_setproctitle();  // 把环境变量搬家

    if (p_config->GetInt("Daemon", 1) == 1) {
        int dae = daemon_process();  // 三种情况：-1 1 0 s
        if (dae == -1) {
            exitcode = 1;
            goto lblexit;
        } else if (dae == 1) {  // 要退出的父进程释放资源
            exitcode = 0;
            goto lblexit;
        } else {
            g_daemonized = 1;
        }
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
        p_mem_manager->FreeMemory(g_p_argmem);
    }
    if (g_p_envmem) {
        p_mem_manager->FreeMemory(g_p_envmem);
    }

    // (2)关闭日志文件
    if (log_s.fd != STDERR_FILENO && log_s.fd != -1) {
        close(log_s.fd);  // 不用判断结果了
        log_s.fd = -1;    // 标记下，防止被再次close吧
    }
}
