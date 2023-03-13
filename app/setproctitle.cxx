
// 设置进程标题
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern int g_argc;
extern char** g_argv;
extern char** g_init_argv;

extern char* g_p_envmem;  // 指向手动开辟内存，保存的环境变量的首地址
extern int g_environlen;  // 为环境变量开辟的内存空间大小

extern char* g_p_argmem;
extern int g_arglen;     // 为命令行参数开辟的内存空间总大小
extern int* g_p_arglen;  // 保存各参数的长度的数组，分配argc*int空间

/**
 * @brief
 */
void init_setproctitle() {
    // 环境变量
    for (int i = 0; environ[i]; i++)
        g_environlen += strlen(environ[i]) + 1;

    g_p_envmem = new char[g_environlen];
    memset(g_p_envmem, '\0', sizeof(g_environlen));
    char* p_envtmp = g_p_envmem;

    for (int i = 0; environ[i]; i++) {
        strncpy(p_envtmp, environ[i], strlen(environ[i]));
        environ[i] = p_envtmp;  // 重定向
        p_envtmp += (strlen(environ[i]) + 1);
    }

    // 命令行参数 目前 g_argc == 0 可以先注释掉
    g_p_arglen = new int[g_argc];

    for (int i = 0; g_init_argv[i]; i++) {
        int len = strlen(g_init_argv[i]);
        g_p_arglen[i] = len;
        g_arglen += (len + 1);
    }
    std::cout << "g_arglen: "<< g_arglen << std::endl;

    // g_arglen == 8 ./nginx 

    g_p_argmem = new char[g_arglen];
    memset(g_p_argmem, '\0', g_arglen);
    char* p_argtmp = g_p_argmem;
    g_argv = (char**)g_p_argmem;  // 一定要初始化，否则访问了NULL，造成段错误

    // 重定向时发生错误，g_argv[i] 此时 g_init_argv 指向地址相同，应当 g_argv 初始化指向 g_p_argmem 处
    for (int i = 0; g_init_argv[i]; i++) {
        strncpy(p_argtmp, g_init_argv[i], strlen(g_init_argv[i]));  // g_argv[i] 保存原有参数的地址
        g_argv[i] = p_argtmp;
        p_argtmp += (strlen(g_argv[i]) + 1);
    }
    return;
}

/**
 * @param title 设置的标题字符串
 */
void setproctitle(const char* title) {
    if (strlen(title) > (g_arglen + g_environlen)) {
        std::cout << "进程标题过长" << std::endl;
        exit(1);
    }
    // 参考写法：先将内存以 0 填充
    memset(g_init_argv[0], '\0', g_arglen + g_environlen);
    strcpy(g_init_argv[0], title);
    return;
}

// 释放内存
void freeresource() {
    delete []g_p_envmem;
    delete []g_p_arglen;
    delete []g_p_argmem;
    return;
}