//
//  Client.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 11/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Client.h"
#include <libelarasockets/Address.h>
#include <libelaracore/memory/Memory.h>

Client::Client() : Socket(){
    this->connect(Address(Address::ADDR, "127.0.0.1"), 12123);
    seed(Memory::getRandomBytes(4096));
}

Client::Client(int fd) : Socket(fd) {
    
}

Client::~Client() {
}

void Client::seed(Memory data) {
    printf("seeding data\n");
    this->send(data);
}

void Client::onReceive() {
    Memory read = this->read((int)this->writeBufferSpace());
    int sent = this->send(read);
    
    if (read.length() > sent) {
        ByteArray b(read.operator char *(), (int)read.length());
        queue.append(b.subBytes(sent));
    }
}

void Client::onWriteReady() {
    if (queue.length()) {
        int sent = send(queue);
        queue = queue.subBytes(sent);
    }
}
