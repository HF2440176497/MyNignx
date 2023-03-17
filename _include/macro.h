
// 宏定义
#ifndef MACRO_H
#define MACRO_H

#define cpymem(dst, src, n)    (((u_char *) memcpy(dst, src, n)) + (n))  // 返回的指针指向赋值字符串后的首地址 先将 void* 指针转换为 u_char* 再用于算术运算

//数字相关--------------------
#define NGX_MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数
#define NGX_INT64_LEN          (sizeof("-9223372036854775808") - 1)     

//日志相关--------------------

#define MAX_ERROR_STR             2048   // 显示的错误信息最大数组长度       
#define MAX_TIME_STR              40

#define NGX_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，日志的内容不再写入log参数指定的文件，而是会直接将日志输出到标准错误设备比如控制台屏幕
#define NGX_LOG_EMERG             1    //紧急 【emerg】
#define NGX_LOG_ALERT             2    //警戒 【alert】
#define NGX_LOG_CRIT              3    //严重 【crit】
#define NGX_LOG_ERR               4    //错误 【error】：属于常用级别
#define NGX_LOG_WARN              5    //警告 【warn】：属于常用级别
#define NGX_LOG_NOTICE            6    //注意 【notice】
#define NGX_LOG_INFO              7    //信息 【info】
#define NGX_LOG_DEBUG             8    //调试 【debug】：最低级别

#define NGX_ERROR_LOG_PATH       "error.log"   //定义日志存放的路径和文件名 

#endif