
#include <cstdio>
#include <cstring>

char* Ltrim(char line[]) {
    char *strtmp = line;
    for (; *strtmp != '\0'; strtmp++) 
        if (*strtmp != ' ') break;
    return strtmp; 
}


// 删除尾部的空格、换行符；替换为结束符
char* Rtrim(char string[]) {
    while (strlen(string) > 0) {
        if ((string[strlen(string)-1] == ' ') || (string[strlen(string)-1] == '\n') || (string[strlen(string)-1] == '\r'))
            string[strlen(string)-1] = 0; 
        else  {return string;}
    }
    return NULL;
}