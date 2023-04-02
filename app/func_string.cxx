//和打印格式相关的函数放这里

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>   //类型相关头文件
#include <queue>
#include <stack>
#include "global.h"
#include "macro.h"
#include "func.h"

// static u_char* ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64,u_char zero, uintptr_t hexadecimal, uintptr_t width);

static u_char* ui64_string(u_char *buf, u_char *last, u_int64_t ui64, u_char zero, u_int64_t hexadecimal, u_int64_t width);
static u_char* frac_string(u_char *buf, u_char *last, u_int64_t ui64, u_char zero, u_int64_t hexadecimal, u_int64_t width);


char* Ltrim(char line[]) {
    char *strtmp = line;
    for (; *strtmp != '\0'; strtmp++) 
        if (*strtmp != ' ') break;
    return strtmp; 
}


// 删除尾部的空格、换行符 替换为结束符
char* Rtrim(char string[]) {
    while (strlen(string) > 0) {
        if ((string[strlen(string)-1] == ' ') || (string[strlen(string)-1] == '\n') || (string[strlen(string)-1] == '\r'))
            string[strlen(string)-1] = 0; 
        else  {return string;}
    }
    return nullptr;
}

u_char *fmt_string_print(u_char *buf, u_char *last, const char *fmt, ...) 
{
    va_list   args;
    u_char   *p;

    va_start(args, fmt); //使args指向起始的参数
    p = fmt_string(buf, last, fmt, args);
    va_end(args);        //释放args   
    return p;
}

u_char *fmt_string(u_char *buf, u_char *last, const char *fmt, va_list args) {
    u_char zero;

    /*
    #ifdef _WIN64
        typedef unsigned __int64  uintptr_t;
    #else
        typedef unsigned int uintptr_t;
    #endif
    */
    uintptr_t width, sign, hex, width_frac, scale;  // 临时用到的一些变量

    ssize_t size;   // %s 时记录字符串长度
    u_char* lp_str; // %s 时代表字符串指针

    int64_t  i64;   // 保存%d对应的可变参
    uint64_t ui64;  // 保存%ud对应的可变参，临时作为%f可变参的整数部分也是可以的
    

    double   frac;       // 保存%f对应的可变参
    uint64_t ui64_frac;  //%f可变参数,根据%.2f等，取得小数部分的2位后的内容；

    while (*fmt && buf < last) {
        if (*fmt == '%') {
            zero = (u_char)((*++fmt == '0') ? '0' : ' ');
            i64 = 0;
            ui64 = 0;
            width = 0;
            sign = 1;  // 默认为有符号数
            hex = 0;
            ui64_frac = 0;
            width_frac = 0;

            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');

            for (;;) {
                switch (*fmt)  // 处理一些%之后的特殊字符
                {
                case 'u':      //%u，这个u表示无符号
                    sign = 0;  // 标记这是个无符号数
                    fmt++;     // 往后走一个字符
                    continue;  // 回到for继续判断

                case 'X':     //%X，X表示十六进制，并且十六进制中的A-F以大写字母显示，不要单独使用，一般是%Xd
                    hex = 2;  // 标记以大写字母显示十六进制中的A-F
                    sign = 0;
                    fmt++;
                    continue;
                case 'x':     //%x，x表示十六进制，并且十六进制中的a-f以小写字母显示，不要单独使用，一般是%xd
                    hex = 1;  // 标记以小写字母显示十六进制中的a-f
                    sign = 0;
                    fmt++;
                    continue;

                case '.':                               // 其后边必须跟个数字，必须与%f配合使用，形如 %.10f：表示转换浮点数时小数部分的位数，比如%.10f表示转换浮点数时，小数点后必须保证10位数字，不足10位则用0来填补；
                    fmt++;                              // 往后走一个字符，后边这个字符肯定是0-9之间，因为%.要求接个数字先
                    while (*fmt >= '0' && *fmt <= '9') {
                        width_frac = width_frac * 10 + (*fmt++ - '0');
                    }  // end while(*fmt >= '0' && *fmt <= '9')
                    break;

                default:
                    break;
                }  // end switch (*fmt)
                break;
            }  // end for ( ;; )

            switch (*fmt) 
            {
            case '%':  // 只有%%时才会遇到这个情形，本意是打印一个%，所以
                *buf++ = '%';
                fmt++;
                continue;

            case 'd':      // 显示整型数据，如果和u配合使用，也就是%ud,则是显示无符号整型数据
                if (sign)  
                    i64 = (int64_t)va_arg(args, int); 
                else                     
                    ui64 = (uint64_t)va_arg(args, u_int);
                fmt++;
                break;  // 这break掉，直接跳道switch后边的代码去执行,这种凡是break的，都不做fmt++;  *********************【switch后仍旧需要进一步处理】

            case 's':                    
                lp_str = va_arg(args, u_char *);
                size = strlen((const char*)lp_str);
                if (buf + size < last)
                    strncpy((char *)buf, (const char*)lp_str, size);
                buf += size;

                // 参考代码：
                // while (*p && buf < last) {
                //     *buf++ = *p++;  // 那就装，比如  "%s"    ，   "abcdefg"，那abcdefg都被装进来
                // }
                fmt++;
                continue;  // 重新从while开始执行

            case 'p':  // 转换一个pid_t类型
                i64 = (int64_t)va_arg(args, pid_t);
                sign = 1;
                fmt++;
                break;

            case 'f':                       
                frac = va_arg(args, double); 
                if (frac < 0) {
                    *buf++ = '-';  
                    frac = -frac;  
                }
            
                ui64 = (int64_t)frac;  
                ui64_frac = 0;

                // 如果要求小数点后显示多少位小数
                if (width_frac) {
                    scale = 1;  // 缩放从1开始
                    for (int n = width_frac; n; n--)
                        scale *= 10;
                    ui64_frac = (uint64_t)((frac - (double)ui64) * scale);
                }

                // 正整数部分，先显示出来
                buf = ui64_string(buf, last, ui64, zero, 0, width); 

                if (width_frac)  // 指定了显示多少位小数
                {
                    if (buf < last) {
                        *buf++ = '.';
                    }
                    buf = frac_string(buf, last, frac, '0', 0, width_frac);
                }
                fmt++;
                continue;

            default:
                *buf++ = *fmt++; 
                continue;        
            } // end switch (*fmt)

            // 显示 %d %p 的，会走下来，原来 fmt 在判定到 *fmt == d *fmt == p 基础上，fmt++
            // 调用 ui64_string 函数之后，不再 fmt++ 个人实现中加了两次

            if (sign) {
                if (i64 < 0) {
                    *buf++ = '-';
                    ui64 = (uint64_t)-i64;
                } else {
                    ui64 = (uint64_t)i64;
                }
            }  // end if (sign)

            buf = ui64_string(buf, last, ui64, zero, hex, width);
        } else {
            *buf++ = *fmt++;  
        }  // end if (*fmt == '%')
    }  // end while (*fmt && buf < last)

    return buf;
}

u_char* ui64_string(u_char *buf, u_char *last, u_int64_t ui64, u_char zero, u_int64_t hexadecimal, u_int64_t width) {
    int non_zero = 0;  // 记录整数显示的有效位数

    static u_char hex[] = "0123456789abcdef"; 
    static u_char HEX[] = "0123456789ABCDEF";

    u_int64_t unit = 0;      // 记录每一位上的数字
    std::queue<u_char> unit_que;
    u_int64_t ui64_tmp = ui64;
    
    // 十进制显示，需要确定十进制的位数
    if (hexadecimal == 0) {
        do {
            non_zero++;
            unit = ui64_tmp % 10;
            ui64_tmp -= unit; ui64_tmp /= 10;
            unit_que.push((u_char) (unit + '0')); 
        } while (ui64_tmp);
    } else if (hexadecimal == 1) {   
        while (ui64_tmp) {
            unit_que.push(hex[(u_int64_t) (ui64_tmp & 0x0f)]);
            non_zero++;
            ui64_tmp >>= 4;
        }
    } else {  // hexadecimal == 2
        while (ui64_tmp) {
            unit_que.push(HEX[(u_int64_t) (ui64_tmp & 0x0f)]);
            non_zero++;
            ui64_tmp >>= 4;
        }
    }
    // 当 width == 0 时，width 应当取为 non_zero 当 width != 0 此时需要修改的是 non_zero
    if (width == 0) {width = non_zero;}
    else {non_zero = (non_zero > width) ? width : non_zero;}  

    buf += (width-1);  // 移动 buf 到最后允许显示的位置

    if (buf < last) {
        for (int i=0; (i<non_zero) && (!unit_que.empty()); i++) {
            *buf-- = unit_que.front();  // 返回队头元素
            unit_que.pop();
        }
        // 若 width <= non_zero 不会进入循环体
        for (int i=0; i < width-non_zero; i++)
            *buf-- = zero;
    } else {
        return nullptr;  // 说明 buf 预留的空间不够显示 width 长度的字符
    }
    return (buf+width+1);  // 此时 buf 正指向开头位置前一位置，但应当定位到字符串之后的首位置，加一是因为最后 buf--
}


static u_char* frac_string(u_char *buf, u_char *last, u_int64_t ui64, u_char zero, u_int64_t hexadecimal, u_int64_t width_frac) {
    int non_zero = 0;  // 记录整数显示的有效位数
    
    static u_char hex[] = "0123456789abcdef"; 
    static u_char HEX[] = "0123456789ABCDEF";

    u_int64_t unit = 0;
    std::stack<u_char> unit_stk;  // 保存各位字符串

    u_int64_t ui64_tmp = ui64;

    if (hexadecimal == 0) {
        do {
            non_zero++;
            unit = ui64_tmp % 10;
            ui64_tmp -= unit; ui64_tmp /= 10;
            unit_stk.push((u_char) (unit + '0')); 
        } while (ui64_tmp);

    } else if (hexadecimal == 1) {
        while (ui64_tmp) {
            unit_stk.push(hex[(u_int64_t) (ui64_tmp & 0x0f)]);
            non_zero++; 
            ui64_tmp >>= 4;
        }
    } else {  // hexadecimal == 2
        while (ui64_tmp) {
            unit_stk.push(HEX[(u_int64_t) (ui64_tmp & 0x0f)]);
            non_zero++;
            ui64_tmp >>= 4;
        }
    }
    if (width_frac == 0) {width_frac = non_zero;}
    else {non_zero = (non_zero > width_frac) ? width_frac : non_zero;}  

    // 从前向后显示
    if (buf < last) {

        // 从栈顶逐步取出 non_zero 个值 始终是从前向后赋值
        for (int i=0; (i<non_zero) && (!unit_stk.empty()); i++) {
            *buf++ = unit_stk.top();
            unit_stk.pop();
        }
        // 若 frac_width <= non_zero 不会进入循环体
        for (int i=0; i < width_frac-non_zero; i++)
            *buf++ = zero;
        
    } else {
        return nullptr;  // 说明 buf 预留的空间不够显示 width 长度的字符
    }
    return buf;  // 此时 buf 正指向字符串之后的首位置
}
