
// 此文件对应一个子类，可处理一种业务场景：
// 将此场景所需的所有逻辑函数放置此源文件 统一由 ThreadRecvProc 调用进入

// 仿照书中的设计：定义某签名类型的函数指针 handler 
// 实现符合 handler 类型的不同的函数

// 不同的函数：作为成员函数 or 一般函数，最好作为成员函数，因此只是对于此子类才用得到
#include <string.h>
#include <arpa/inet.h>

#include "macro.h"
#include "global.h"
#include "func.h"
#include "c_socket.h"
#include "c_socketlogic.h"
#include "c_crc32.h"

typedef bool (CSocketLogic::*handler)(char* msg);

static const handler status_handleset[] {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &CSocketLogic::_HandleRegister,
    &CSocketLogic::_HandleLogin
};

#define TOTAL_COMMAND_NUM sizeof(status_handleset) / sizeof(handler)

CSocketLogic::CSocketLogic(/* args */) {
}

CSocketLogic::~CSocketLogic() {
}


// 虚函数实现：将通用的处理部分放在此函数内
// 此时已完成：包头中指定长度合法，消息完整
// 应当完成：
// 1. 定位：消息头，包头，包体（若存在）
// 2. 检验：收取状态，防止非正常的调用
// msg 指向的内存分配有 LEN+1，最后一个是 \0 字符
// 若包体长度 == 0，msg_body 就会指向这个最后的 \0 
// 
// msg：消息头+包头+包体的完整消息
int CSocketLogic::ThreadRecvProc(char* msg) {
    log_error_core(LOG_STDERR, 0, "进入到 ThreadRecvProc 函数，线程开始处理消息");
    STRUC_MSG_HEADER msg_header;
    memcpy(&msg_header, msg, MSG_HEADER_LEN);
    COMM_PKG_HEADER pkg_header;
    memcpy(&pkg_header, msg + MSG_HEADER_LEN, PKG_HEADER_LEN);

    char* lp_body = msg + MSG_HEADER_LEN + PKG_HEADER_LEN;
    uint16_t pkg_len = ntohs(pkg_header.pkgLen);
    uint16_t body_len = pkg_len - PKG_HEADER_LEN;
    
    int cur_crc = ntohl(pkg_header.crc32);  // crc 为 int 型，相当于 uint_32_t 
    const connection_t* lp_conn = msg_header.lp_curconn;  // 作为消费者，指向的连接对象只可访问不容修改

    // 检验此时连接对象的连接状态，个人评价：是惊险且巧妙的设计，消费者要谨慎处理 lp_curconn
    if (lp_conn->fd <= 0) {
        log_error_core(LOG_STDERR, 0, "线程当前处理连接对象失效");
        return -1;
    }
    if (lp_conn->s_cursequence != msg_header.msg_cursequence) {
        log_error_core(LOG_STDERR, 0, "线程当前处理连接可能已回收");
        return -1;
    }
    uint16_t imsgcode = ntohs(pkg_header.msgCode);

    // 包体不存在
    if (body_len == 0) {
        log_error_core(LOG_STDERR, 0, "包体不存在，处理函数直接返回");
        if (pkg_header.crc32 != 0) {
            return -1;
        }
        return 0;
    }
    // 若存在包体，检验 crc 值
    int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)lp_body, body_len); 
    if (calccrc != cur_crc) {
        log_error_core(0, 0, "CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!");
        return -1;
    }
    // 检验 msgcode 
    if (imsgcode >= TOTAL_COMMAND_NUM) {
        log_error_core(LOG_STDERR, 0, "msgcode 超过最大范围");
        return -1;
    }
    if (status_handleset[imsgcode] == nullptr) {
        log_error_core(LOG_STDERR, 0, "没有对应的处理函数");
        return -1;
    }
    (this->*status_handleset[imsgcode])(msg);
    return 0;
}

bool CSocketLogic::_HandleRegister(char* msg) {
    log_error_core(LOG_STDERR, 0, "CSocketLogic::_HandleRegister 执行");

    LPSTRUCT_REGISTER lp_struct = (LPSTRUCT_REGISTER)(msg + MSG_HEADER_LEN + PKG_HEADER_LEN);

    log_error_core(LOG_INFO, 0, "_HandleRegister iType: [%d], username: [%s], password: [%s]", 
    ntohl(lp_struct->iType), lp_struct->username, lp_struct->password);

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}

bool CSocketLogic::_HandleLogin(char* msg) {
    log_error_core(LOG_STDERR, 0, "CSocketLogic::_HandleLogin 执行");
    LPSTRUCT_LOGIN lp_struct = (LPSTRUCT_LOGIN)(msg + MSG_HEADER_LEN + PKG_HEADER_LEN);

    log_error_core(LOG_INFO, 0, "_HandleLogin username: [%s], password: [%s]", 
     lp_struct->username, lp_struct->password);

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}

