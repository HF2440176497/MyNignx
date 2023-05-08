
// 单例类：读取配置项
// 不需要全局变量，只需要类的
#ifndef CCONFIG_H
#define CCONFIG_H

#include "global.h"  
#include <vector>


// 结构定义 放在 c_conf.h 因为 c_conf 类才会包括此类型的成员
typedef struct {
    char itemname[30];
    char itemvalue[60];
}ConfItem, *LPConfItem;

class CConfig {
private:
    CConfig();
    CConfig(const CConfig& CConfig);
    CConfig& operator = (const CConfig& CConfig);
    ~CConfig();

public:
    static CConfig* GetInstance() {
        if (m_instance == nullptr) {
            m_instance = new CConfig();
            static GarRecycle tmp;  // 同样是 static 成员
        }      
        return m_instance;
    }
    class GarRecycle {
    public:
        ~GarRecycle() {
            if (CConfig::m_instance) {
                delete CConfig::m_instance;  // 这里会调用 ~CConfig() 析构函数中可添加代码
                CConfig::m_instance = nullptr;
            }
        }
    };

private:
    static CConfig* m_instance;  // m_ 表示成员变量

public:
    bool Load(const char* pfile);
    int GetInt(const char* itemname, const int def);
    const char* GetString(const char* itemname);
    void ReadConf_Thread();
    void Test();

public:
    std::vector<LPConfItem> m_itemlist;
};

#endif
