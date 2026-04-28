//
//  Secp256k1.cpp
//  libElaraEncryption
//
//  Created by Nyhl Rawlings on 06/09/2018.
//  Copyright © 2018 Nyhl Rawlings. All rights reserved.
//

#include "Secp256k1Signer.h"

namespace elara {
    
    Secp256k1Signer::Secp256k1Signer() {
        this->ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    }
    
    Secp256k1Signer::Secp256k1Signer(const Memory& private_key) {
        this->private_key = private_key;
        this->ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    
        if (!secp256k1_ec_pubkey_create(this->ctx, &this->pub_key, (const unsigned char*)this->private_key.getPtr())) {
            throw "Invalid Key";
        }
    }
    
    Secp256k1Signer::~Secp256k1Signer() {
        secp256k1_context_destroy(this->ctx);
    }
    
    bool Secp256k1Signer::setPublicKey(Memory& public_key) {
        return secp256k1_ec_pubkey_parse(this->ctx, &this->pub_key, (const unsigned char*)public_key.getPtr(), public_key.length()) == 1;
    }
    
    Memory Secp256k1Signer::getPublicKey(bool compressed) {
        size_t sz = 65;
        
        if (compressed) {
            Memory ret(33);
            sz = 33;
            secp256k1_ec_pubkey_serialize(this->ctx, (unsigned char*)ret.getPtr(), &sz, &this->pub_key, SECP256K1_EC_COMPRESSED);
            return ret;
        }
        
        Memory ret(65);
        secp256k1_ec_pubkey_serialize(this->ctx, (unsigned char*)ret.getPtr(), &sz, &this->pub_key, SECP256K1_EC_UNCOMPRESSED);
        return ret;
    }
    
    Memory Secp256k1Signer::sign(Memory block) {
        secp256k1_ecdsa_signature sig;
        
        if (secp256k1_ecdsa_sign(this->ctx, &sig, (const unsigned char*)block.getPtr(), (const unsigned char*)this->private_key.operator char *(), 0, 0) == 1) {
            Memory ret(64);
            secp256k1_ecdsa_signature_serialize_compact(this->ctx, (unsigned char*)ret.getPtr(), &sig);
            return ret;
        }
        
        throw "Faield to sign";
    }
    
    bool Secp256k1Signer::verify(Memory block, Memory signiture) {
        secp256k1_ecdsa_signature sig;
        
        if (secp256k1_ecdsa_signature_parse_compact(this->ctx, &sig, (const unsigned char*)signiture.getPtr()) == 1) {
            return secp256k1_ecdsa_verify(this->ctx, &sig, (const unsigned char*)block.getPtr(), &this->pub_key) == 1;
        }
        
        return false;
    }
    
    int Secp256k1Signer::getBlockSize() {
        return 32;
    }
    
}
