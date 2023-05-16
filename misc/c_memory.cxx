
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_memory.h"

/**
 * @brief 
 * @param memCount 
 * @param ifmemset true 表示要求内存清0
 * @return void* 
 */
void *CMemory::AllocMemory(int memCount, bool ifmemset) {
    void *tmpData = (void *)new char[memCount];
    if (ifmemset)
        memset(tmpData, 0, memCount);
    return tmpData;
}

/**
 * @brief 分配内存时是 char*，回收时也先转换为 char*
 * @param point 
 */
void CMemory::FreeMemory(void *point) {
    if (point != nullptr) {
        delete[] ((char *)point);
    }
    point = nullptr;  // 原指针指空
}