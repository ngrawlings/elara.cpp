//
//  IndexDataStore.hpp
//  NrIO
//
//  Created by Nyhl Rawlings on 23/5/22.
//  Copyright © 2022 Liquidsoft Studio. All rights reserved.
//

#ifndef IndexDataStore_hpp
#define IndexDataStore_hpp

#include <stdio.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/Memory.h>
#include <libelaracore/memory/String.h>
#include "File.h"

#define MAGIC_FLAG_INDEX    0xAAAAAAAA
#define MAGIC_FLAG_FILE     0xBBBBBBBB
#define MAGIC_FLAG_DATA     0xCCCCCCCC
#define MAGIC_FLAG_BANK_MAP 0xDDDDDDDD

#define BANK_SIZE 16

namespace elara {

    class IndexedDataStore {
    public:
        typedef struct {
            unsigned long magic_flag;
            unsigned char range_start;      // Start of first 16 entries
            unsigned long long next_index_descriptor; // if the most significant bit is a 1, this is a bank map, not a chained list
            unsigned long long file;        // File which holds data block for recycling
            unsigned long long slot[BANK_SIZE];    // Most significant bit indicates if in use or not
        } INDEX_DESCRIPTOR;

        typedef struct {
            unsigned long magic_flag;
            unsigned long long banks[256/BANK_SIZE];
        } BANK_MAP;
        
        typedef struct {
            unsigned long long offset;
            INDEX_DESCRIPTOR descriptor;
        } LOADED_INDEX_DESCRIPTOR;
        
        typedef struct {
            unsigned long magic_flag;
            unsigned long long first_data_block;
            unsigned long long last_data_block;
            unsigned long long file_size;
            unsigned int block_size;
        } FILE_DESCRIPTOR;
        
        typedef struct {
            unsigned long long offset;
            FILE_DESCRIPTOR descriptor;
        } LOADED_FILE_DESCRIPTOR;
        
        typedef struct {
            unsigned long magic_flag;
            unsigned long long next_data_block;
            unsigned int block_size;
            unsigned int used_bytes;
        } DATA_BLOCK_DESCRIPTOR;
        
        typedef struct {
            unsigned long long offset;
            DATA_BLOCK_DESCRIPTOR descriptor;
            Memory data;
        } LOADED_DATA_BLOCK_DESCRIPTOR;

        typedef struct {
            unsigned long long offset;
            BANK_MAP descriptor;
        } LOADED_BANK_MAP;

        typedef struct {
            Memory key;
            Ref<LOADED_INDEX_DESCRIPTOR> desc;
        } ITERATION_DESC;
        
    public:
        IndexedDataStore(String path);
        virtual ~IndexedDataStore();
        
        Ref<LOADED_FILE_DESCRIPTOR> createFile(Memory key, unsigned int block_size);
        Ref<LOADED_FILE_DESCRIPTOR> getFile(Memory key);
        
        Ref<LOADED_FILE_DESCRIPTOR> getOrCreateFile(Memory key, unsigned int block_size);
        
        bool writeToFile(Ref<LOADED_FILE_DESCRIPTOR> file, Memory data, unsigned long long offset, unsigned long long length);
        unsigned long long getFileSize(Ref<LOADED_FILE_DESCRIPTOR> file);
        Memory readFromFile(Ref<LOADED_FILE_DESCRIPTOR> file, unsigned long long offset, unsigned long long length);
        
        void set(Memory key, Memory value);
        void set(Memory key, int value);
        void set(Memory key, unsigned int value);
        void set(Memory key, long long value);
        void set(Memory key, unsigned long long value);
        
        Memory read(Memory key, unsigned int length);
        int readInt(Memory key);
        long long readLongLong(Memory key);
        
        Memory readOrSet(Memory key, Memory default_value);
        int readOrSet(Memory key, int default_value);
        unsigned int readOrSet(Memory key, unsigned int default_value);
        long long readOrSet(Memory key, long long default_value);
        unsigned long long readOrSet(Memory key, unsigned long long default_value);

        bool convertDescriptorListToBankMap(Memory key); // Needs to be debuged, works but not time proven

        RefArray<int> getChildIndexes(Memory key);

    private:
        File file;
        
        Ref<LOADED_INDEX_DESCRIPTOR> getChildDescriptor(Ref<LOADED_INDEX_DESCRIPTOR> descriptor, unsigned char index, bool create_index);
        Ref<LOADED_INDEX_DESCRIPTOR> loadIndexDescriptor(unsigned long long offset);
        void updateIndexDescriptor(Ref<LOADED_INDEX_DESCRIPTOR> descriptor);

        Ref<LOADED_BANK_MAP> loadBankMap(unsigned long long offset);
        
        Ref<LOADED_FILE_DESCRIPTOR> loadFileDescriptor(unsigned long long offset);
        void updateFileDescriptor(Ref<LOADED_FILE_DESCRIPTOR> descriptor);
        
        Ref<LOADED_DATA_BLOCK_DESCRIPTOR> loadDataDescriptor(unsigned long long offset);
        
        Ref<LOADED_DATA_BLOCK_DESCRIPTOR> createDataBlock(Ref<LOADED_FILE_DESCRIPTOR> file, Ref<LOADED_DATA_BLOCK_DESCRIPTOR> previous);
        void updateDataBlockDescriptor(Ref<LOADED_DATA_BLOCK_DESCRIPTOR> descriptor);
        
        void updateDataBlockData(Ref<LOADED_DATA_BLOCK_DESCRIPTOR> descriptor);
        
        Ref<LOADED_INDEX_DESCRIPTOR> getRootDecriptor();
        Ref<LOADED_INDEX_DESCRIPTOR> getSystemDecriptor();
        Ref<LOADED_INDEX_DESCRIPTOR> getUserDecriptor();

    };

}

#endif /* IndexDataStore_hpp */
