#ifndef ELARAOSEPADEBUGSERVER_H
#define ELARAOSEPADEBUGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "ElaraOsEpaDebugService.h"

namespace elara {
class ElaraOsEpaDebugServer : public sockets::rpc::json::JsonRPCServer {
public:
    ElaraOsEpaDebugServer();
    virtual ~ElaraOsEpaDebugServer();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
