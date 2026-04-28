//
//  Signer.hpp
//  libElaraEncryption
//
//  Created by Nyhl Rawlings on 08/09/2018.
//  Copyright © 2018 Nyhl Rawlings. All rights reserved.
//

#ifndef Signer_hpp
#define Signer_hpp

#include <libelaracore/memory/Memory.h>

namespace nrcore {
    
    class Signer {
    public:
        virtual ~Signer() {}

        virtual Memory sign(Memory hash) = 0;
        virtual bool verify(Memory hash, Memory signiture) = 0;
        
        virtual int getBlockSize() = 0;
    };
    
};

#endif /* Signer_hpp */
