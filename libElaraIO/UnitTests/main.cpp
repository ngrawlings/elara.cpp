//
//  main.cpp
//  UnitTests
//
//  Created by Nyhl Rawlings on 10/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include <iostream>
#include "../libelaraio/IndexedDataStore.h"

#include <libelaracore/memory/ByteArray.h>

using namespace nrcore;

int main(int argc, const char * argv[]) {
    IndexedDataStore *data = new IndexedDataStore("/media/mint/9d008129-b444-490f-a90d-d3fc4a808f93/output/state.dat");
    
    try {
        Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file1 = data->getFile(Memory("block", 5));
        unsigned long long flen = data->getFileSize(file1);
        printf("-> %llu\n", flen);
    } catch (const char * e) {
        printf("%s\n", e);
    }

    return 0;
}
