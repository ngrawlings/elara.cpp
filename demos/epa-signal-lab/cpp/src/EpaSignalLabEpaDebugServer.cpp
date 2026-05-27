#include "EpaSignalLabEpaDebugServer.h"

namespace elara {
EpaSignalLabEpaDebugServer::EpaSignalLabEpaDebugServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new EpaSignalLabEpaDebugService());
    addService(service);
}
EpaSignalLabEpaDebugServer::~EpaSignalLabEpaDebugServer() {}
void EpaSignalLabEpaDebugServer::start(int port, String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
}
