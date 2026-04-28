//
//  BlockLoaderBase.cpp
//  NrSockets
//
//  Created by Nyhl Rawlings on 26/04/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "BlockLoaderBase.h"

namespace nrcore {
    
    BlockLoaderBase::BlockLoaderBase() {
        
    }
    
    BlockLoaderBase::~BlockLoaderBase() {
        
    }

    ByteArray BlockLoaderBase::getBuffer() {
        return buffer;
    }
    
}
