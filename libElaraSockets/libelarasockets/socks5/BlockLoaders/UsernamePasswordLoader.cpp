//
//  UsernamePasswordLoader.cpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 26/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "UsernamePasswordLoader.h"

#include <libelarasockets/Socket.h>
#include <libelaracore/exception/Exception.h>

namespace elara {
    
    UsernamePasswordLoader::UsernamePasswordLoader() : state(USERNAME_LEN) {
        username_len = -1;
        password_len = -1;
    }
    
    UsernamePasswordLoader::~UsernamePasswordLoader() {
        
    }
    
    bool UsernamePasswordLoader::appendAvailableData(Socket *socket) {
        switch (state) {
            case USERNAME_LEN:
                if (socket->available() >= 1) {
                    Memory r = socket->read(username_len);
                    buffer.append(r.getPtr(), (int)r.length());
                    
                    username_len = socket->read(1)[0];
                    
                    state = USERNAME;
                }
                return false;
                
            case USERNAME:
                {
                    Memory r = socket->read(username_len);
                    buffer.append(r.getPtr(), (int)r.length());
                    
                    if (buffer.length() <= username_len+1)
                        state = PASSWORD_LEN;
                }
                return false;
                
            case PASSWORD_LEN:
                if (socket->available() >= 1) {
                    Memory r = socket->read(username_len);
                    buffer.append(r.getPtr(), (int)r.length());
                    
                    password_len = socket->read(1)[0];
                    
                    state = PASSWORD;
                }
                return false;
                
            case PASSWORD:
                {
                    Memory r = socket->read(username_len);
                    buffer.append(r.getPtr(), (int)r.length());
                    
                    if (buffer.length() <= username_len+1) {
                        state = FINISHED;
                        return true;
                    }
                    
                }
                break;
                
            default:
                break;
        }
        
        return true;
    }
    
    String UsernamePasswordLoader::getUsername() {
        if (state != FINISHED)
            throw Exception(-1, "Not ready");
        
        return String(buffer.subBytes(1, username_len), username_len);
    }
    
    String UsernamePasswordLoader::getPassword() {
        if (state != FINISHED)
            throw Exception(-1, "Not ready");
        
        return String(buffer.subBytes(2+username_len, password_len), password_len);
    }
    
}
