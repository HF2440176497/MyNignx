
// 单例类：读取配置项
// 不需要全局变量，只需要类的
#ifndef CConfig_H
#define CConfig_H

#include "global.h"  
#include <vector>

using namespace std;


class CConfig {
private:
    CConfig();
    CConfig(const CConfig& CConfig);
    CConfig& operator = (const CConfig& CConfig);
    ~CConfig();

public:
    static CConfig* getInstance() {
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
    bool LoadLine(const char* line);
    int GetInt(const char* itemname, const int def);
    const char* GetString(const char* itemname);

    void Test();

public:
    vector<LPConfItem> m_itemlist;
};

#endif

// 需注意这里要包含头文件