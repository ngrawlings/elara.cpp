//
//  IndexDataStore.cpp
//  NrIO
//
//  Created by Nyhl Rawlings on 23/5/22.
//  Copyright © 2022 Liquidsoft Studio. All rights reserved.
//

#include "IndexedDataStore.h"

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/ByteArray.h>
#include <unistd.h>

namespace elara {

    namespace {
        IndexedDataStore::CRASH_TEST_POINT indexed_data_store_crash_point = IndexedDataStore::CRASH_NONE;
        int indexed_data_store_crash_trigger_count = 0;
    }

    void IndexedDataStore::setCrashTestPoint(CRASH_TEST_POINT point, int trigger_count) {
        indexed_data_store_crash_point = point;
        indexed_data_store_crash_trigger_count = trigger_count > 0 ? trigger_count : 1;
    }

    void IndexedDataStore::clearCrashTestPoint() {
        indexed_data_store_crash_point = CRASH_NONE;
        indexed_data_store_crash_trigger_count = 0;
    }

    void IndexedDataStore::crashTestHook(CRASH_TEST_POINT point) {
        if (indexed_data_store_crash_point != point)
            return;

        if (--indexed_data_store_crash_trigger_count <= 0)
            _exit(86);
    }

    IndexedDataStore::IndexedDataStore(String path) : file(path) {
        if (file.length()==0) {
            INDEX_DESCRIPTOR root_descriptor;
            INDEX_DESCRIPTOR system_descriptor;
            INDEX_DESCRIPTOR user_descriptor;
            FILE_DESCRIPTOR recycled_blocks_file;
            
            memset(&root_descriptor, 0, sizeof(INDEX_DESCRIPTOR));
            memset(&system_descriptor, 0, sizeof(INDEX_DESCRIPTOR));
            memset(&user_descriptor, 0, sizeof(INDEX_DESCRIPTOR));
            memset(&recycled_blocks_file, 0, sizeof(FILE_DESCRIPTOR));
            
            root_descriptor.magic_flag = MAGIC_FLAG_INDEX;
            system_descriptor.magic_flag = MAGIC_FLAG_INDEX;
            user_descriptor.magic_flag = MAGIC_FLAG_INDEX;
            recycled_blocks_file.magic_flag = MAGIC_FLAG_FILE;
            
            root_descriptor.slot[0] = sizeof(INDEX_DESCRIPTOR);   // Offset of system descriptor
            root_descriptor.slot[1] = sizeof(INDEX_DESCRIPTOR)*2; // Offset of user descriptor
            root_descriptor.file = sizeof(INDEX_DESCRIPTOR)*3;
            
            file.write(0, (const char*)&root_descriptor, sizeof(INDEX_DESCRIPTOR));
            file.write(sizeof(INDEX_DESCRIPTOR), (const char*)&system_descriptor, sizeof(INDEX_DESCRIPTOR));
            file.write(sizeof(INDEX_DESCRIPTOR)*2, (const char*)&user_descriptor, sizeof(INDEX_DESCRIPTOR));
            file.write(sizeof(INDEX_DESCRIPTOR)*3, (const char*)&recycled_blocks_file, sizeof(FILE_DESCRIPTOR));
        }
    }

    IndexedDataStore::~IndexedDataStore() {
        
    }

    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> IndexedDataStore::createFile(Memory key, unsigned int block_size) {
        Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> desc = getUserDecriptor();
        
        for (int i=0; i<key.length(); i++) {
            desc = getChildDescriptor(desc, (unsigned char)(key.getPtr()[i]&0xFF), true);
            if (!desc.getPtr())
                throw "Failed to get file key";
        }
        
        if (desc.getPtr()->descriptor.file)
            throw "File already exists";
        
        FILE_DESCRIPTOR file_desc;
        memset(&file_desc, 0, sizeof(FILE_DESCRIPTOR));
        file_desc.magic_flag = MAGIC_FLAG_FILE;
        file_desc.block_size = block_size;
        unsigned long long offset = file.length();
        file.write(offset, (const char*)&file_desc, sizeof(FILE_DESCRIPTOR));
        crashTestHook(CRASH_AFTER_CREATE_FILE_DESCRIPTOR_WRITE);
        
        desc.getPtr()->descriptor.file = offset;
        updateIndexDescriptor(desc);
        
        return loadFileDescriptor(offset);
    }

    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> IndexedDataStore::getFile(Memory key) {
        Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> desc = getUserDecriptor();
        
        for (int i=0; i<key.length(); i++) {
            desc = getChildDescriptor(desc, (unsigned char)(key.getPtr()[i]&0xFF), false);
            if (!desc.getPtr())
                throw "Failed to get file key";
        }
        
        if (desc.getPtr()->descriptor.file)
            return loadFileDescriptor(desc.getPtr()->descriptor.file);
        
        return Ref<LOADED_FILE_DESCRIPTOR>();
    }

    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> IndexedDataStore::getOrCreateFile(Memory key, unsigned int block_size) {
        Ref<LOADED_FILE_DESCRIPTOR> file;
        try {
            file = getFile(key);
        } catch (...) {
            file = createFile(key, block_size);
        }

        if (!file.getPtr())
            file = createFile(key, block_size);
        
        return file;
    }

    bool IndexedDataStore::writeToFile(Ref<LOADED_FILE_DESCRIPTOR> file, Memory data, unsigned long long offset, unsigned long long length) {
        if (!file.getPtr())
            throw "Invalid file descriptor";

        unsigned long long file_size = getFileSize(file);
        if (offset > file_size)
            return false;
        
        if (!file.getPtr()->descriptor.first_data_block) {
            // Create First data block must be done manually
            DATA_BLOCK_DESCRIPTOR desc;
            memset(&desc, 0, sizeof(DATA_BLOCK_DESCRIPTOR));
            desc.magic_flag = MAGIC_FLAG_DATA;
            desc.block_size = file.getPtr()->descriptor.block_size;
            
            unsigned long long file_offset = this->file.length();
            this->file.write(file_offset, (const char*)&desc, sizeof(DATA_BLOCK_DESCRIPTOR));
            crashTestHook(CRASH_AFTER_FIRST_DATA_DESCRIPTOR_WRITE);
            
            Memory mem(file.getPtr()->descriptor.block_size);
            for (int i=0; i<file.getPtr()->descriptor.block_size; i++) {
                mem.getPtr()[i] = 0;
            }
            this->file.write(file_offset+sizeof(DATA_BLOCK_DESCRIPTOR), mem.operator char *(), file.getPtr()->descriptor.block_size);
            crashTestHook(CRASH_AFTER_FIRST_DATA_PAYLOAD_WRITE);
            
            file.getPtr()->descriptor.first_data_block = file_offset;
            file.getPtr()->descriptor.last_data_block = file_offset;
            //updateFileDescriptor(file);
        }
        
        Ref<LOADED_DATA_BLOCK_DESCRIPTOR> desc = loadDataDescriptor(file.getPtr()->descriptor.first_data_block);
        unsigned long long block_offset = 0;
        int cursor = 0;
        unsigned long long written = 0;

        // Get block where offset resides
        // This causes a major slow down when there are thousands of blocks
        // Most of the time we are just appending data, though an index in a later version would be good. Just jumping to the last block will work for appending.
        if (offset == file_size && file_size%file.getPtr()->descriptor.block_size == 0) {
            // This optimisation will only work when appending whole blocks
            block_offset = offset;
            desc = loadDataDescriptor(file.getPtr()->descriptor.last_data_block);
            if (offset)
                desc = createDataBlock(file, desc);
        } else {
            while(desc.getPtr() && desc.getPtr()->descriptor.used_bytes == desc.getPtr()->descriptor.block_size && (desc.getPtr()->descriptor.used_bytes+block_offset) <= offset) {
                if (desc.getPtr()->descriptor.next_data_block) {
                    desc = loadDataDescriptor(desc.getPtr()->descriptor.next_data_block);
                    block_offset += desc.getPtr()->descriptor.block_size;
                } else {
                    desc = createDataBlock(file, desc);
                    block_offset += file.getPtr()->descriptor.block_size;
                }
            }
        }
            
        if (!desc.getPtr())
            throw "Reached EOF before offset was located";
            
        if (offset > block_offset && offset < block_offset+desc.getPtr()->descriptor.block_size)
            cursor = int(offset-block_offset);
        
        while (written < length) {
            unsigned long long len = desc.getPtr()->descriptor.block_size - cursor;
            
            if (length-written < len)
                len = length-written;
            
            for (unsigned long long i=0; i<len; i++) {
                desc.getPtr()->data.getPtr()[cursor++] = data.getPtr()[written++];
            }
            
            if (cursor > desc.getPtr()->descriptor.used_bytes) {
                desc.getPtr()->descriptor.used_bytes = cursor;
                updateDataBlockDescriptor(desc);
            }
            
            updateDataBlockData(desc);
            
            if (written<length) {
                block_offset += desc.getPtr()->descriptor.block_size;
                
                if (desc.getPtr()->descriptor.next_data_block) {
                    desc = loadDataDescriptor(desc.getPtr()->descriptor.next_data_block);
                } else {
                    desc = createDataBlock(file, desc);
                }
            }
            cursor = 0;
        }
        
        if (offset+length > file_size) {
            file.getPtr()->descriptor.file_size = offset+length;
            updateFileDescriptor(file);
        }
        
        return written == length;
    }

    unsigned long long IndexedDataStore::getFileSize(Ref<LOADED_FILE_DESCRIPTOR> file) {
        return file.getPtr()->descriptor.file_size;
    }

    Memory IndexedDataStore::readFromFile(Ref<LOADED_FILE_DESCRIPTOR> file, unsigned long long offset, unsigned long long length) {
        ByteArray ret;
        
        unsigned long long file_size = getFileSize(file);
        if (offset >= file_size)
            return ret;
        
        Ref<LOADED_DATA_BLOCK_DESCRIPTOR> desc = loadDataDescriptor(file.getPtr()->descriptor.first_data_block);
        int cursor = 0;
        
        // Get block where offset resides
        while(desc.getPtr() && desc.getPtr()->descriptor.used_bytes == desc.getPtr()->descriptor.block_size && (desc.getPtr()->descriptor.used_bytes+cursor) <= offset) {
            if (desc.getPtr()->descriptor.next_data_block) {
                desc = loadDataDescriptor(desc.getPtr()->descriptor.next_data_block);
                cursor += desc.getPtr()->descriptor.used_bytes;
            } else {
                return ret;
            }
        }
        
        unsigned int _offset = (int)(offset-cursor);
        unsigned int len = desc.getPtr()->descriptor.used_bytes-_offset;
        if (len > length)
            len = (unsigned int)length;
        
        while (ret.length()<length) {
            ByteArray block = desc.getPtr()->data;
            ret.append(block.subBytes(_offset, len));
            
            if (!desc.getPtr()->descriptor.next_data_block)
                break;
            
            desc = loadDataDescriptor(desc.getPtr()->descriptor.next_data_block);
            
            _offset = 0;
            len = desc.getPtr()->descriptor.used_bytes;
            if (ret.length()+len > length)
                len = (unsigned int)(length-ret.length());
        }
        
        return ret;
    }

    Memory IndexedDataStore::readOrSet(Memory key, Memory default_value) {
        Ref<LOADED_FILE_DESCRIPTOR> file = getOrCreateFile(key, (int)default_value.length());
        Memory ret = readFromFile(file, 0, default_value.length());
        
        if (ret.length() != default_value.length()) {
            writeToFile(file, default_value, 0, (int)default_value.length());
            return default_value;
        }
        return ret;
    }

    void IndexedDataStore::set(Memory key, Memory value) {
        Ref<LOADED_FILE_DESCRIPTOR> file = getOrCreateFile(key, (int)value.length());
        writeToFile(file, value, 0, value.length());
        if (file.getPtr()->descriptor.file_size != (unsigned long long)value.length()) {
            file.getPtr()->descriptor.file_size = value.length();
            updateFileDescriptor(file);
        }
    }

    void IndexedDataStore::set(Memory key, int value) {
        set(key, Memory(&value, sizeof(int)));
    }

    void IndexedDataStore::set(Memory key, unsigned int value) {
        set(key, Memory(&value, sizeof(int)));
    }

    void IndexedDataStore::set(Memory key, long long value) {
        set(key, Memory(&value, sizeof(long long)));
    }

    void IndexedDataStore::set(Memory key, unsigned long long value) {
        set(key, Memory(&value, sizeof(long long)));
    }

    int IndexedDataStore::readOrSet(Memory key, int default_value) {
        Memory res = readOrSet(key, Memory(&default_value, sizeof(int)));
        int ret;
        memcpy(&ret, res.operator char *(), sizeof(int));
        return ret;
    }

    Memory IndexedDataStore::read(Memory key, unsigned int length) {
        Ref<LOADED_FILE_DESCRIPTOR> file = getFile(key);
        return readFromFile(file, 0, length);
    }

    int IndexedDataStore::readInt(Memory key) {
        Memory res = read(key, sizeof(int));
        int ret;
        memcpy(&ret, res.operator char *(), sizeof(int));
        return ret;
    }

    long long IndexedDataStore::readLongLong(Memory key) {
        Memory res = read(key, sizeof(long long));
        long long ret;
        memcpy(&ret, res.operator char *(), sizeof(long long));
        return ret;
    }

    unsigned int IndexedDataStore::readOrSet(Memory key, unsigned int default_value) {
        Memory res = readOrSet(key, Memory(&default_value, sizeof(int)));
        unsigned int ret;
        memcpy(&ret, res.operator char *(), sizeof(int));
        return ret;
    }

    long long IndexedDataStore::readOrSet(Memory key, long long default_value) {
        Memory res = readOrSet(key, Memory(&default_value, sizeof(long long)));
        unsigned long long ret;
        memcpy(&ret, res.operator char *(), sizeof(long long));
        return ret;
    }

    unsigned long long IndexedDataStore::readOrSet(Memory key, unsigned long long default_value) {
        Memory res = readOrSet(key, Memory(&default_value, sizeof(long long)));
        unsigned long long ret;
        memcpy(&ret, res.operator char *(), sizeof(long long));
        return ret;
    }

    Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> IndexedDataStore::getChildDescriptor(Ref<LOADED_INDEX_DESCRIPTOR> descriptor, unsigned char index, bool create_index) {
        const unsigned long long bank_map_flag = 0x8000000000000000ULL;
        bool repeat;
        
        do {
            repeat = false;

            if (descriptor.getPtr()->descriptor.next_index_descriptor & bank_map_flag) {
                // This is a bank map, select correct node directly
                Ref<LOADED_BANK_MAP> bmap = loadBankMap(descriptor.getPtr()->descriptor.next_index_descriptor & ~bank_map_flag);

                unsigned long long slot = index/BANK_SIZE;

                if (bmap.getPtr()->descriptor.banks[slot]) {
                    descriptor = loadIndexDescriptor(bmap.getPtr()->descriptor.banks[slot]);
                } else 
                    return Ref<LOADED_INDEX_DESCRIPTOR>();
            }
            
            if (descriptor.getPtr()->descriptor.range_start <= index && descriptor.getPtr()->descriptor.range_start+BANK_SIZE > index) {
                unsigned long long offset = descriptor.getPtr()->descriptor.slot[index-descriptor.getPtr()->descriptor.range_start];
                if ((offset & bank_map_flag) == bank_map_flag) {
                    // return existing descriptor
                    return loadIndexDescriptor(offset & ~bank_map_flag);
                } else if (create_index) {
                    // Create new descriptor
                    INDEX_DESCRIPTOR ndesc;
                    memset(&ndesc, 0, sizeof(INDEX_DESCRIPTOR));
                    ndesc.magic_flag = MAGIC_FLAG_INDEX;
                    
                    unsigned long long offset = file.length();
                    file.write(offset, (const char*)&ndesc, sizeof(INDEX_DESCRIPTOR));
                    
                    descriptor.getPtr()->descriptor.slot[index-descriptor.getPtr()->descriptor.range_start] = offset | bank_map_flag;
                    updateIndexDescriptor(descriptor);
                    
                    return loadIndexDescriptor(offset);
                } else {
                    break;
                }
            } else if (create_index) {
                // Create sibling descriptor
                if (!descriptor.getPtr()->descriptor.next_index_descriptor) {
                    unsigned int start_range = index-(index%BANK_SIZE);
                    
                    INDEX_DESCRIPTOR ndesc;
                    memset(&ndesc, 0, sizeof(INDEX_DESCRIPTOR));
                    ndesc.magic_flag = MAGIC_FLAG_INDEX;
                    ndesc.range_start = start_range;
                    
                    unsigned long long offset = file.length();
                    file.write(offset, (const char*)&ndesc, sizeof(INDEX_DESCRIPTOR));
                    
                    descriptor.getPtr()->descriptor.next_index_descriptor = offset;
                    updateIndexDescriptor(descriptor);
                }
                
            }

            if (descriptor.getPtr()->descriptor.next_index_descriptor & bank_map_flag)
                throw "Bank map, should never reach this code";
            
            if (descriptor.getPtr()->descriptor.next_index_descriptor) {
                descriptor = loadIndexDescriptor(descriptor.getPtr()->descriptor.next_index_descriptor);
                repeat = true;
            }
            
        } while (repeat);
        
        return Ref<LOADED_INDEX_DESCRIPTOR>();
    }

    Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> IndexedDataStore::loadIndexDescriptor(unsigned long long offset) {
        Memory mem = file.read(offset, sizeof(INDEX_DESCRIPTOR));
        
        LOADED_INDEX_DESCRIPTOR *desc = new LOADED_INDEX_DESCRIPTOR;
        memcpy(&desc->descriptor, mem.operator char *(), sizeof(INDEX_DESCRIPTOR));
        
        if (desc->descriptor.magic_flag != MAGIC_FLAG_INDEX)
            throw "Invalid index descriptor";
        
        desc->offset = offset;
        
        return Ref<LOADED_INDEX_DESCRIPTOR>(desc);
    }

    Ref<IndexedDataStore::LOADED_BANK_MAP> IndexedDataStore::loadBankMap(unsigned long long offset) {
        Memory mem = file.read(offset, sizeof(BANK_MAP));

        LOADED_BANK_MAP *bmap = new LOADED_BANK_MAP;
        memcpy(&bmap->descriptor, mem.operator char *(), sizeof(BANK_MAP));
        bmap->offset = offset;

        if (bmap->descriptor.magic_flag != MAGIC_FLAG_BANK_MAP)
            throw "Invalid bank map";

        return Ref<LOADED_BANK_MAP>(bmap);
    }

    bool IndexedDataStore::convertDescriptorListToBankMap(Memory key) {
        const unsigned long long bank_map_flag = 0x8000000000000000ULL;
        Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> desc = getUserDecriptor();
        
        for (int i=0; i<key.length(); i++) {
            desc = getChildDescriptor(desc, (unsigned char)(key.getPtr()[i]&0xFF), false);
            if (!desc.getPtr())
                throw "Failed to get file key";
        }

        if (desc.getPtr()->descriptor.range_start == 0) { // We will only convert a descriptor list to a bank map if we have a lowest possible descriptor
            Memory mem(sizeof(BANK_MAP));
            BANK_MAP *bmap = (BANK_MAP*)mem.operator char *();
            memset(bmap, 0, sizeof(BANK_MAP));
            bmap->magic_flag = MAGIC_FLAG_BANK_MAP;

            Ref<LOADED_INDEX_DESCRIPTOR> next;
            unsigned long long next_offset = desc.getPtr()->descriptor.next_index_descriptor;

            unsigned long long file_offset = file.length();

            if ((next_offset & bank_map_flag) == 0) {
                desc.getPtr()->descriptor.next_index_descriptor = file_offset | bank_map_flag;
                updateIndexDescriptor(desc);

                bmap->banks[0] = desc.getPtr()->offset;
                while (next_offset) {
                    next = loadIndexDescriptor(next_offset);
                    int bank_slot = next.getPtr()->descriptor.range_start/BANK_SIZE;
                    bmap->banks[bank_slot] = next.getPtr()->offset;

                    next_offset = next.getPtr()->descriptor.next_index_descriptor;
                    next.getPtr()->descriptor.next_index_descriptor = file_offset | bank_map_flag;
                    updateIndexDescriptor(next);
                }

                this->file.write(file_offset, mem.operator char *(), sizeof(BANK_MAP));
                crashTestHook(CRASH_AFTER_BANK_MAP_WRITE);

                return true;
            }
        }

        return false;
    }

    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> IndexedDataStore::loadFileDescriptor(unsigned long long offset) {
        Memory mem = file.read(offset, sizeof(FILE_DESCRIPTOR));
        
        LOADED_FILE_DESCRIPTOR *desc = new LOADED_FILE_DESCRIPTOR;
        memcpy(&desc->descriptor, mem.operator char *(), sizeof(FILE_DESCRIPTOR));
        
        if (desc->descriptor.magic_flag != MAGIC_FLAG_FILE)
            throw "Invalid file descriptor";
        
        desc->offset = offset;
        
        return Ref<LOADED_FILE_DESCRIPTOR>(desc);
    }

    Ref<IndexedDataStore::LOADED_DATA_BLOCK_DESCRIPTOR> IndexedDataStore::loadDataDescriptor(unsigned long long offset) {
        Memory mem = file.read(offset, sizeof(DATA_BLOCK_DESCRIPTOR));
        
        LOADED_DATA_BLOCK_DESCRIPTOR *desc = new LOADED_DATA_BLOCK_DESCRIPTOR;
        memcpy(&desc->descriptor, mem.operator char *(), sizeof(DATA_BLOCK_DESCRIPTOR));
        
        if (desc->descriptor.magic_flag != MAGIC_FLAG_DATA)
            throw "Invalid data descriptor";
        
        desc->offset = offset;
        desc->data = file.read(offset+sizeof(DATA_BLOCK_DESCRIPTOR), desc->descriptor.block_size);
        
        return Ref<LOADED_DATA_BLOCK_DESCRIPTOR>(desc);
    }

    void IndexedDataStore::updateIndexDescriptor(Ref<LOADED_INDEX_DESCRIPTOR> descriptor) {
        file.write(descriptor.getPtr()->offset, (const char*)&descriptor.getPtr()->descriptor, sizeof(INDEX_DESCRIPTOR));
        crashTestHook(CRASH_AFTER_INDEX_DESCRIPTOR_WRITE);
    }

    Ref<IndexedDataStore::LOADED_DATA_BLOCK_DESCRIPTOR> IndexedDataStore::createDataBlock(Ref<LOADED_FILE_DESCRIPTOR> file_desc, Ref<LOADED_DATA_BLOCK_DESCRIPTOR> previous) {
        DATA_BLOCK_DESCRIPTOR desc;
        memset(&desc, 0, sizeof(DATA_BLOCK_DESCRIPTOR));
        desc.magic_flag = MAGIC_FLAG_DATA;
        desc.block_size = file_desc.getPtr()->descriptor.block_size;
        
        unsigned long long file_offset = file.length();
        file.write(file_offset, (const char*)&desc, sizeof(DATA_BLOCK_DESCRIPTOR));
        
        Memory mem(desc.block_size);
        for (int i=0; i<desc.block_size; i++) {
            mem.getPtr()[i] = 0;
        }
        this->file.write(file_offset+sizeof(DATA_BLOCK_DESCRIPTOR), mem.operator char *(), desc.block_size);
        
        previous.getPtr()->descriptor.next_data_block = file_offset;
        file_desc.getPtr()->descriptor.last_data_block = file_offset;
        updateDataBlockDescriptor(previous);
        updateFileDescriptor(file_desc);
        
        return loadDataDescriptor(file_offset);
    }
        
    void IndexedDataStore::updateFileDescriptor(Ref<LOADED_FILE_DESCRIPTOR> descriptor) {
        file.write(descriptor.getPtr()->offset, (const char*)&descriptor.getPtr()->descriptor, sizeof(FILE_DESCRIPTOR));
        crashTestHook(CRASH_AFTER_FILE_DESCRIPTOR_WRITE);
    }

    void IndexedDataStore::updateDataBlockDescriptor(Ref<LOADED_DATA_BLOCK_DESCRIPTOR> descriptor) {
        file.write(descriptor.getPtr()->offset, (const char*)&descriptor.getPtr()->descriptor, sizeof(DATA_BLOCK_DESCRIPTOR));
        crashTestHook(CRASH_AFTER_DATA_DESCRIPTOR_WRITE);
    }

    void IndexedDataStore::updateDataBlockData(Ref<LOADED_DATA_BLOCK_DESCRIPTOR> descriptor) {
        unsigned long long offset = descriptor.getPtr()->offset+sizeof(DATA_BLOCK_DESCRIPTOR);
        int len = descriptor.getPtr()->descriptor.block_size;
        
        file.write(offset, descriptor.getPtr()->data.operator char *(), len);
        crashTestHook(CRASH_AFTER_DATA_PAYLOAD_WRITE);
    }

    Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> IndexedDataStore::getRootDecriptor() {
        return loadIndexDescriptor(0);
    }

    Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> IndexedDataStore::getSystemDecriptor() {
        Ref<LOADED_INDEX_DESCRIPTOR> root = getRootDecriptor();
        return loadIndexDescriptor(root.getPtr()->descriptor.slot[0]);
    }

    Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> IndexedDataStore::getUserDecriptor() {
        Ref<LOADED_INDEX_DESCRIPTOR> root = getRootDecriptor();
        return loadIndexDescriptor(root.getPtr()->descriptor.slot[1]);
    }

    RefArray<int> IndexedDataStore::getChildIndexes(Memory key) {
        const unsigned long long bank_map_flag = 0x8000000000000000ULL;
        Ref<IndexedDataStore::LOADED_INDEX_DESCRIPTOR> desc = getUserDecriptor();
        for (int i=0; i<key.length(); i++) {
            desc = getChildDescriptor(desc, (unsigned char)(key.getPtr()[i]&0xFF), false);
            if (!desc.getPtr())
                throw "Failed to get file key";
        }

        Array<int> list;

        if (desc.getPtr()->descriptor.next_index_descriptor & bank_map_flag) {
            Ref<LOADED_BANK_MAP> bmap = loadBankMap(desc.getPtr()->descriptor.next_index_descriptor & ~bank_map_flag);

            for (int bank=0; bank<(256/BANK_SIZE); bank++) {
                if (!bmap.getPtr()->descriptor.banks[bank])
                    continue;

                Ref<LOADED_INDEX_DESCRIPTOR> bank_desc = loadIndexDescriptor(bmap.getPtr()->descriptor.banks[bank]);
                for (int i=0; i<BANK_SIZE; i++) {
                    if (bank_desc.getPtr()->descriptor.slot[i] & bank_map_flag)
                        list.push(bank_desc.getPtr()->descriptor.range_start + i);
                }
            }
        } else {
            while (desc.getPtr()) {
                for (int i=0; i<BANK_SIZE; i++) {
                    if (desc.getPtr()->descriptor.slot[i] & bank_map_flag)
                        list.push(desc.getPtr()->descriptor.range_start + i);
                }

                if (!desc.getPtr()->descriptor.next_index_descriptor)
                    break;

                desc = loadIndexDescriptor(desc.getPtr()->descriptor.next_index_descriptor);
            }
        }

        int len = list.length();
        int *ret = new int[len+1];
        for (int i=0; i<len; i++) {
            ret[i] = list.get(i);
        }
        ret[len] = -1;

        return RefArray<int>(ret);
    }

}
