
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstring>
#include "c_conf.h"  // LPConfItem 在 global.h 声明
#include "func.h"

#define LINESIZE 200


// 静态成员变量赋值
CConfig* CConfig::m_instance = nullptr;

CConfig::CConfig() {}

CConfig::~CConfig() {
    for (auto item = m_itemlist.begin(); item != m_itemlist.end(); item++) 
        delete *item;
    m_itemlist.clear();
}
/**
 * @brief 读取配置文件
 * @return true 读取成功 false 打开失败或存在非法行 
*/
bool CConfig::Load(const char* pfile) {
    FILE* fp = fopen(pfile, "r");
    if (fp == NULL) 
        return false;
    char line[LINESIZE];

    while(!feof(fp)) {
        
        memset(line, 0, LINESIZE);
        if (fgets(line, LINESIZE-1, fp) == NULL)
            continue;
        
        char *noteline, *sepline;
        char* strline = Ltrim(line);

        // std::cout << "Start Ltrim: " << endl;
        // std::cout << "Ltrim: " << strline << endl;

        if (*strline < 0) 
            continue;

        // 判断换行或注释行
        if ((*strline == 0) || (*strline == 10) || (*strline == 13) || (*strline == '#') || (*strline == '['))
            continue;

        // 去除行内注释
        if (strchr(strline, '#') != NULL) {
            noteline = strchr(strline, '#');
            for (int k=0; k < strlen(noteline); k++)
                noteline[k] = 0;
        }
        // 去除行尾空格、换行符；替换为结束符
        Rtrim(strline);

        if (strchr(strline, '=') != NULL) {
            sepline = strchr(strline, '=');
            *sepline = '\0';
        } else {
            continue;
        }

        LPConfItem p_confitem = new ConfItem;
        memset(p_confitem, 0, sizeof(ConfItem));

        // 拷贝字符串
        strcpy(p_confitem->itemvalue, Ltrim(sepline+1));
        strcpy(p_confitem->itemname, Rtrim(strline));

        m_itemlist.push_back(p_confitem);
    }
    fclose(fp);
    return true;
}


void CConfig::Test() {

    std::cout<< "*********** CConfig test ***********" << endl;

    for (auto it = m_itemlist.begin(); it != m_itemlist.end(); it++) {
        std::cout<< "itemname: " << (*it)->itemname << endl;
        std::cout<< "itemvalue: " << (*it)->itemvalue << endl;
    }
}

/**
 * 根据配置项名称，读取配置项值
*/
const char* CConfig::GetString(const char* itemname) {
    for (auto it = m_itemlist.begin(); it != m_itemlist.end(); it++) {
        if (strcmp((*it)->itemname, itemname) == 0) 
            return (*it)->itemvalue;
    }
    return nullptr;  // 未找到则返回 nullptr
}

int CConfig::GetInt(const char* itemname, const int def) {
    for (auto it = m_itemlist.begin(); it != m_itemlist.end(); it++) {
        if (strcmp((*it)->itemname, itemname) == 0) 
            return atoi((*it)->itemvalue);
    }
    return def;
}
