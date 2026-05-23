#include "EpaDbgServer.h"

namespace elara {

EpaDbgServer::EpaDbgServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new EpaDbgService());
    addService(service);
}

EpaDbgServer::~EpaDbgServer() {}

void EpaDbgServer::start(int port, const String &address) {
    String addr(address);
    listen(addr, (unsigned short)port);
    runEventLoop(true);
}

} // namespace elara
