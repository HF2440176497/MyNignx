
#ifndef FUNC_H
#define FUNC_H


// 配置项读取相关函数
char* Ltrim(char []);
char* Rtrim(char []);

// 设置进程标题相关函数
void init_setproctitle();
void setproctitle(const char*);


#endif