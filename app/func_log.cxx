
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno

#include "global.h"
#include "macro.h"
#include "func.h"
#include "c_conf.h"


// 错误等级，和ngx_macro.h里定义的日志等级宏是一一对应关系
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
log_t   log_s;  // 描述整个项目配置的日志属性

static u_char* log_errno(u_char *buf, u_char *last, int err);


static u_char* log_errno(u_char *buf, u_char *last, int errnum) {
    const char* errinfo = strerror(errnum);
    size_t len = strlen(errinfo);

    buf = fmt_string_print(buf, last, " [%d: ", errnum);
    buf = fmt_string_print(buf, last, "%s]", errinfo);

    return buf;
}

void std_error_core(int errnum, const char *fmt, ...) {    
    u_char  errstr[MAX_ERROR_STR+1];
    u_char  *p,*last;
    memset(errstr,0,sizeof(errstr)); 
    last = errstr + MAX_ERROR_STR;
                                                
    p = cpymem(errstr, "nginx: ", strlen("nginx: "));     //p指向"nginx: "之后    
    
    va_list args;
    va_start(args, fmt);
    p = fmt_string(p,last,fmt,args); 
    va_end(args);

    if (errnum) {
        p = log_errno(p, last, errnum);
    }

    // last 就是数组最后位置，last - 1 是倒数第二个位置，需要保存 \n
    // 个人理解：这里实际不用判断，因为 fmt_string 会判断 buf 与 last
    if (p >= (last - 1)) {
        p = (last - 1) - 1; 
    }
    *p++ = '\n';
    write(STDERR_FILENO,errstr,p - errstr);
    
    return;
}

void log_error_core(int level, int errnum, const char *fmt, ...)
{
    u_char  *last;
    u_char  errstr[MAX_ERROR_STR+1];   //这个+1也是我放入进来的，本函数可以参考ngx_log_stderr()函数的写法；

    memset(errstr,0,sizeof(errstr));  
    last = errstr + MAX_ERROR_STR;   
    
    struct timeval   tv;
    struct tm        tm;
    time_t           sec;   //秒
    u_char           *p;    //指向当前要拷贝数据到其中的内存位置
    va_list          args;

    memset(&tv,0,sizeof(struct timeval));    
    memset(&tm,0,sizeof(struct tm));

    gettimeofday(&tv, nullptr);            

    sec = tv.tv_sec;             
    localtime_r(&sec, &tm);      
    tm.tm_mon++;                 
    tm.tm_year += 1900;          
    
    u_char strcurrtime[40]={0};  
    fmt_string_print(strcurrtime,  
                    (u_char *)-1,                       //若用一个u_char *接一个 (u_char *)-1,则 得到的结果是 0xffffffff....，这个值足够大
                    "%4d/%02d/%02d %02d:%02d:%02d",     //格式是 年/月/日 时:分:秒
                    tm.tm_year, tm.tm_mon,
                    tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec);
    p = cpymem(errstr,strcurrtime,strlen((const char *)strcurrtime));  //日期增加进来，得到形如：     2019/01/08 20:26:07
    p = fmt_string_print(p, last, " [%s] ", err_levels[level]);                //日志级别增加进来，得到形如：  2019/01/08 20:26:07 [crit] 
    p = fmt_string_print(p, last, " %p ", cur_pid);                             //支持%P格式，进程id增加进来，得到形如：   2019/01/08 20:50:15 [crit] 2037:

    va_start(args, fmt);                     //使args指向起始的参数
    p = fmt_string(p, last, fmt, args);   //把fmt和args参数弄进去，组合出来这个字符串
    va_end(args);                            //释放args 

    if (errnum)  
        p = log_errno(p, last, errnum);
    
    if (p >= (last - 1)) {
        p = (last - 1) - 1; 
    }
    *p++ = '\n'; //增加个换行符      

    while (1) {
        if (level > log_s.log_level) {
            break;
        }
        // 写日志文件
        ssize_t n = write(log_s.fd, errstr, p - errstr);  // 文件写入成功后，如果中途
        if (n == -1) {
            // 写失败有问题
            if (errno == ENOSPC)  {
                /* do something */
            } else {
                if (log_s.fd != STDERR_FILENO)
                    n = write(STDERR_FILENO, errstr, p - errstr);
            }
        }
        break;
    }  // end while
    return;
}

/**
 * @brief 从配置文件中读取 日志路径 and 日志等级
*/
void init_log() {
    u_char *plogname = nullptr;
    size_t nlen;

    CConfig *p_config = CConfig::GetInstance();
    plogname = (u_char *)p_config->GetString("Log");
    if(plogname == nullptr) {
        plogname = (u_char *) ERROR_LOG_PATH;
    }
    log_s.log_level = p_config->GetInt("LogLevel", NGX_LOG_NOTICE);  // 缺省日志等级设置为 NGX_LOG_NOTICE
    log_s.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT,0644);  
    
    if (log_s.fd == -1) {
        log_error_core(NGX_LOG_ALERT, errno,"[alert] could not open error log file: open() \"%s\" failed", plogname);
        log_s.fd = STDERR_FILENO;      
    }
    return;
}
