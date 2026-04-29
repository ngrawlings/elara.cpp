//
//  RingBuffer.h
//  libElaraCore
//
//  Created by Nyhl on 03/11/14.
//  Copyright (c) 2014 N G Rawlings. All rights reserved.
//

#ifndef __libElaraCore__RingBuffer__
#define __libElaraCore__RingBuffer__

#include <libelaracore/types.h>
#include "RefArray.h"
#include "Memory.h"

namespace elara {

    class RingBuffer {
    public:
        RingBuffer(size_t size);
        virtual ~RingBuffer();
        
        size_t size();
        size_t length();
        size_t freeSpace();
        
        size_t append(const char *data, size_t len);
        Memory fetch(size_t len);
        Memory getDataUntilEnd();
        void drop(int len);
        
    private:
        size_t _size;    // Size of buffer
        size_t read_cursor;  // Start of data
        size_t write_cursor; // Start of Append position
        size_t _length;  // Amount of data in buffer
        
        char *buffer;
    };
    
}

namespace elara {
namespace core {
namespace memory {
    using ::elara::RingBuffer;
}
}
}

#endif /* defined(__libElaraCore__RingBuffer__) */
