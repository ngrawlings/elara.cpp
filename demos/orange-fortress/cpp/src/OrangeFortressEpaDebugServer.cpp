#include "OrangeFortressEpaDebugServer.h"

namespace elara {
OrangeFortressEpaDebugServer::OrangeFortressEpaDebugServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new OrangeFortressEpaDebugService());
    addService(service);
}
OrangeFortressEpaDebugServer::~OrangeFortressEpaDebugServer() {}
void OrangeFortressEpaDebugServer::start(int port, String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
}
