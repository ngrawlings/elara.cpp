//
//  UsernamePasswordLoader.hpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 26/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#ifndef UsernamePasswordLoader_hpp
#define UsernamePasswordLoader_hpp

#include <stdio.h>

#include <libelarasockets/utils/BlockLoaderBase.h>
#include <libelaracore/memory/String.h>

namespace elara {
    
    class UsernamePasswordLoader : public BlockLoaderBase {
    public:
        UsernamePasswordLoader();
        virtual ~UsernamePasswordLoader();
        
        bool appendAvailableData(Socket *socket);
        
        String getUsername();
        String getPassword();
        
    protected:
        typedef enum {
            USERNAME_LEN,
            USERNAME,
            PASSWORD_LEN,
            PASSWORD,
            FINISHED
        } STATE;
        
        STATE state;
        
        int username_len;
        int password_len;
    };
    
}

#endif /* UsernamePasswordLoader_hpp */
