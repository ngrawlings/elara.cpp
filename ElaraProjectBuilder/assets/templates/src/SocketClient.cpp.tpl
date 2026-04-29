#include "%SocketClientName%.h"

#include <libelarasockets/Address.h>
#include <libelaracore/memory/Memory.h>
#include <stdio.h>

%SocketClientName%::%SocketClientName%(elara::String address, int port) : Socket() {
    if (!connect(elara::Address(elara::Address::ADDR, address.operator char *()), port)) {
        printf("Failed to connect to %s:%u\n", address.operator char *(), (unsigned int)port);
    }
}

%SocketClientName%::~%SocketClientName%() {
}

void %SocketClientName%::sendLine(elara::String text) {
    text += elara::String("\n");
    send(text.operator char *(), (size_t)text.length());
}

void %SocketClientName%::onReceive() {
    elara::Memory data = read((int)available());
    if (data.length()) {
        fwrite(data.operator char *(), 1, (size_t)data.length(), stdout);
        fflush(stdout);
    }
}

void %SocketClientName%::onWriteReady() {
}
