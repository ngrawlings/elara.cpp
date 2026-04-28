//
//  ClientSocket.hpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 19/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef ClientSocket_hpp
#define ClientSocket_hpp

#include <stdio.h>

#include <libelaracore/memory/ByteArray.h>
#include <libelarasockets/Socket.h>

namespace nrcore {
    
    class Socks5Server;

    class ClientSocket : public Socket {
    public:
        ClientSocket(Socks5Server *server, Address address, unsigned short port);
        virtual ~ClientSocket();
        
    protected:
        
        void onReceive();
        void onWriteReady();
        
    private:
        Socks5Server *server;
        ByteArray buffer;
    };
    
}

#endif /* ClientSocket_hpp */
