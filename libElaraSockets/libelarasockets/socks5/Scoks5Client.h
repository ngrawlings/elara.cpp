//
//  Scoks5Client.hpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 18/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Scoks5Client_hpp
#define Scoks5Client_hpp

#include <stdio.h>

#include <libelaracore/memory/ByteArray.h>
#include <libelarasockets/Listener.h>
#include <libelarasockets/Socket.h>

namespace nrcore {

    class Socks5Client : public Socket {
    public:
        Socks5Client(int _fd);
        virtual ~Socks5Client();
        
        void onReceive();
        void onWriteReady();
        
    private:
        ByteArray buffer;
    };

}

#endif /* Scoks5Client_hpp */
