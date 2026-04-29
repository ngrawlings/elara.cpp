//
//  ByteArray.cpp
//  libElaraCore
//
//  Created by Nyhl Rawlings on 9/24/13.
//  Copyright (c) 2013 N G Rawlings. All rights reserved.
//

#include "ByteArray.h"

#include <string.h>
#include "LinkedList.h"
#include "Array.h"

#define GET_HEX_HIGH_CHAR(x) ((x>>4)&0x0F)<10 ? ((x>>4)&0x0F)+48 : ((x>>4)&0x0F)+55;
#define GET_HEX_LOW_CHAR(x) (x&0x0F)<10 ? (x&0x0F)+48 : (x&0x0F)+55;

namespace elara {
    
    ByteArray::ByteArray(const void *bytes, int len) : buffer(0), size(0), _length(0) {
        allocateBlock(len);
        _length = len;
        if (len)
            memcpy(buffer, bytes, len);
    }

    ByteArray::ByteArray(const Memory &bytes) : buffer(0), size(0), _length(0) {
        int len = (int)bytes.length();
        
        allocateBlock(len);
        _length = len;
        if (len)
            memcpy(buffer, bytes.getMemory().getPtr(), len);
    }
    
    ByteArray::ByteArray(const ByteArray &bytes) : buffer(0), size(0), _length(0) {
        allocateBlock(bytes._length);
        _length = bytes._length;
        memcpy(buffer, bytes.buffer, _length);
    }
    
    ByteArray::~ByteArray() {
        if (buffer)
            delete [] buffer;
    }
    
    void ByteArray::allocateBlock(size_t min_sz) {
        size_t mask = -16;
        size_t newsize = ((min_sz)&mask)+16;
        
        if (size >= newsize)
            return;
        
        size = newsize;
        char *block = new char[newsize];
        
        if (_length && buffer != 0)
            memcpy(block, buffer, _length);
        
        if (buffer)
            delete [] buffer;
        
        buffer = block;
    }
    
    void ByteArray::clear() {
        _length = 0;
    }
    
    void ByteArray::append(Memory mem) {
        append(mem.operator char *(), (int)mem.length());
    }
    
    void ByteArray::append(const ByteArray &bytes) {
        if (size <= _length+bytes._length)
            allocateBlock(_length+bytes._length);
        memcpy(&buffer[_length], bytes.buffer, bytes._length);
        _length += bytes._length;
    }
    
    void ByteArray::append(const void* bytes, int len) {
        if (size <= _length+len)
            allocateBlock(_length+len);
        
        if (bytes)
            memcpy(&buffer[_length], bytes, len);
        else
            memset(&buffer[_length], 0, len);
        
        _length += len;
    }
    
    void ByteArray::append(int len) {
        append(0, len);
    }
    
    int ByteArray::indexOf(ByteArray search, int start) {
        bool found;
        ssize_t slen = search.length();

        if (!_length || !slen || slen > _length || start < 0 || (size_t)start >= _length)
            return -1;
        
        for (unsigned int i=start; i<=_length-slen; i++) {
            found = true;
            for (int x=0; x<slen; x++) {
                found = buffer[i+x] == search.buffer[x];
                if (!found)
                    break;
            }
            if (found)
                return i;
        }
        return -1;
    }
    
    int ByteArray::occuranceCount(ByteArray search) {
        int offset = 0;
        int cnt = 0;
        
        while ((offset = indexOf(search, offset)) != -1) {
            cnt++;
            offset++;
        }
        
        return cnt;
    }
    
    void ByteArray::shift(long bits) {
        if (!bits || !_length)
            return;

        ByteArray original(*this);
        const unsigned long original_length = _length;

        if (bits > 0) {
            const unsigned long byte_shift = (unsigned long)(bits / 8);
            const int bit_shift = (int)(bits % 8);
            const unsigned long new_length = original_length + byte_shift + (bit_shift ? 1UL : 0UL);

            allocateBlock(new_length);
            memset(buffer, 0, new_length);
            _length = new_length;

            for (unsigned long i = 0; i < original_length; i++) {
                const unsigned char b = (unsigned char)original.buffer[i];
                const unsigned long dest = i + byte_shift;

                if (bit_shift) {
                    buffer[dest] |= (char)(b >> bit_shift);
                    buffer[dest + 1] |= (char)(b << (8 - bit_shift));
                } else {
                    buffer[dest] = (char)b;
                }
            }
        } else {
            const long abs_bits = -bits;
            const unsigned long byte_shift = (unsigned long)(abs_bits / 8);
            const int bit_shift = (int)(abs_bits % 8);
            const unsigned long new_length = original_length + byte_shift + (bit_shift ? 1UL : 0UL);

            allocateBlock(new_length);
            memset(buffer, 0, new_length);
            _length = new_length;

            for (unsigned long i = 0; i < original_length; i++) {
                const unsigned char b = (unsigned char)original.buffer[i];
                const unsigned long dest = i;

                if (bit_shift) {
                    buffer[dest] |= (char)(b >> (8 - bit_shift));
                    buffer[dest + 1] |= (char)(b << bit_shift);
                } else {
                    buffer[dest] = (char)b;
                }
            }
        }
    }
    
    ByteArray ByteArray::subBytes(int offset, int length) {
        int nlen = length ? length : (int)_length;
        
        if (offset > _length)
            nlen = 0;
        else if (offset+nlen > _length)
            nlen = (int)(_length-offset);
    
        return ByteArray(&buffer[offset], nlen);
    }
    
    ByteArray &ByteArray::insert(int index, const char* ins, size_t len) {
        if (_length+len >= size)
            allocateBlock(_length+len);
        
        for (ssize_t i=_length; i>=index; i--)
            buffer[i+len] = buffer[i];
        
        memcpy(&buffer[index], ins, len);
        
        _length += len;
        
        return *this;
    }
    
    ByteArray &ByteArray::insert(int index, ByteArray ins) {
        return insert(index, ins.operator const char *(), ins.length());
    }
    
    ByteArray &ByteArray::replace(ByteArray search, ByteArray replace, int offset, int maxcnt) {
        ssize_t cnt=0, index, size_dif, slen, rlen;
        
        slen = search.length();
        rlen = replace.length();
        if (!slen)
            return *this;

        size_dif = rlen - slen;
        
        while ((index = indexOf(search, offset)) != -1) {
            if (maxcnt > 0 && cnt == maxcnt)
                break;
            
            if (size_dif > 0 && _length+size_dif >= size)
                allocateBlock(_length+size_dif);
            
            memcpy(&buffer[index], replace.operator const char *(), rlen);
            memmove(&buffer[index + rlen],
                &buffer[index + slen],
                _length - (index + slen));
            
            _length += size_dif;
            
            cnt++;
            offset = index + rlen;
        }
        
        return *this;
    }
    
    String ByteArray::toHex() {
        char tmp[3];
        String ret;
        
        tmp[2] = 0;
        
        for (unsigned int i=0; i<_length; i++) {
            tmp[0] = GET_HEX_HIGH_CHAR(buffer[i]);
            tmp[1] = GET_HEX_LOW_CHAR(buffer[i]);
            ret += tmp;
        }
        
        return ret;
    }
    
    ByteArray ByteArray::fromHex(String hex) {
        char buf[hex.length()/2], h, l;
        int i, len = (int)hex.length();
        for (i=0; i<len; i+=2) {
            h = hex[i] - 48;
            l = hex[i+1] - 48;
            
            if (h > 10)
                h -= 7;
            
            if (l > 10)
                l -= 7;
            
            buf[i/2] = ((h<<4)&0xF0) | (l&0x0F);
        }
        
        return ByteArray(buf, i/2);
    }
    
};
