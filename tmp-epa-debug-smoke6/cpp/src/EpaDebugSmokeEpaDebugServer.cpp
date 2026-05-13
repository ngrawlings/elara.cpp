#include "EpaDebugSmokeEpaDebugServer.h"

namespace elara {
EpaDebugSmokeEpaDebugServer::EpaDebugSmokeEpaDebugServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new EpaDebugSmokeEpaDebugService());
    addService(service);
}
EpaDebugSmokeEpaDebugServer::~EpaDebugSmokeEpaDebugServer() {}
void EpaDebugSmokeEpaDebugServer::start(int port, String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
}
