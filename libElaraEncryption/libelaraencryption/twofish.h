#ifndef TWOFISH_H
#define TWOFISH_H

#include "base/Cipher.h"
#include "twofish/twofish2.h"

namespace nrcore {

    class TwoFish : Cipher
    {
    public:
        TwoFish(const Memory &key, const Memory &iv);

        void setKey(const Memory &key, const Memory &iv);

        Memory encrypt(const char* buf, int len);
        Memory decrypt(const char* buf, int len);
        
        int getBlockSize();
        
    private:
        cipherInstance c_inst;

        keyInstance k_encrypt, k_decrypt;
    };
    
};

#endif // TWOFISH_H
