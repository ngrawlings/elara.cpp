//
//  ClientSocket.cpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 19/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "ClientSocket.h"
#include "Socks5Server.h"

namespace nrcore {
    
    ClientSocket::ClientSocket(Socks5Server *server, Address address, unsigned short port) : Socket() {
        this->server = server;
        this->connect(address, port);
    }
    
    ClientSocket::~ClientSocket() {
        
    }
    
    void ClientSocket::onReceive() {
        if (!buffer.length()) {
            size_t available = this->available();
            
            size_t sz = server->writeBufferSpace();
            
            sz = sz > available ? available : sz;
            Memory data = this->read((int)sz);
            
            int sent = server->send(data.operator char *(), data.length());
            
            if (sent<data.length()) {
                ByteArray ba(data.operator char *(), (int)data.length());
                buffer.append(ba.subBytes(sent));
            }
        } else {
            int sent = server->send(buffer.operator char *(), buffer.length());
            if (sent)
                buffer = buffer.subBytes(sent);
        }
    }
    
    void ClientSocket::onWriteReady() {
        if (buffer.length()) {
            int sent = server->send(buffer.operator char *(), buffer.length());
            buffer = buffer.subBytes(sent);
        }
    }
    
}
