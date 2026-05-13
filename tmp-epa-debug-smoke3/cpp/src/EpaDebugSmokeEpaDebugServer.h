#ifndef EPADEBUGSMOKEEPADEBUGSERVER_H
#define EPADEBUGSMOKEEPADEBUGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "EpaDebugSmokeEpaDebugService.h"

namespace elara {
class EpaDebugSmokeEpaDebugServer : public sockets::rpc::json::JsonRPCServer {
public:
    EpaDebugSmokeEpaDebugServer();
    virtual ~EpaDebugSmokeEpaDebugServer();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
