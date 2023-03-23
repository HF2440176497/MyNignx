//和开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "func.h"
#include "macro.h"
#include "c_conf.h"


// 静态函数声明 内部链接 不添加到 func.h 
static void init_master_process();
static void init_worker_process(int inum);
static void start_worker_process(int threadnums);
static int spawn_process(int inum,const char *pprocname);
static void worker_process_cycle(int inum, const char *pprocname);

int worker_process_num;

static void init_master_process() {
    sigset_t set;       
    sigemptyset(&set);   

    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {        
        log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!");
    }

    char title[g_arglen + g_environlen] = {0};
    strcpy(title, MASTER_PROCESS_TITLE);
    strcat(title, " ");

    for (int i=0; i < g_argc; i++)
        strcat(title, g_argv[i]);

    setproctitle(title);  // 其中含有标题过长检测
    return;
}

void master_process_cycle() {    
    
    CConfig* p_config = CConfig::GetInstance();
    worker_process_num = p_config->GetInt("WorkProcesses", 4);

    init_master_process();
    start_worker_process(worker_process_num);  //这里要创建worker子进程

    sigset_t set;
    sigemptyset(&set); 
    
    // 父进程在此循环
    for ( ;; ) {
        log_error_core(0,0,"haha--这是父进程");

        sigsuspend(&set);       
        std_error_core(0, "Now we just finish sigsuspend ");
    }
    return;
}

// 根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
// threadnums:要创建的子进程数量
static void start_worker_process(int threadnums) {
    int i;
    for (i = 0; i < threadnums; i++) {
        spawn_process(i, WORKER_PROCESS_TITLE);
    } //end for
    return;
}

static int spawn_process(int inum,const char *pprocname)
{
    pid_t  pid;  // pid 仅用于判别父子进程
    pid = fork();
    switch (pid)
    {  
    case -1: //产生子进程失败
        log_error_core(NGX_LOG_ALERT,errno,"ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!",inum,pprocname);
        return -1;

    case 0:  //子进程分支
        parent_pid = cur_pid;              // 因为是子进程了，所有原来的pid变成了父pid ngx_pid 全局变量，当前进程的 pid
        cur_pid = getpid();                // 重新获取pid,即本子进程的pid
        worker_process_cycle(inum,pprocname);    //我希望所有worker子进程，在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
        break;

    default: //这个应该是父进程分支，直接break;，流程往switch之后走        
        break;
    }//end switch

    return pid;
}

static void worker_process_cycle(int inum, const char *pprocname) {
    init_worker_process(inum);
    
    for (;;) {
        log_error_core(0, 0, "good--这是子进程，测试 [%0.5f]", 123.456, cur_pid);
        log_error_core(0, 0, "good--这是子进程，测试 [%5.5f]", 123.456, cur_pid);
        log_error_core(0, 0, "good--这是子进程，测试 [%05.5f]", 123.456, cur_pid);
        log_error_core(0, 0, "good--这是子进程，测试 [%s]", "my name is...", cur_pid);

        sleep(5);
    }  // end for(;;)
    return;
}

// 描述：子进程创建时调用本函数进行一些初始化工作
static void init_worker_process(int inum) {
    sigset_t set;
    sigemptyset(&set); 
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  {
        log_error_core(NGX_LOG_ALERT, errno, "ngx_worker_process_init()中sigprocmask()失败!");
    }
    setproctitle(WORKER_PROCESS_TITLE);  // 设置标题
    //....将来再扩充代码
    //....
    return;
}