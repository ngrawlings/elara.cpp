//
//  Scoks5Client.cpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 18/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Scoks5Client.h"

namespace nrcore {
    
    Socks5Client::Socks5Client(int _fd) : Socket(_fd) {
        
    }
    
    Socks5Client::~Socks5Client() {
        
    }
    
    void Socks5Client::onReceive() {
        Memory data = this->read(4096);
        
    }
    
    void Socks5Client::onWriteReady() {

    }
    
}
