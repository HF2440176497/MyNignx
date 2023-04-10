
#ifndef MEMORY_H
#define MEMORY_H

// 内存单例类
// 未来实现内存池功能
class CMemory {

private:
    CMemory() {}
    ~CMemory() {}

private:
    static CMemory* m_instance;

public:
    static CMemory* GetInstance() {
        if (m_instance == nullptr) {
            m_instance = new CMemory();
            static GarRecycle c1;
        }
        return m_instance;

    }
    class GarRecycle {
    public:
        ~GarRecycle() {
            if (CMemory::m_instance) {
                delete CMemory::m_instance;
                CMemory::m_instance = nullptr;
            }
        }
    };

public:
    void* AllocMemory(int memCount, bool ifmemset);
    void  FreeMemory(void* point);
};

#endif