//函数声明放在这个头文件里-------------------------------------------

#ifndef __NGX_FUNC_H__
#define __NGX_FUNC_H__


//字符串相关函数
char* Ltrim(char []);
char* Rtrim(char []);

//设置可执行程序标题相关函数
void   init_setproctitle();
void   setproctitle(const char *title);

//和日志，打印输出有关
void   log_init();
void   std_error_core(int errnum, const char *fmt, ...);
void   log_error_core(int level,  int err, const char *fmt, ...);

u_char *fmt_string_print(u_char *buf, u_char *last, const char *fmt, ...);
u_char *fmt_string(u_char *buf, u_char *last,const char *fmt,va_list args);

int    init_signals();
void   master_process_cycle();

#endif  