
// int dup2(int oldfd, int newfd);
//
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "macro.h"
#include "func.h"
#include "global.h"
#include "c_conf.h"


/**
 * @brief 调用者根据返回结果判断进程性质
 * @return int 
 */
int daemon_process() {
    switch (fork()) {
    case -1:
        log_error_core(0, errno, "Failed to create daemon process at [%s]", "fork");
        return -1;
    case 0:
        break;
    default:
        // exit(0);  父进程不直接退出 返回 1 进行资源释放
        return 1;
    }

    // 子进程走到这里 已经在 main 中赋值的 parent_pid curpid 此处要更新
    parent_pid = cur_pid;
    cur_pid = getpid();

    if (setsid() == -1) {
        log_error_core(LOG_EMERG, errno, "Failed to create daemon process at [%s]", "setsid");
        return -1;
    }
    umask(0);
    chdir("/");

    int fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        log_error_core(LOG_EMERG, errno, "Failed to create daemon process at [%s]", "open /dev/null");
        return -1;
    }
    if (dup2(fd, STDIN_FILENO) == -1) {
        log_error_core(LOG_EMERG, errno, "Failed to create daemon process at [%s]", "redirect STDIN");
        return -1;
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        log_error_core(LOG_EMERG, errno, "Failed to create daemon process at [%s]", "redirect STDOUT");
        return -1;
    }

    return 0;  // 表示成功
}