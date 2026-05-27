#ifndef EPASIGNALLABEPADEBUGSERVER_H
#define EPASIGNALLABEPADEBUGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "EpaSignalLabEpaDebugService.h"

namespace elara {
class EpaSignalLabEpaDebugServer : public sockets::rpc::json::JsonRPCServer {
public:
    EpaSignalLabEpaDebugServer();
    virtual ~EpaSignalLabEpaDebugServer();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
