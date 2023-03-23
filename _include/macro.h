
#ifndef MACRO_H
#define MACRO_H

// 显示的错误信息最大数组长度
#define MAX_ERROR_STR   2048   

// 配置文件一行信息的最大长度
#define LINESIZE 200

//简单功能函数--------------------
//类似memcpy，但常规memcpy返回的是指向目标dst的指针，而这个ngx_cpymem返回的是目标【拷贝数据后】的终点位置，连续复制多段数据时方便
#define cpymem(dst, src, n)   (((u_char *) memcpy(dst, src, n)) + (n))

//数字相关--------------------
#define MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数
#define INT64_LEN          (sizeof("-9223372036854775808") - 1)     

//日志相关--------------------
//我们把日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
#define NGX_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，而是会直接将日志输出到标准错误设备比如控制台屏幕
#define NGX_LOG_EMERG             1    //紧急 【emerg】
#define NGX_LOG_ALERT             2    //警戒 【alert】
#define NGX_LOG_CRIT              3    //严重 【crit】
#define NGX_LOG_ERR               4    //错误 【error】：属于常用级别
#define NGX_LOG_WARN              5    //警告 【warn】：属于常用级别
#define NGX_LOG_NOTICE            6    //注意 【notice】
#define NGX_LOG_INFO              7    //信息 【info】
#define NGX_LOG_DEBUG             8    //调试 【debug】：最低级别

#define ERROR_LOG_PATH       "logs/error.log"   //定义日志存放的路径和文件名 

#define MASTER_PROCESS_TITLE        "master process"
#define WORKER_PROCESS_TITLE        "worker process"


#endif
