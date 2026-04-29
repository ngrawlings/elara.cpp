#include "%RpcServerName%.h"

%RpcServerName%::%RpcServerName%() {
    service = elara::Ref<elara::sockets::rpc::json::JsonRPCService>(new %RpcServiceName%());
    addService(service);
}

%RpcServerName%::~%RpcServerName%() {
}

void %RpcServerName%::start(int port, elara::String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
