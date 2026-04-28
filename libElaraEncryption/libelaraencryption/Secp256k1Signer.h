//
//  Secp256k1.hpp
//  libElaraEncryption
//
//  Created by Nyhl Rawlings on 06/09/2018.
//  Copyright © 2018 Nyhl Rawlings. All rights reserved.
//

#ifndef Secp256k1_hpp
#define Secp256k1_hpp

#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Memory.h>
#include <libelaraencryption/base/Signer.h>

#include <secp256k1.h>

namespace elara {
    
    class Secp256k1Signer : public Signer {
    public:
        Secp256k1Signer();
        Secp256k1Signer(const Memory& private_key);
        virtual ~Secp256k1Signer();
        
        bool setPublicKey(Memory& public_key);
        Memory getPublicKey(bool compressed);
        
        Memory sign(Memory hash);
        bool verify(Memory hash, Memory signiture);
        
        int getBlockSize();
        
    private:
        secp256k1_context *ctx;
        Memory private_key;
        secp256k1_pubkey pub_key;
        
    };
    
}

#endif /* Secp256k1_hpp */
