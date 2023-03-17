

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <time.h>
#include <sys/time.h>

#include "c_conf.h"
#include "func.h"
#include "global.h"
#include "macro.h"

// err_level 由自己设定
static u_char err_levels[][20] = {
        {"stderr"},  // 0：控制台错误
        {"emerg"},   // 1：紧急
        {"alert"},   // 2：警戒
        {"crit"},    // 3：严重
        {"error"},   // 4：错误
        {"warn"},    // 5：警告
        {"notice"},  // 6：注意
        {"info"},    // 7：信息
        {"debug"}    // 8：调试
};

log_t log_s;

/**
 * @brief 根据 errno 组合出错误信息字符串
 * @param buf 放置字符串的内存空间
 * @param err errno
 * @details 用于日志模块，位于 func_log 文件，不放置于 func_string 文件
*/
u_char* log_string_errno(u_char* buf, u_char* last, int errnum) {
    const char* errinfo = strerror(errnum);
    size_t len = strlen(errinfo);
    buf = fmt_string_print(buf, last, "%d", errnum);
    buf = fmt_string_print(buf, last, "  %s  ", errinfo);
    return buf;
}


/**
 * @brief 写入日志文件或标准错误
 * @param level 当 level == 0 时，向 STDERR_FILENO 输出；当 level != 0 向文件输出
 * @param errnum ERRNO 当其大于 level 时，此时不会进行打印日志文件
 * @param fmt 
 * @details 日期  PID: 线程ID  [错误等级]:ERRNO  错误信息
 * 向文件输出产生错误时，也会向 STDERR_FILENO 输出错误信息
 * 
*/
void log_error_core(int level, int errnum, const char *fmt, ...) {

    u_char errstr[MAX_ERROR_STR + 1] {0};  // 错误信息全部保存于此
    u_char* last = errstr + MAX_ERROR_STR;

    // 获取时间 
    time_t now = time(NULL);
	tm* time_s = localtime(&now);

    // 显示时间 思路：构造时间字符串
    u_char timestr[MAX_TIME_STR+1] {0};
    
    fmt_string_print(timestr, timestr+ MAX_TIME_STR, "%4d/%02d/%02d %02d:%02d:%02d", 
        time_s->tm_year+1900, time_s->tm_mon+1, time_s->tm_mday,
        time_s->tm_hour+8, time_s->tm_min, time_s->tm_sec);  // 转换为北京时间 东八时

    // 组合字符串 “日期  PID: 线程ID  [错误等级]: ”
    u_char* p = cpymem(errstr, timestr, strlen((const char*)timestr));
    p = fmt_string_print(p, last, "  PID: %p", cur_pid);
    p = fmt_string_print(p, last, "  [%s]: ", err_levels[level]);
    
    // 若有 ERRNO 组合错误信息字符串
    if (errnum) 
        p = log_string_errno(p, last, errnum);

    // 组合自定义字符串 
    va_list args;
    va_start(args, fmt);
    p = fmt_string(p, last, fmt, args);
    va_end(args);

    if (p >= (last-1)) 
        p = (last-1) - 1;
    *p++ = '\n';

    if (level == 0) {
        if (log_s.fd != STDERR_FILENO)
            ssize_t n = write(STDERR_FILENO, errstr, p - errstr);
        return;
    } 
    while (1) {
        if (level > log_s.level)
            break;

        ssize_t n = write(log_s.fd, errstr, p-errstr);
        if (n == -1) {
            if (errno == ENOSPC)  {  // 磁盘没有空间
                /* do something */
            } else {  // 其余情况转而向 STDERR_FILENO 输出信息
                if (log_s.fd != STDERR_FILENO)  
                    n = write(STDERR_FILENO, errstr, p - errstr);
            }
        }
        break;  // break while
    }
    return;
}

void log_init() {
    u_char* plogname = NULL;
    size_t  nlen;

    // 从配置文件中读取和日志相关的配置
    CConfig *p_config = CConfig::GetInstance();

    plogname = (u_char *)p_config->GetString("Log");
    if (plogname == NULL) {
        plogname = (u_char *)NGX_ERROR_LOG_PATH;  //"logs/error.log" ,logs目录需要提前建立出来
    }
    log_s.fd = open((const char *)plogname, O_WRONLY | O_APPEND | O_CREAT, 0644);
    log_s.level = p_config->GetInt("LogLevel", NGX_LOG_NOTICE);
    
    if (log_s.fd == -1)  // 如果有错误，则直接定位到 标准错误上去
    {
        log_error_core(0, errno, "could not open error log file: open() \"%s\" failed", plogname);
        log_s.fd = STDERR_FILENO;  // 直接定位到标准错误去了
    }
    return;

}