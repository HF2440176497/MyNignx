
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

// 连接，包体长度，
typedef bool (CSocketLogic::*handler)(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body);

static const handler status_handleset[] {
    &CSocketLogic::_HandlePing,
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

bool CSocketLogic::Initialize() {
    bool bParentInit = CSocket::Initialize();  //调用父类的同名函数
    return bParentInit;
}


/**
 * @brief 线程处理单个消息的函数，线程入口函数调用
 * @param msg 消息头+包头+包体的完整消息 指向堆区空间 调用者负责释放收取消息的对应内存
 * @return int -1 表示处理出错
 * @details ThreadRecvProc 出错返回时，线程应当处理下一条消息，对应连接不一定需要关闭，只有 JudgeOutdate 没通过，才需要连接关闭
 * 但是对于线程只要完成当前消息的处理，不负责连接的关闭
 */
int CSocketLogic::ThreadRecvProc(char* msg) {
    pthread_t tid = pthread_self();

    LPSTRUC_MSG_HEADER lp_msgheader = (LPSTRUC_MSG_HEADER)(msg);
    LPCOMM_PKG_HEADER lp_pkgheader = (LPCOMM_PKG_HEADER)(msg+MSG_HEADER_LEN);

    char* lp_body = msg + MSG_HEADER_LEN + PKG_HEADER_LEN;
    uint16_t pkg_len = ntohs(lp_pkgheader->pkgLen);
    uint16_t body_len = pkg_len - PKG_HEADER_LEN;
    
    int cur_crc = ntohl(lp_pkgheader->crc32);  // crc 为 int 型，相当于 uint_32_t 
    lp_connection_t lp_conn = lp_msgheader->lp_curconn;  // 作为消费者，handler 可能将处理结果反馈给对应的连接对象，因此未采用 const

    // 检验此时连接对象的连接状态，这里我们不加锁
    if (lp_conn->JudgeOutdate(lp_msgheader->msg_cursequence) == false) {
        log_error_core(LOG_INFO, 0, "ThreadRecvProc: 当前连接对象已失效");
        return -1;
    }
    // 判断 crc 值
    if (body_len == 0) {  // 包体不存在
        if (lp_pkgheader->crc32 != 0) {
            log_error_core(LOG_ALERT, 0, "ThreadRecvProc: 包体不存在时 CRC 错误");
            return -1;
        }
    } else {  // 包体存在
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)lp_body, body_len); 
        if (calccrc != cur_crc) {
            log_error_core(0, 0, "ThreadRecvProc: CRC 错误，丢弃数据");
            return -1;
        }
    }
    // 通过 imgcode 判断操作
    uint16_t imsgcode = ntohs(lp_pkgheader->msgCode);
    if (imsgcode >= TOTAL_COMMAND_NUM) {
        log_error_core(LOG_INFO, 0, "ThreadRecvProc: msgcode 超过最大范围");
        return -1;
    }
    if (status_handleset[imsgcode] == nullptr) {
        log_error_core(LOG_INFO, 0, "ThreadRecvProc: 没有对应的处理函数");
        return -1;
    }
    // status_handler 需要处理包括没有
    if ((this->*status_handleset[imsgcode])(lp_conn, lp_msgheader, lp_pkgheader, lp_body) == false) {
        log_error_core(LOG_INFO, 0, "业务逻辑函数 status_handleset[imsgcode = ] 出错", imsgcode);
        return -1;
    }
    return 0;
}

/**
 * @brief 
 * @details handler 会不会出现空指针的情况，只要当前处理的 msg 不被释放就行
 */
bool CSocketLogic::_HandleRegister(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body) {
    if ((ntohs(lp_pkgheader->pkgLen) - PKG_HEADER_LEN) != LEN_STRUCT_REGISTER) {
        log_error_core(LOG_INFO, 0, "包体长度不合法 实际长度: [%d] 理论长度: [%d] _HandleRegister 返回", lp_pkgheader->pkgLen - PKG_HEADER_LEN, LEN_STRUCT_REGISTER);
        return false;
    }
    pthread_t tid = pthread_self();

    // 双重加锁机制：第一次判断避免了无谓的加锁；第二次判断是在加锁之后进行 避免了整个 handler 的逻辑被打断
    if (lp_conn->JudgeOutdate(lp_msgheader->msg_cursequence) == false) {
        return false;
    } else {
        CLock lock(&lp_conn->s_connmutex);
    }
    if (lp_conn->JudgeOutdate(lp_msgheader->msg_cursequence) == false) {
        return false;
    }
    // LPSTRUCT_REGISTER lp_struct = (LPSTRUCT_REGISTER)(lp_body);
    // log_error_core(LOG_INFO, 0, "线程 [%d] _HandleRegister fd: [%d], iType: [%d], username: [%s], password: [%s]", 
    //  tid, lp_conn->fd, ntohl(lp_struct->iType), lp_struct->username, lp_struct->password);

    // 构造发送消息 发送消息仍为 消息头+包头+包体
    CCRC32* p_crc32 = CCRC32::GetInstance();
    int sendinfo_len = LEN_STRUCT_REGISTER;

    std::shared_ptr<char> send_ptr = std::shared_ptr<char>(new char[MSG_HEADER_LEN + PKG_HEADER_LEN + sendinfo_len + 1](), 
                                                    [](char* p) { delete[] p; });
    char* send_buf = send_ptr.get();

    // （1）填充消息头
    memcpy(send_buf, lp_msgheader, MSG_HEADER_LEN);

    // （2）填充包头
    LPCOMM_PKG_HEADER p_pkgheader = (LPCOMM_PKG_HEADER)(send_buf + MSG_HEADER_LEN);
    p_pkgheader->msgCode = htons(_CMD_REGISTER);
    p_pkgheader->pkgLen = htons(PKG_HEADER_LEN + sendinfo_len);
    lp_conn->s_sendlen_suppose = PKG_HEADER_LEN + sendinfo_len;  // 应当发送的长度，发送线程中校验用

    // （3）填充包体 内容随意不赋值
    LPSTRUCT_REGISTER send_info = (LPSTRUCT_REGISTER)(send_buf + MSG_HEADER_LEN + PKG_HEADER_LEN);
    p_pkgheader->crc32 = htonl(p_crc32->Get_CRC((u_char*)send_info, sendinfo_len));
    
    MsgSendInQueue(send_ptr);
    send_buf = nullptr;
    send_ptr = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}


bool CSocketLogic::_HandleLogin(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body) {
    if ((ntohs(lp_pkgheader->pkgLen) - PKG_HEADER_LEN) != LEN_STRUCT_LOGIN) {
        log_error_core(LOG_INFO, 0, "包体长度不合法 实际长度: [%d] 理论长度: [%d] _HandleLogin 返回", lp_pkgheader->pkgLen - PKG_HEADER_LEN, LEN_STRUCT_REGISTER);
        return false;
    }
    pthread_t tid = pthread_self();
    if (lp_conn->JudgeOutdate(lp_msgheader->msg_cursequence) == false) {
        return false;
    } else {
        CLock lock(&lp_conn->s_connmutex);
    }
    if (lp_conn->JudgeOutdate(lp_msgheader->msg_cursequence) == false) {
        return false;
    }
    // LPSTRUCT_LOGIN lp_struct = (LPSTRUCT_LOGIN)(lp_body);
    // log_error_core(LOG_INFO, 0, "线程 [%d] _HandleLogin fd: [%d], username: [%s], password: [%s]", 
    //  tid, lp_conn->fd, lp_struct->username, lp_struct->password);

    CCRC32* p_crc32 = CCRC32::GetInstance();
    int sendinfo_len = LEN_STRUCT_LOGIN;

    std::shared_ptr<char> send_ptr = std::shared_ptr<char>(new char[MSG_HEADER_LEN + PKG_HEADER_LEN + sendinfo_len + 1](), 
                                                    [](char* p) { delete[] p; });
    char* send_buf = send_ptr.get();

    // （1）填充消息头
    memcpy(send_buf, lp_msgheader, MSG_HEADER_LEN);

    // （2）填充包头
    LPCOMM_PKG_HEADER p_pkgheader = (LPCOMM_PKG_HEADER)(send_buf + MSG_HEADER_LEN);
    p_pkgheader->msgCode = htons(_CMD_LOGIN);
    p_pkgheader->pkgLen = htons(PKG_HEADER_LEN + sendinfo_len);
    lp_conn->s_sendlen_suppose = PKG_HEADER_LEN + sendinfo_len;  // 应当发送的长度，发送线程中校验用

    // （3）填充包体 内容随意不赋值
    LPSTRUCT_LOGIN send_info = (LPSTRUCT_LOGIN)(send_buf + MSG_HEADER_LEN + PKG_HEADER_LEN);  // 指向包体
    p_pkgheader->crc32 = htonl(p_crc32->Get_CRC((u_char*)send_info, sendinfo_len));
    
    MsgSendInQueue(send_ptr);
    send_buf = nullptr;
    send_ptr = nullptr;

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    return true;
}

/**
 * @brief 对于心跳包，包头，没有包体
 * 对于心跳包有没有必要加锁判断，认为没有必要
 * 需要完成：包头指针传入到发送队列，更新连接对象的时间成员
 */
bool CSocketLogic::_HandlePing(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body) {
    if ((ntohs(lp_pkgheader->pkgLen) - PKG_HEADER_LEN) != LEN_STRUCT_PING) {
        log_error_core(LOG_INFO, 0, "包体长度不合法 实际长度: [%d] 理论长度: [%d] _HandleLogin 返回", lp_pkgheader->pkgLen - PKG_HEADER_LEN, LEN_STRUCT_PING);
        return false;
    }
    
    pthread_t tid = pthread_self();
    CLock lock(&lp_conn->s_connmutex);
    // log_error_core(LOG_INFO, 0, "线程 [%d] _HandlePing fd: [%d]", tid, lp_conn->fd);

    lp_conn->ping_update_time = time(NULL);
    std::shared_ptr<char> send_ptr = std::shared_ptr<char>(new char[MSG_HEADER_LEN + PKG_HEADER_LEN + 1](), [](char* p) { delete[] p; });
    char* send_buf = send_ptr.get();

    // （1）填充消息头
    memcpy(send_buf, lp_msgheader, MSG_HEADER_LEN);
    // （2）填充包头
    LPCOMM_PKG_HEADER p_pkgheader = (LPCOMM_PKG_HEADER)(send_buf + MSG_HEADER_LEN);
    p_pkgheader->msgCode = htons(_CMD_PING);
    p_pkgheader->pkgLen = htons(PKG_HEADER_LEN);
    lp_conn->s_sendlen_suppose = PKG_HEADER_LEN;  // 应当发送的长度，发送线程中校验用

    // （3）无包体，直接赋值 crc32
    p_pkgheader->crc32 = htonl(0);

    MsgSendInQueue(send_ptr);  // 填入发送队列，实际上发送的是包头，长度 8 个字节
    send_buf = nullptr;
    send_ptr = nullptr;
    return true;
}