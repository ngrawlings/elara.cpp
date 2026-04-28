//
//  FileStream.cpp
//  NrIO
//
//  Created by Nyhl Rawlings on 10/09/2018.
//  Copyright © 2018 Liquidsoft Studio. All rights reserved.
//

#include "FileStream.h"



namespace nrcore {
    
    FileStream::FileStream(int fd) : Stream(fd), file(0) {
        
    }
    
    FileStream::FileStream(String filename) : Stream(0), file(0) {
        file = fopen ((char*)filename, "r+");
        if (!file)
            file = fopen ((char*)filename, "w+");
        
        fd = fileno(file);
    }
    
    FileStream::FileStream(const FileStream& fs) : Stream(fs), file(fs.file) {
    }
    
    FileStream::~FileStream() {
    }
    
    void FileStream::seek(off_t position) {
        lseek(fd, position, SEEK_SET);
    }
    
    void FileStream::seekEOF() {
        lseek(fd, 0, SEEK_END);
    }
    
    off_t FileStream::position() {
        return lseek(fd, 0, SEEK_CUR);
    }
    
    ssize_t FileStream::write(const char* buf, size_t sz) {
        return fwrite(buf, sz, 1, file);
    }
    
    ssize_t FileStream::read(char* buf, size_t sz) {
        return fread(buf, sz, 1, file);
    }
    
    off_t FileStream::getfileSize() {
        off_t position = lseek(fd, 0, SEEK_CUR);
        off_t end = lseek(fd, 0, SEEK_END);
        lseek(fd, position, SEEK_SET);
        
        if (end == -1)
            end = 0;
        
        return end;
    }
    
    void FileStream::flush() {
        if (file) {
            fflush(file);
        }
    }
    
    void FileStream::close() {
        if (file) {
            fclose(file);
            file = 0;
        }
    }
    
    
    
}
