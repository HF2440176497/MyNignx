
#ifndef C_SOCKETLOGIC_H
#define C_SOCKETLOGIC_H

#include "c_socket.h"

// 消息类型码，应当与客户端统一

#define _CMD_START	                    0  
#define _CMD_PING                       _CMD_START + 1   // 心跳包
#define _CMD_REGISTER 		            _CMD_START + 5   // 注册
#define _CMD_LOGIN 		                _CMD_START + 6   // 登录


class CSocketLogic: public CSocket {
public:
    virtual int ThreadRecvProc(char* msg);
    bool        _HandleRegister(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body);
    bool        _HandleLogin(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body);
    bool        _HandlePing(lp_connection_t lp_conn, LPSTRUC_MSG_HEADER lp_msgheader, LPCOMM_PKG_HEADER lp_pkgheader, char* lp_body);

public:
    CSocketLogic(/* args */);
    virtual ~CSocketLogic();
    virtual bool Initialize();
};



#endif