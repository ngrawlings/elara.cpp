//
//  Keccak256.hpp
//  libElaraEncryption
//
//  Created by Nyhl Rawlings on 17/10/2019.
//  Copyright © 2019 Nyhl Rawlings. All rights reserved.
//

#ifndef Keccak256_hpp
#define Keccak256_hpp

#include <stdio.h>
#include "Hash.h"

#include "keccak256/keccak256.h"

namespace elara {

    class Keccak256 : public Hash {
    public:
        Keccak256() {
            keccak_init(&ctx);
        }
        
        void update(const Memory& mem) {
            keccak_update(&ctx, (const uint8_t *)((Memory&)mem).operator char *(), (uint32_t)mem.length());
        }
        
        void final () {
            keccak_final(&ctx, hash);
        }
        
        size_t length() {
            return 32;
        }
        
        unsigned char* get() {
            return hash;
        }
        
    protected:
        SHA3_CTX ctx;
        unsigned char hash[32];
    };

}

#endif /* Keccak256_hpp */
