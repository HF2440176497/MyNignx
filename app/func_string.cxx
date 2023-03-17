
// 和字符串相关的函数
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <cstdarg>
#include <stack>
#include <queue>
#include <sys/time.h>
#include "macro.h"
#include "func.h"  // 参考代码中，ui_string frac_string 设置为 static 函数，属于内部链接


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
    return NULL;
}

/**
 * @brief 构建 fmt 的格式字符串，将传入的参数按 % 指定的类型显示
 * 是对于 fmt_string 的一层简单的封装，便于传入可变参数 args
 * @details 对应参考代码中的 ngx_slprintf 函数
*/
u_char* fmt_string_print(u_char *buf, u_char *last, const char *fmt, ...) {
    u_char* p;
    va_list args;
    va_start(args, fmt);
    p = fmt_string(buf, last, fmt, args);
    va_end(args);  // 释放 args 参数
    return p;
}


/**
 * @brief 按照指定格式，组合得到字符串
 * @param buf 字符串存放地址
 * @param last 存放地址结束位置
 * @param fmt 用于指定格式的字符串
 * @return 返回字符串存放位置的首地址
*/
u_char* fmt_string(u_char *buf, u_char *last,const char *fmt,va_list arg) {
    u_char zero;

    int64_t   i64;
    u_int64_t ui64;

    u_int64_t width;
    u_int64_t sign;
    u_int64_t hex;

    u_char *lp_str;

    double frac;   // 分数
    int    scale;  // 小数部分转换为整数的缩放尺度

    u_int64_t width_frac;
    u_int64_t ui64_frac;  // 小数部分的长度，分数的整数部分，分数的小数部分

    while (*fmt && buf < last) {
        
        // 判断 *fmt: fmt 此处不自增，当 *fmt != %，需要此位置原样输出
        if (*fmt == '%') {

            i64 = 0;
            ui64 = 0;
            width = 0; 
            sign = 1;  // 默认为有符号数
            hex = 0;
            ui64_frac = 0;
            width_frac = 0; 

            zero = (u_char)((*++fmt == '0' ? '0' : ' ')); 

            while (*fmt <= '9' && *fmt >= '0') {
                width = width * 10 + (*fmt++ - '0');  // 注意编码，(*fmt-'0') 为有符号 char 型，但符号位此时为 0，再转为无符号型 uint
            }

            // 特殊字符
            while (1) {
                switch (*fmt)
                {
                case 'x':
                    hex = 1;
                    sign = 0;
                    fmt++;
                    continue;
                case 'X':
                    hex = 2;
                    sign = 0;
                    fmt++;
                    continue;
                case 'u':
                    sign = 0;  // 表示无符号数
                    fmt++;
                    continue;
                case '.':  // 后面跟上数字，表示小数点后几位，这里需要读取到 frac_width
                        // 数字之后一定有 f，读到 f 之后再处理

                    fmt++;
                    while (*fmt <= '9' && *fmt >= '0') {
                        width_frac = width_frac * 10 + (*fmt++ - '0');
                    }
                    break;  // 特殊符号判断完毕
                default:
                    break;
                }            
                break;  // switch-case 出来后跳出 while 循环
            }
        
            // 格式控制字符
            switch (*fmt)
            {
            case '%':
                *buf++ = '%';
                fmt++;
                continue;  // 跳到 while (*fmt && buf < last) 重新开始循环，因为特殊字符可能在后面
            
            case 'd':
                if (sign)
                    i64 = (int64_t)va_arg(arg, int);
                else 
                    ui64 = (u_int64_t)va_arg(arg, u_int);  // 高位补零扩维
                fmt++;
                break;

            case 's':
                lp_str = va_arg(arg, u_char*);  
                
                // 无符号0-255 超过 128 的数字将会转为负值; 0-127 的字符能够正常解释
                strncpy((char *)buf, (const char*)lp_str, strlen((const char*)lp_str) );
                buf += strlen((const char*)lp_str);

                // 参考代码：
                // while (*lp_str && buf < last) 
                //     *buf = *lp_str++;
                fmt++;
                continue;  // 字符串类型不用再进行剩余代码，重新进行 while

            case 'p':
                ui64 = (u_int64_t) va_arg(arg, pid_t);
                sign = 0;
                fmt++;
                break;

            case 'f':
                frac = va_arg(arg, double);
                if (frac < 0) {
                    *buf++ = '-';
                    frac = -frac;
                }
                ui64 = (u_int64_t)frac;
                buf = ui64_string(buf, last, ui64, zero, hex, width);

                if (width_frac) {
                    scale = 1;

                    for (int i=0; i < width_frac; i++) scale = scale * 10;
                    ui64_frac = (u_int64_t)(scale * (frac - (double)ui64));  // 不考虑进位，u_int64_t 将丢弃不需要的位
                    
                    if (buf < last) {*buf++ = '.';}  // 显示小数点
                    buf = frac_string(buf, last, ui64_frac, zero, hex, width_frac);
                }
                fmt++;
                continue;  // 注意：f 读到了，此部分格式祖字符串显示也结束了

            default:  // 未找到上述几种格式控制字符 则原样输出，buf fmt 移动     
                *buf++ = *fmt++;
                continue;  
            }

            // 走到这里，处理除了 f 的其他类型，输出 ui64 
            if (sign) {
                if (i64 < 0) {
                    *buf++ = '-';
                    ui64 = (u_int64_t) (-i64);
                } else {
                    ui64 = (u_int64_t) (i64);
                }
            }
            buf = ui64_string(buf, last, ui64, zero, hex, width);
            // fmt++;  当 %d break 之后，fmt 已经 ++，因此不再需要 fmt++
            continue;  // continue while
        } else {  // 未识别到 % 符号，则会原样输出 end if (*fmt++ == '%')
            *buf++ = *fmt++;
            continue;  // continue while
        }
    }
    return buf;
}

// 思考：这里与参考代码不同的地方在于参考代码考虑了进位，当 frac_width != 0 需要进一步修正 ui64 这里并未考虑

/**
 * @brief 将 u_int64_t 类型的整数，显示为字符串，采用前补零的方式 我们从后向前赋值
 * @param buf 
 * @param zero 
 * @param width 整数的显示宽度
 * @details 先不考虑十六进制
*/
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
        return NULL;  // 说明 buf 预留的空间不够显示 width 长度的字符
    }
    return (buf+width+1);  // 此时 buf 正指向开头位置前一位置，但应当定位到字符串之后的首位置，加一是因为最后 buf--
}


/**
 * @brief 将 u_int64_t 类型的整数，显示为字符串，采用后补零的方式，因此采用从前向后的显示顺序
 * @param buf 
 * @param zero 
 * @param width 整数的显示宽度
 * @details 先不考虑小数进位问题
*/
u_char* frac_string(u_char *buf, u_char *last, u_int64_t ui64, u_char zero, u_int64_t hexadecimal, u_int64_t width_frac) {
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
        return NULL;  // 说明 buf 预留的空间不够显示 width 长度的字符
    }
    return buf;  // 此时 buf 正指向字符串之后的首位置
}