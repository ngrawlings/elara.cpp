>>>>>>>>>>main>>>>SERVER_NAME>SERVICE_NAME
#include "%SERVER_NAME%.h"

namespace elara {
%SERVER_NAME%::%SERVER_NAME%() {
    service = Ref<sockets::rpc::json::JsonRPCService>(new %SERVICE_NAME%());
    addService(service);
}
%SERVER_NAME%::~%SERVER_NAME%() {}
void %SERVER_NAME%::start(int port, String address) {
    listen(address, (unsigned short)port);
    runEventLoop(true);
}
}
<<<<<<<<<<main
