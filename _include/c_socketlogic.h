
#ifndef C_SOCKETLOGIC_H
#define C_SOCKETLOGIC_H

#include "c_socket.h"

class CSocketLogic: public CSocket {

public:
    virtual int ThreadRecvProc(char* msg);
    bool _HandleRegister(char* msg);
    bool _HandleLogin(char* msg);
    
public:
    CSocketLogic(/* args */);
    ~CSocketLogic();
};



#endif