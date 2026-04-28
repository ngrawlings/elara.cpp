//
//  main.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 06/09/2018.
//  Copyright © 2018 Nyhl Rawlings. All rights reserved.
//

#include <iostream>

#include <libelaracore/memory/Memory.h>

#include <libelaradebug/UnitTests.h>
#include "../libelaraencryption/Secp256k1Signer.h"
#include <libelaracore/utils/ByteUtils.h>

using namespace elara;

bool testSecp256k1() {
    Ref<char> bytes = ByteUtils::getRandomBytes(32);
    Memory private_key(bytes.getPtr(), 32);
    bytes.release();
    
    Secp256k1Signer secp(private_key);
    Memory compressed_key = secp.getPublicKey(true);
    Memory pub_key = secp.getPublicKey(false);
    
    printf("Compressed Public Key: %s\r\n", (char*)compressed_key.toHex(false));
    printf("Uncompressed Public Key: %s\r\n", (char*)pub_key.toHex(false));
    
    Memory sig = secp.sign(Memory("12345678901234567890123456789012", 32));
    printf("%s\n", (char*)sig.toHex(false));
    
    if (secp.verify(Memory("12345678901234567890123456789012", 32), sig)) {
        printf("Signiture passed");
        return true;
    } else {
        printf("Signiture failed");
    }
    
    return false;
}

int main(int argc, const char * argv[]) {
    UnitTests tests;
    
    tests.addTest("testSecp256k1", testSecp256k1);
    tests.run();
    
    return 0;
}
