//
//  File.cpp
//  NrIO
//
//  Created by Nyhl Rawlings on 15/01/2020.
//  Copyright © 2020 Liquidsoft Studio. All rights reserved.
//

#include "File.h"

namespace nrcore {

    File::File(const char *path) : Memory(FILE_BUFFER_SIZE), fill(0), offset(0), update_file(false)  {
        this->path = path;
        
        fp = fopen(path, "r+");
        if (!fp) {
            fp = fopen(path, "w+");
            if (!fp)
                throw "Failed to open";
        }
        
        updateFileSize();
        
        fill = fread(buffer.getPtr(), 1, FILE_BUFFER_SIZE, fp);
    }
    
    File::~File() {
        if (update_file)
            updateFile();
        
        if (fp)
            fclose(fp);
    }
    
    char& File::operator [](size_t index) {
        if (index>=sz)
            throw "Index Out Of Range";
        
        if (fill && index >= offset && index < offset+fill)
            return Memory::operator [](index-offset);
        
        if (fill && update_file)
            updateFile();
        
        offset = index-(index%FILE_BUFFER_SIZE);
        fseek(fp, offset, SEEK_SET);
        fill = fread(buffer.getPtr(), 1, FILE_BUFFER_SIZE, fp);
        return Memory::operator [](index-offset);
    }
    
    
    Memory File::getMemory() const {
        char *buf = new char[sz];
        fseek(fp, 0L, SEEK_SET);
        size_t len = fread(buf, 1, sz, fp);
        
        Memory mem(buf, len);
        
        delete[] buf;
        
        return mem;
    }
    
    Memory File::getSubBytes(size_t offset, size_t length) const {
        char *buf = new char[length];
        fseek(fp, offset, SEEK_SET);
        length = fread(buf, 1, length, fp);
        
        Memory mem(buf, length);
        
        delete[] buf;
        
        return mem;
    }
    
    void File::write(size_t offset, const char* data, size_t length) {
        size_t len = 0;
        if (update_file) {
            if (offset < this->offset) { // copy preceeding bytes
                len = (this->offset - offset > length) ? length : this->offset - offset;
                writeToFile(offset, data, len);
                offset += len;
                data += len;
                length -= len;
            }
            
            if (length) {
                if (offset >= this->offset && offset < this->offset+fill) {
                    size_t coffset = this->offset - offset;
                    len = (fill-coffset) < length ? fill-coffset : length;
                    char *buf = &buffer.getPtr()[coffset];
                    memcpy(buf, data, len);
                    
                    offset += len;
                    data += len;
                    length -= len;
                }
            }
            
            if (length) {
                writeToFile(offset, data, length);
                updateFileSize();
            }
            
        } else {
            writeToFile(offset, data, length);
            updateFileSize();
        }
    }

    Memory File::read(size_t offset, size_t length) const {
        Memory buffer(length);
        
        fseek(fp, offset, SEEK_SET);
        size_t fill = fread(buffer.getPtr(), 1, length, fp);
        fseek(fp, this->offset, SEEK_SET);
        
        return Memory(buffer.operator char *(), fill);
    }
    
    size_t File::length() const {
        return sz;
    }
    
    void File::setFileUpdating(bool val) {
        update_file = val;
    }
    
    void File::grow(size_t size) {
        char byte = 0;
        fseek(fp, 0, SEEK_END);
        
        sz += size;
        
        while(size--)
            fwrite(&byte, 1, 1, fp);
    }
    
    void File::truncate() {
        if (fp) {
            offset = 0;
            fp = freopen(NULL, "w+", fp);
        }
    }
    
    int File::fileno() {
        return ::fileno(fp);
    }

    void File::updateFileSize() {
        fseek(fp, 0L, SEEK_END);
        sz = ftell(fp);
        fseek(fp, 0L, SEEK_SET);
    }
    
    void File::updateFile(){
        fseek(fp, offset, SEEK_SET);
        size_t written = 0;
        while(written < fill)
            fwrite(&buffer.getPtr()[written], 1, fill, fp);
        fflush(fp);
    }
    
    void File::writeToFile(size_t offset, const char* data, size_t length) {
        fseek(fp, offset, SEEK_SET);
        size_t written = 0;
        while(written < length)
            written += fwrite(&data[written], 1, length, fp);
        fflush(fp);
    }

}
