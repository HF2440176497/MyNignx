
#ifndef FUNC_H
#define FUNC_H


// 配置项读取相关函数 位于 func_string
char* Ltrim(char []);
char* Rtrim(char []);

// 设置进程标题相关函数 位于 func_proctitle
void init_setproctitle();
void setproctitle(const char*);

// 字符串相关 位于 func_string
u_char* ui64_string(u_char*, u_char*, u_int64_t, u_char, u_int64_t, u_int64_t);
u_char* frac_string(u_char*, u_char*, u_int64_t, u_char, u_int64_t, u_int64_t);

u_char* fmt_string(u_char*, u_char*, const char*, va_list);
u_char* fmt_string_print(u_char*, u_char*, const char*, ...);

// 日志相关 位于 func_log.cxx
u_char* log_string_errno(u_char*, u_char* , int);
void log_error_core(int , int, const char *, ...);
void log_init();


#endif