//
//  Listener.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 11/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Server.h"

DataLoopServer::DataLoopServer(EventBase *ev) : Listener(12123, LISTENER_OPTS_IPV4 | LISTENER_OPTS_IPV4_REQUIRED, ev){

}

DataLoopServer::~DataLoopServer() {
    
}

void DataLoopServer::onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz) {
    if (client.getPtr())
        client.getPtr()->close();
    
    client = Ref<Client>(new Client(fd));
    client.getPtr()->seed(Memory::getRandomBytes(4096));
}
