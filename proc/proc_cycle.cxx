//和开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>
#include <sys/epoll.h>

#include "func.h"
#include "macro.h"
#include "global.h"
#include "c_conf.h"
#include "c_socketlogic.h"

int g_worker_process_num = 2;   // 定义时尽量初始化

// 静态函数声明 内部链接 不添加到 func.h 只有 master_process_cycle 非静态函数
static void init_master_process();

static void init_worker_process(int proc_num);
static void start_worker_process(int threadnums);

static int spawn_process(int proc_num, const char *proc_name);
static void worker_process_cycle(int proc_num, const char *proc_name);

/**
 * @brief 主进程的相关初始化工作
*/
static void init_master_process() {

    
    return;
}


void master_process_cycle() {
    // 屏蔽指定信号
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

    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1) {
        log_error_core(LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmask()失败!");
    }

    // 设置进程标题
    char title[g_arglen + g_environlen] = {0};
    strcpy(title, MASTER_PROCESS_TITLE);
    strcat(title, " ");
    for (int i=0; i < g_argc; i++) {
        strcat(title, g_argv[i]);  // 紧接着是各参数
    }
    setproctitle(title);  // 其中含有标题过长检测

    // 父进程相关的配置信息
    CConfig* p_config = CConfig::GetInstance();
    g_worker_process_num = p_config->GetInt("WorkProcesses", g_worker_process_num);

    start_worker_process(g_worker_process_num);  // 这里要创建worker子进程 传入要创建的子进程数量 这里是全局变量
    
    // 
    sigemptyset(&set);
    for ( ;; ) {
        sigsuspend(&set);  // 新 set 是不屏蔽的，收到信号之后，恢复原来屏蔽   
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // 休息一秒 
    }
    return;
}

// 根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
// process_count 要创建的子进程数量，作为全局变量而非成员变量
static void start_worker_process(int process_count) {
    // matser proc 进行 for 循环，最后只有 master proc return
    for (int process_num = 0; process_num < process_count; process_num++) {
        spawn_process(process_num, "worker process");
    }
    return;
}

/**
 * @brief master process 创建 worker process，进入各自 worker_process_cycle 开始进行处初始化
 * @param port_num 
 * @param port_value 
 * @param process_num 当前端口对应线程的编号 0, ..., g_worker_process_num
 */
static int spawn_process(int proc_num, const char* proc_name) {
    pid_t  pid;  // pid 仅用于判别父子进程
    pid = fork();
    switch (pid)
    {  
    case -1: // 产生子进程失败
        log_error_core(LOG_EMERG, errno, "Fork process at porc_num: [%d] has failed", proc_num);
        return -1;

    case 0:  // 子进程分支
        parent_pid = cur_pid;              // cur_pid: 此时为 mater process 的 pid 
        cur_pid = getpid();                // 更新 cur_pid
        worker_process_cycle(proc_num, proc_name);    // 所有worker子进程，在这个函数里不断循环
        break;

    default: // 父进程分支
        break;
    }// end switch
    return pid;
}


static void worker_process_cycle(int proc_num, const char* proc_name) {
    log_error_core(LOG_INFO, 0, "[%d] worker proc 启动并开始运行", proc_num);
    process_form = PROCESS_WORKER;
    init_worker_process(proc_num);
    setproctitle(proc_name); 
    for (;;) {
        g_socket.epoll_process_events(-1); // -1 表示一直阻塞等待

        // 统计信息打印，考虑到测试的时候总会收到各种数据信息，所以上边的函数调用一般都不会卡住等待收数据
        g_socket.PrintInfo();
    }
    g_threadpool.StopAll();
    g_socket.Shutdown_SubProc();
    log_error_core(0, 0, "子进程退出：worker_process_cycle 结束");
    return;
}


// 描述：子进程创建时调用本函数进行一些初始化工作
static void init_worker_process(int proc_num) {

    // 屏蔽额外指定信号，子进程会继承父进程的信号的 signal mask and dispositions
    sigset_t set;
    sigemptyset(&set); 
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1)  {
        log_error_core(LOG_ALERT, errno, "sigprocmask has failed at [%s]", "init_worker_process");
    }
    //....将来再扩充代码
    //....

    // 线程池在开启监听之前创建
    g_threadpool.Initialize_SubProc();
    g_threadpool.Create();  
    
    g_socket.Initialize_SubProc();
    g_socket.epoll_event_init();

    return;
}