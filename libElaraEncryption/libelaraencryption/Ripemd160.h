//
//  Ripemd160.hpp
//  libElaraEncryption
//
//  Created by Nyhl Rawlings on 17/10/2019.
//  Copyright © 2019 Nyhl Rawlings. All rights reserved.
//

#ifndef Ripemd160_hpp
#define Ripemd160_hpp

#include <stdio.h>
#include "Hash.h"

#include "ripemd160/ripemd160.h"

namespace nrcore {

    class Ripemd160 : public Hash {
    public:
        Ripemd160() {
            ripemd160_Init(&ctx);
        }
        
        void update(const Memory& mem) {
            ripemd160_Update(&ctx, (const uint8_t *)((Memory&)mem).operator char *(), (uint32_t)mem.length());
        }
        
        void final () {
            ripemd160_Final(&ctx, hash);
        }
        
        size_t length() {
            return RIPEMD160_DIGEST_LENGTH;
        }
        
        unsigned char* get() {
            return hash;
        }
        
    protected:
        RIPEMD160_CTX ctx;
        unsigned char hash[RIPEMD160_BLOCK_LENGTH];
    };

}

#endif /* Ripemd160_hpp */
