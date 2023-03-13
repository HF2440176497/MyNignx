
#include <cstdio>
#include <unistd.h>
#include <iostream>
#include "c_conf.h"
#include "func.h"

int    g_argc = 0;
char** g_argv = NULL;     // 指向新分配内存中保存环境变量的各处，类似于 environ[i]
char** g_init_argv = NULL;// 指向原环境变量的所在处

char* g_p_envmem = NULL;  // 指向开辟内存的首地址，保存的环境变量的首地址
int   g_environlen = 0;   // 为环境变量开辟的内存空间大小

char* g_p_argmem = NULL;  // 指向开辟内存的首地址
int   g_arglen = 0;       // 为main参数开辟的内存空间大小
int*  g_p_arglen = NULL;  // 保存各参数的长度的数组，分配argc*int空间


CConfig* p_config;  // 配置文件相关的全局变量

int main(int argc, char *const *argv) {

    g_argc = argc;
    g_init_argv = (char**)argv;

    CConfig* p_config = CConfig::getInstance();  
    if (p_config->Load("nginx.conf") == false) {
        std::cout<< "读取失败" <<endl;  // 写入日志
    }
    p_config->Test();

    init_setproctitle();
    setproctitle("wanghf nginx");

    for(;;) {
       sleep(1); //休息1秒
       printf("休息1秒\n");
    }

    return 0;
}
