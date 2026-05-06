>>>>>>>>>>main>>>>RPC_SERVER_NAME>RPC_SERVICE_NAME
#ifndef %RPC_SERVER_NAME%_h
#define %RPC_SERVER_NAME%_h

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "%RPC_SERVICE_NAME%.h"

class %RPC_SERVER_NAME% : public elara::sockets::rpc::json::JsonRPCServer {
public:
    %RPC_SERVER_NAME%();
    virtual ~%RPC_SERVER_NAME%();
    void start(int port, elara::String address);

private:
    elara::Ref<elara::sockets::rpc::json::JsonRPCService> service;
};

#endif
<<<<<<<<<<main
