#include "%SocketServerName%.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

%SocketServerName%::%SocketServerName%() {
}

%SocketServerName%::~%SocketServerName%() {
}

void %SocketServerName%::start(int port, elara::String address) {
    unsigned int ipv4_interface = INADDR_ANY;
    if (address.length() && address != elara::String("0.0.0.0") && address != elara::String("*")) {
        ipv4_interface = inet_addr(address.operator char *());
    }
    listen(port, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED, ipv4_interface);
    runEventLoop(true);
}

void %SocketServerName%::onNewConnection(elara::EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {
    (void)event_base;
    (void)addr;
    (void)addr_sz;
    send(fd, "%ProjectName% accepted your connection.\n", strlen("%ProjectName% accepted your connection.\n"), 0);
    ::close(fd);
    printf("Accepted and closed a socket client.\n");
}
