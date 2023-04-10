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


int g_worker_process_num;

// 静态函数声明 内部链接 不添加到 func.h 只有 master_process_cycle 非静态函数
static void ReadConf();
static void init_master_process();

static void init_worker_process(int port_num, int port_value);
static void start_worker_process(int threadnums);

static int spawn_process(int port_num, int port_value, int process_num);
static void worker_process_cycle(int port_num, int port_value);

/**
 * @brief master process 的配置读取函数
 * 主要是读取监听的端口数量
*/
static void ReadConf() {
    CConfig* p_config = CConfig::GetInstance();
    p_socket->m_port_count = p_config->GetInt("ListenPortCount", 2);  // 默认监听两个端口
    p_socket->m_total_connections = p_config->GetInt("WorkerConnections", 50);
    g_worker_process_num = p_config->GetInt("WorkProcesses", 4);
    return;
}


/**
 * @brief 设置标题
*/
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

    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1) {        
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
    
    ReadConf();
    init_master_process();
    start_worker_process(g_worker_process_num);  // 这里要创建worker子进程 传入要创建的子进程数量 这里是全局变量
    
    sigset_t set;  // 此 set 仅用于 master process 内
    sigemptyset(&set);

    // 父进程在此循环
    for ( ;; ) {
        // log_error_core(0, 0, "haha--这是父进程");
        // sleep(5);
        // sigsuspend(&set);  // 新 set 是不屏蔽的，收到信号之后，恢复原来屏蔽   
        // std_error_core(0, "Now we just finish sigsuspend ");
    }
    return;
}

// 根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
// process_count 要创建的子进程数量，作为全局变量而非成员变量
static void start_worker_process(int process_count) {
    char str[20];
    memset(str, 0 , strlen(str));
    int port_count = p_socket->m_port_count;

    for (int port_num = 0; port_num < port_count; port_num++) {
        // 在此处 直接读取端口号，传入 spawn，进而用于创建 socket

        CConfig *p_config = CConfig::GetInstance();
        sprintf(str, "ListenPort%d", port_num);
        int port_value = p_config->GetInt(str, DEFAULT_PORT + port_num);  // 默认的端口设置为 9000+

        // 端口对应线程的编号，即 process_num
        for (int process_num = 0; process_num < process_count; process_num++) {
            if (spawn_process(port_num, port_value, process_num) == -1) { exit(-1); }
        }
    }
    return;
}

/**
 * @brief master process 创建 worker process，进入各自 worker_process_cycle 开始进行处初始化
 * @param port_num 
 * @param port_value 
 * @param process_num 当前端口对应线程的编号 0, ..., g_worker_process_num
 */
static int spawn_process(int port_num, int port_value, int process_num)
{
    pid_t  pid;  // pid 仅用于判别父子进程
    pid = fork();
    switch (pid)
    {  
    case -1: //产生子进程失败
        log_error_core(NGX_LOG_EMERG, errno, "Fork process at port_num: [%d] process_num: [%d] has failed", port_num, process_num);
        return -1;

    case 0:  //子进程分支
        parent_pid = cur_pid;              // 因为是子进程了，所有原来的pid变成了父pid ngx_pid 全局变量，当前进程的 pid
        cur_pid = getpid();                // 重新获取pid,即本子进程的pid
        worker_process_cycle(port_num, port_value);    // 所有worker子进程，在这个函数里不断循环
        break;

    default: //这个应该是父进程分支，直接break;，流程往switch之后走        
        break;
    }//end switch

    return pid;
}

static void worker_process_cycle(int port_num, int port_value) {

    init_worker_process(port_num, port_value);  // 调用 open_listen
    log_error_core(0, 0, "子进程运行中");

    for (;;) {
        if (p_socket->epoll_process_events(port_num, port_value, -1) == -1) { 
            log_error_core(0, 0, "子进程退出 epoll_process_events 失败");
            exit(-1); 
        }
    }  // end for(;;)
    log_error_core(0, 0, "子进程退出 worker_process_cycle 结束");
    return;
}

// 描述：子进程创建时调用本函数进行一些初始化工作
static void init_worker_process(int port_num, int port_value) {
    sigset_t set;
    sigemptyset(&set); 
    if (sigprocmask(SIG_SETMASK, &set, nullptr) == -1)  {
        log_error_core(NGX_LOG_ALERT, errno, "sigprocmask has failed at [%s]", "init_worker_process");
    }

    // 设置标题为 worker process [port_num]: [port_value]
    char title[MAX_TITLE_LEN];
    sprintf(title, "worker process [%d]: [%d]", port_num, port_value);
    setproctitle(title);

    //....将来再扩充代码
    //....

    p_socket->event_init(port_num, port_value);
    p_socket->epoll_init();

    return;
}