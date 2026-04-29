#ifndef %RpcServerName%_h
#define %RpcServerName%_h

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "%RpcServiceName%.h"

class %RpcServerName% : public elara::sockets::rpc::json::JsonRPCServer {
public:
    %RpcServerName%();
    virtual ~%RpcServerName%();
    void start(int port, elara::String address);

private:
    elara::Ref<elara::sockets::rpc::json::JsonRPCService> service;
};

#endif
