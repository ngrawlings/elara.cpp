//
//  File.hpp
//  NrIO
//
//  Created by Nyhl Rawlings on 15/01/2020.
//  Copyright © 2020 Liquidsoft Studio. All rights reserved.
//

#ifndef elara_File_hpp
#define elara_File_hpp

#include <stdio.h>
#define FILE_BUFFER_SIZE        4096

#include <stdio.h>

#include <libelaracore/memory/Memory.h>

namespace elara {
    
    class File : public Memory {
    public:
        File(const char *path);
        File(const char *path, bool create_if_missing);
        virtual ~File();
        
        char& operator [](size_t index);
        Memory getMemory() const;
        Memory getSubBytes(size_t offset, size_t length) const;
        void write(size_t offset, const char* data, size_t length);
        Memory read(size_t offset, size_t length) const;
        virtual size_t length() const;
        void setFileUpdating(bool val);
        void grow(size_t size);
        void truncate();
        int fileno();
        void flush();
        
    private:
        FILE* fp;
        size_t sz;
        size_t fill;
        
        size_t offset;
        
        String path;
        
        bool update_file;
        
        void updateFileSize();
        
        void updateFile();
        void writeToFile(size_t offset, const char* data, size_t length);
    };
    
};

#endif /* elara_File_hpp */
