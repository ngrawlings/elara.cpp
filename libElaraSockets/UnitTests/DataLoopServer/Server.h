//
//  Listener.hpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 11/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef DataLoopServer_hpp
#define DataLoopServer_hpp

#include <libelaracore/memory/Ref.h>
#include <libelarasockets/Listener.h>
#include "Client.h"

using namespace elara;

class DataLoopServer : public Listener {
public:
    DataLoopServer(EventBase *ev);
    virtual ~DataLoopServer();
    
protected:
    Ref<Client> client;
    
    void onNewConnection(EventBase *event_base, int fd, unsigned char *addr, int addr_sz);
    
};

#endif /* DataLoopServer_hpp */
