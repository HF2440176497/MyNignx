
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
#include "c_lock.h"

typedef bool (CSocketLogic::*handler)(lp_connection_t lp_conn, char* msg);

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


// 此时包头中指定长度合法，消息完整
// 
// msg：消息头+包头+包体的完整消息 指向堆区空间，移除队列后还没有释放内存，会在 ThreadFunc 中释放内存
int CSocketLogic::ThreadRecvProc(char* msg) {
    pthread_t tid = pthread_self();

    STRUC_MSG_HEADER msg_header;
    memcpy(&msg_header, msg, MSG_HEADER_LEN);
    COMM_PKG_HEADER pkg_header;
    memcpy(&pkg_header, msg + MSG_HEADER_LEN, PKG_HEADER_LEN);

    char* lp_body = msg + MSG_HEADER_LEN + PKG_HEADER_LEN;
    uint16_t pkg_len = ntohs(pkg_header.pkgLen);
    uint16_t body_len = pkg_len - PKG_HEADER_LEN;
    
    int cur_crc = ntohl(pkg_header.crc32);  // crc 为 int 型，相当于 uint_32_t 
    lp_connection_t lp_conn = msg_header.lp_curconn;  // 作为消费者，handler 可能将处理结果反馈给对应的连接对象，因此未采用 const

    // log_error_core(LOG_STDERR, 0, "ThreadRecvProc: 线程 [%d] 开始处理来自 [%d] 的消息", tid, lp_conn->fd);

    // 检验此时连接对象的连接状态，这里我们不加锁
    if (lp_conn->JudgeOutdate(msg_header.msg_cursequence) == false) {
        log_error_core(LOG_STDERR, 0, "JudgeOutdate 返回，ThreadRecvProc 处理线程退出");
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
    (this->*status_handleset[imsgcode])(lp_conn, msg);  // 重申：msg 是完整消息
    return 0;
}


bool CSocketLogic::_HandleRegister(lp_connection_t lp_conn, char* msg) {
    
    STRUC_MSG_HEADER msg_header;
    memcpy(&msg_header, msg, MSG_HEADER_LEN);
    pthread_t tid = pthread_self();

    // 双重加锁机制：第一次判断避免了无谓的加锁；第二次判断是在加锁之后进行 避免了被打断
    if (lp_conn->JudgeOutdate(msg_header.msg_cursequence) == false) {
        log_error_core(LOG_STDERR, 0, "第一次判断 JudgeOutdate 返回，_HandleRegister 返回");
        return false;
    } else {
        CLock lock(&lp_conn->s_connmutex);
    }
    if (lp_conn->JudgeOutdate(msg_header.msg_cursequence) == false) {
        log_error_core(LOG_STDERR, 0, "第二次判断 JudgeOutdate 返回，_HandleRegister 返回");
        return false;
    }
    log_error_core(LOG_INFO, 0, "线程 [%d] 获取到了锁 _HandleRegister, fd: [%d] ", tid, lp_conn->fd);
    LPSTRUCT_REGISTER lp_struct = (LPSTRUCT_REGISTER)(msg + MSG_HEADER_LEN + PKG_HEADER_LEN);

    log_error_core(LOG_INFO, 0, "线程 [%d] _HandleRegister fd: [%d], iType: [%d], username: [%s], password: [%s]", 
     tid, lp_conn->fd, ntohl(lp_struct->iType), lp_struct->username, lp_struct->password);

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}


bool CSocketLogic::_HandleLogin(lp_connection_t lp_conn, char* msg) {

    STRUC_MSG_HEADER msg_header;
    memcpy(&msg_header, msg, MSG_HEADER_LEN);
    pthread_t tid = pthread_self();
    
    // 双重加锁机制：第一次判断避免了无谓的加锁；第二次判断是在加锁之后进行 避免了被打断
    if (lp_conn->JudgeOutdate(msg_header.msg_cursequence) == false) {
        log_error_core(LOG_STDERR, 0, "第一次判断 JudgeOutdate 返回，_HandleLogin 返回");
        return false;
    } else {
        CLock lock(&lp_conn->s_connmutex);
    }
    if (lp_conn->JudgeOutdate(msg_header.msg_cursequence) == false) {
        log_error_core(LOG_STDERR, 0, "第二次判断 JudgeOutdate 返回，_HandleLogin 返回");
        return false;
    }
    log_error_core(LOG_INFO, 0, "线程 [%d] 获取到了锁 _HandleLogin, fd: [%d] ", tid, lp_conn->fd);

    LPSTRUCT_LOGIN lp_struct = (LPSTRUCT_LOGIN)(msg + MSG_HEADER_LEN + PKG_HEADER_LEN);
    log_error_core(LOG_INFO, 0, "线程 [%d] _HandleLogin fd: [%d], username: [%s], password: [%s]", 
     tid, lp_conn->fd, lp_struct->username, lp_struct->password);

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}

