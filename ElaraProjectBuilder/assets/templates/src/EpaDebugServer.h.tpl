>>>>>>>>>>main>>>>SERVER_NAME>SERVER_NAME_UPPER>SERVICE_NAME
#ifndef %SERVER_NAME_UPPER%_H
#define %SERVER_NAME_UPPER%_H

#include <libelarasockets/rpc/json/JsonRPCServer.h>
#include <libelaracore/memory/Ref.h>
#include "%SERVICE_NAME%.h"

namespace elara {
class %SERVER_NAME% : public sockets::rpc::json::JsonRPCServer {
public:
    %SERVER_NAME%();
    virtual ~%SERVER_NAME%();
    void start(int port, String address);
private:
    Ref<sockets::rpc::json::JsonRPCService> service;
};
}

#endif
<<<<<<<<<<main
