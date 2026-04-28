//
//  Client.hpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 11/06/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef Client_hpp
#define Client_hpp

#include <libelarasockets/Socket.h>
#include <libelaracore/memory/ByteArray.h>

using namespace nrcore;

class Client : public Socket {
public:
    Client();
    Client(int fd);
    virtual ~Client();
    
    void seed(Memory data);
    
protected:
    virtual void onReceive();
    virtual void onWriteReady();
    
private:
    ByteArray queue;
};

#endif /* Client_hpp */
