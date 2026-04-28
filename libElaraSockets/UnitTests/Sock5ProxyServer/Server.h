//
//  Listener.hpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 07/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Server_hpp
#define Server_hpp

#include <libelaracore/memory/Array.h>
#include <libelarasockets/Listener.h>
#include <libelarasockets/Socket.h>

#include <libelarasockets/socks5/Socks5Server.h>

using namespace nrcore;

class Server : public Listener, Socket::CallbackInterface {
public:
    Server();
    virtual ~Server();
    
    void onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
    
protected:
    Array< Ref<Socks5Server> > connections;
    
    void onConnected(Socket *socket);
    void onClosed(Socket *socket);
    void onDestroyed(Socket *socket);
    
};

#endif /* Server_hpp */
