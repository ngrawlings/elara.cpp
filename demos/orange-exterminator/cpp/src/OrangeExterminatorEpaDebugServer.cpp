#include "OrangeExterminatorEpaDebugServer.h"

namespace elara {
OrangeExterminatorEpaDebugServer::OrangeExterminatorEpaDebugServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new OrangeExterminatorEpaDebugService());
    addService(service);
}
OrangeExterminatorEpaDebugServer::~OrangeExterminatorEpaDebugServer() {}
void OrangeExterminatorEpaDebugServer::start(int port, String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
}
