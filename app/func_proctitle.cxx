//和设置课执行程序标题（名称）相关的放这里 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //env
#include <string.h>

#include "global.h"

//设置可执行程序标题相关函数：分配内存，并且把环境变量拷贝到新内存中来
void init_setproctitle() {   
    //这里无需判断penvmen == nullptr,有些编译器new会返回nullptr，有些会报异常，但不管怎样，如果在重要的地方new失败了，你无法收场，让程序失控崩溃，助你发现问题为好； 
    g_p_envmem = new char[g_environlen]; 
    memset(g_p_envmem,0,g_environlen);  //内存要清空防止出现问题

    char *p_envtmp = g_p_envmem;        // 把原环境变量内容拷贝到新地方 p_envtmp
    for (int i = 0; environ[i]; i++) {
        size_t size = strlen(environ[i])+1 ; // 不要拉下+1，否则内存全乱套了，因为strlen是不包括字符串末尾的\0的
        strncpy(p_envtmp, environ[i], size);     
        environ[i] = p_envtmp;               // 重定向
        p_envtmp += size;
    }

    g_p_argmem = new char[g_arglen];  // 同理，将 搬家到此处
    memset(g_p_argmem, 0, g_arglen);
    char* p_argtmp = g_p_argmem;

    g_argv = new char*[g_argc];  // 保存地址，因此 g_argv[i] 是各个参数地址

    for (int i = 0; g_init_argv[i]; i++) {
        size_t size = strlen(g_init_argv[i])+1;
        strncpy(p_argtmp, g_init_argv[i], size);  // g_init_argv[i] 保存原有参数的地址
        g_argv[i] = p_argtmp;
        p_argtmp += size;
    }
    return;
}

//设置可执行程序标题
void setproctitle(const char *title) {

    size_t titlelen = strlen(title); 
    size_t sum_size = g_arglen + g_environlen;
    if(titlelen >= sum_size) {
        // 控制台输出
        return;
    }
    g_init_argv[1] = nullptr;  

    //(4)把标题弄进来，注意原来的命令行参数都会被覆盖掉，不要再使用这些命令行参数,而且g_os_argv[1]已经被设置为nullptr了
    char *ptmp = g_init_argv[0]; //让ptmp指向g_os_argv所指向的内存
    strncpy(ptmp, title, titlelen);
    ptmp += titlelen; // 跳过标题 

    // ptmp 定位到 title 所在处之后，此后连续的空间被清空
    size_t remain_size = sum_size - titlelen;
    memset(ptmp, 0, remain_size);
    return;
}