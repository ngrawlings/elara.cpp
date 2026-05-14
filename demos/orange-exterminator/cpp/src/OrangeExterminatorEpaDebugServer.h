#ifndef ORANGEEXTERMINATOREPADEBUGSERVER_H
#define ORANGEEXTERMINATOREPADEBUGSERVER_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "OrangeExterminatorEpaDebugService.h"

namespace elara {
class OrangeExterminatorEpaDebugServer : public sockets::rpc::json::JsonRPCServer {
public:
    OrangeExterminatorEpaDebugServer();
    virtual ~OrangeExterminatorEpaDebugServer();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
