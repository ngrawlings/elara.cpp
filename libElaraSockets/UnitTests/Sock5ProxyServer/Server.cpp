//
//  Listener.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 07/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Server.h"

#include <libelarasockets/Socket.h>

Server::Server() : Listener(1080, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED) {
    Socket::init(getEventBase());
}

Server::~Server() {
    
}

void Server::onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {
    Ref<Socks5Server> ss = Ref<Socks5Server>(new Socks5Server(fd));
    ss.getPtr()->setCallbackInterface(this);
    ss.getPtr()->setPreferedAuthOrder(Memory((char*){0x00}, 1));
    
    connections.push(ss);
}

void Server::onConnected(Socket *socket) {
    
}

void Server::onClosed(Socket *socket) {
    int length = (int)connections.length();
    for (int i=0; i<length; i++) {
        if (socket == connections[i].getPtr()) {
            connections.remove(i);
            delete socket;
        }
    }
}

void Server::onDestroyed(Socket *socket) {
    printf("Socket destroyed\n");
}
