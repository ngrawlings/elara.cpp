>>>>>>>>>>main>>>>RPC_SERVER_NAME>RPC_SERVICE_NAME
#include "%RPC_SERVER_NAME%.h"

%RPC_SERVER_NAME%::%RPC_SERVER_NAME%() {
    service = elara::Ref<elara::sockets::rpc::json::JsonRPCService>(new %RPC_SERVICE_NAME%());
    addService(service);
}

%RPC_SERVER_NAME%::~%RPC_SERVER_NAME%() {
}

void %RPC_SERVER_NAME%::start(int port, elara::String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
<<<<<<<<<<main
