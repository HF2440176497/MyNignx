#ifndef C_CRC32_H
#define C_CRC32_H

#include <stddef.h>

// 单例类
class CCRC32 {
private:
    CCRC32();

public:
    ~CCRC32();

private:
    static CCRC32* m_instance;

public:
    static CCRC32* GetInstance() {
        if (m_instance == nullptr) {
            if (m_instance == nullptr) {
                m_instance = new CCRC32();
                static CGarhuishou cl;
            }
        }
        return m_instance;
    }
    class CGarhuishou {
    public:
        ~CGarhuishou() {
            if (CCRC32::m_instance) {
                delete CCRC32::m_instance;
                CCRC32::m_instance = nullptr;
            }
        }
    };
    //-------
public:
    void Init_CRC32_Table();
    // unsigned long Reflect(unsigned long ref, char ch); // Reflects CRC bits in the lookup table
    unsigned int Reflect(unsigned int ref, char ch);  // Reflects CRC bits in the lookup table

    // int   Get_CRC(unsigned char* buffer, unsigned long dwSize);
    int Get_CRC(unsigned char* buffer, unsigned int dwSize);

public:
    // unsigned long crc32_table[256]; // Lookup table arrays
    unsigned int crc32_table[256];  // Lookup table arrays
};

#endif
