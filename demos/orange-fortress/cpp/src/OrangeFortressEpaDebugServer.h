#ifndef ORANGEFORTRESSEPADEBUGSERVER_H
#define ORANGEFORTRESSEPADEBUGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "OrangeFortressEpaDebugService.h"

namespace elara {
class OrangeFortressEpaDebugServer : public sockets::rpc::json::JsonRPCServer {
public:
    OrangeFortressEpaDebugServer();
    virtual ~OrangeFortressEpaDebugServer();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
