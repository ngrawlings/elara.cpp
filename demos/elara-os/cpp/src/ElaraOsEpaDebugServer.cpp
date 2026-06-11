#include "ElaraOsEpaDebugServer.h"
#include <libelarasockets/Socket.h>

namespace elara {
ElaraOsEpaDebugServer::ElaraOsEpaDebugServer() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new ElaraOsEpaDebugService());
    addService(service);
}
ElaraOsEpaDebugServer::~ElaraOsEpaDebugServer() {}
void ElaraOsEpaDebugServer::start(int port, String address) {
    listen(address, (unsigned short)port);
    Socket::init(getEventBase());
    runEventLoop(false);
}
}
