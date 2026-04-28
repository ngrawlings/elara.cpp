//
//  FileStream.hpp
//  NrIO
//
//  Created by Nyhl Rawlings on 10/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#ifndef FileStream_hpp
#define FileStream_hpp

#include "Stream.h"

#include <iostream>
#include <fstream>

#include <libelaracore/memory/String.h>

namespace nrcore {

    class FileStream : public Stream {
    public:
        FileStream(int fd);
        FileStream(String filename);
        FileStream(const FileStream& fs);
        virtual ~FileStream();
        
        void seek(off_t position);
        void seekEOF();
        off_t position();
        
        ssize_t write(const char* buf, size_t sz);
        ssize_t read(char* buf, size_t sz);
        
        off_t getfileSize();
        
        void flush();
        void close();
        
    protected:
        FILE* file;
    };
    
}

#endif /* FileStream_hpp */
