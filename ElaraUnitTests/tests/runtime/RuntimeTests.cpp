#include "RuntimeTests.h"

#include <libelaracore/encoding/Base58.h>
#include <libelaracore/encoding/Base64.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/LinkedList.h>
#include <libelaracore/memory/RingBuffer.h>
#include <libelaracore/memory/String.h>
#include <libelaraio/IndexedDataStore.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelarathreads/memory/InstancePool.h>
#include <libelarathreads/memory/Ref.h>

#include <atomic>
#include <functional>
#include <set>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

namespace elara {

    namespace {

        long long getCurrentTimeMicros() {
            struct timeval tv;
            gettimeofday(&tv, 0);
            return ((long long)tv.tv_sec * 1000000LL) + tv.tv_usec;
        }

        bool matchesSelector(String selector, String name) {
            if (!selector.length() || selector == String("all"))
                return true;

            if (selector == String("runtime"))
                return true;

            if (selector == String("stress-memory"))
                return name == String("stress");

            if (selector == String("runtime.stress"))
                return name == String("stress");

            if (selector == String("runtime.fuzz"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.bytearray"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.hashmap"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.ringbuffer"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.string"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.linkedlist"))
                return name == String("fuzz");

            if (selector == String("runtime.fuzz.instancepool"))
                return name == String("fuzz");

            return selector == String("runtime.%").arg(name);
        }

        class StressPayload {
        public:
            StressPayload(int seed) : seed(seed) {
            }

            int seed;
            ByteArray bytes;
            String text;
        };

        String buildAlphaString(int seed, int length) {
            String value;

            for (int i=0; i<length; i++) {
                value += String((char)('a' + ((seed + i) % 26)));
            }

            return value;
        }

        Memory memoryFromString(String value) {
            return Memory((char*)value, value.length());
        }

        bool memoryEquals(const Memory &left, const Memory &right) {
            if (left.length() != right.length())
                return false;

            for (size_t i=0; i<left.length(); i++) {
                if (left.getByte((off_t)i) != right.getByte((off_t)i))
                    return false;
            }

            return true;
        }

        String buildIndexedDataStorePath() {
            return String("/tmp/elara-indexed-data-store-fuzz-%.dat").arg((int)getpid());
        }

        ByteArray buildIndexedDataStoreValue(int seed, int length) {
            ByteArray value;
            for (int i=0; i<length; i++)
                value.append((char[]){(char)((seed + (i * 17)) & 0xFF)}, 1);
            return value;
        }

        template <class T>
        T readIndexedStoreStruct(File &file, unsigned long long offset, const char *error_context) {
            Memory mem = file.read(offset, sizeof(T));
            if (mem.length() != sizeof(T))
                UnitTests::fail(error_context);

            T value;
            memcpy(&value, (char*)mem, sizeof(T));
            return value;
        }

        bool validateIndexedDataStoreFile(String path) {
            const unsigned long long bank_map_flag = 0x8000000000000000ULL;
            File file((char*)path);
            unsigned long long file_length = (unsigned long long)file.length();

            if (file_length < (sizeof(IndexedDataStore::INDEX_DESCRIPTOR) * 3ULL) + sizeof(IndexedDataStore::FILE_DESCRIPTOR))
                UnitTests::fail("IndexedDataStore integrity file too small");

            std::set<unsigned long long> reachable_indexes;
            std::set<unsigned long long> reachable_files;
            std::set<unsigned long long> reachable_banks;
            std::set<unsigned long long> reachable_data;
            std::set<unsigned long long> scanning_indexes;
            std::set<unsigned long long> scanning_files;

            std::function<void (unsigned long long)> visitIndex;
            std::function<void (unsigned long long)> visitFile;
            std::function<void (unsigned long long)> visitBankMap;

            auto ensureOffsetInFile = [&](unsigned long long offset, unsigned long long length, const char *error_context) {
                if (offset > file_length || length > file_length || offset + length > file_length)
                    UnitTests::fail(error_context);
            };

            auto isRecoverableOrphanFile = [&](unsigned long long offset) {
                IndexedDataStore::FILE_DESCRIPTOR descriptor = readIndexedStoreStruct<IndexedDataStore::FILE_DESCRIPTOR>(file, offset, "IndexedDataStore integrity failed to read orphaned file descriptor");
                return descriptor.magic_flag == MAGIC_FLAG_FILE
                    && descriptor.first_data_block == 0
                    && descriptor.last_data_block == 0
                    && descriptor.file_size == 0;
            };

            auto isRecoverableOrphanData = [&](unsigned long long offset) {
                IndexedDataStore::DATA_BLOCK_DESCRIPTOR descriptor = readIndexedStoreStruct<IndexedDataStore::DATA_BLOCK_DESCRIPTOR>(file, offset, "IndexedDataStore integrity failed to read orphaned data descriptor");
                if (descriptor.magic_flag != MAGIC_FLAG_DATA || descriptor.next_data_block != 0 || descriptor.used_bytes != 0)
                    return false;

                Memory payload = file.read(offset + sizeof(IndexedDataStore::DATA_BLOCK_DESCRIPTOR), descriptor.block_size);
                if (!payload.length() && offset + sizeof(IndexedDataStore::DATA_BLOCK_DESCRIPTOR) == file_length)
                    return true;

                if (payload.length() != descriptor.block_size)
                    return false;

                for (unsigned int i=0; i<descriptor.block_size; i++) {
                    if (payload.getByte(i) != 0)
                        return false;
                }

                return true;
            };

            visitBankMap = [&](unsigned long long offset) {
                ensureOffsetInFile(offset, sizeof(IndexedDataStore::BANK_MAP), "IndexedDataStore integrity bank map offset out of range");
                if (reachable_banks.count(offset))
                    return;

                IndexedDataStore::BANK_MAP bmap = readIndexedStoreStruct<IndexedDataStore::BANK_MAP>(file, offset, "IndexedDataStore integrity failed to read bank map");
                if (bmap.magic_flag != MAGIC_FLAG_BANK_MAP)
                    UnitTests::fail("IndexedDataStore integrity invalid bank map magic");

                reachable_banks.insert(offset);

                for (int bank=0; bank<(256 / BANK_SIZE); bank++) {
                    if (!bmap.banks[bank])
                        continue;

                    visitIndex(bmap.banks[bank]);
                    IndexedDataStore::INDEX_DESCRIPTOR descriptor = readIndexedStoreStruct<IndexedDataStore::INDEX_DESCRIPTOR>(file, bmap.banks[bank], "IndexedDataStore integrity failed to read banked index descriptor");
                    if ((descriptor.range_start / BANK_SIZE) != (unsigned int)bank)
                        UnitTests::fail("IndexedDataStore integrity bank map points to wrong range");
                }
            };

            visitFile = [&](unsigned long long offset) {
                ensureOffsetInFile(offset, sizeof(IndexedDataStore::FILE_DESCRIPTOR), "IndexedDataStore integrity file descriptor offset out of range");
                if (reachable_files.count(offset))
                    return;
                if (scanning_files.count(offset))
                    UnitTests::fail("IndexedDataStore integrity file descriptor cycle detected");

                scanning_files.insert(offset);

                IndexedDataStore::FILE_DESCRIPTOR descriptor = readIndexedStoreStruct<IndexedDataStore::FILE_DESCRIPTOR>(file, offset, "IndexedDataStore integrity failed to read file descriptor");
                if (descriptor.magic_flag != MAGIC_FLAG_FILE)
                    UnitTests::fail("IndexedDataStore integrity invalid file descriptor magic");
                if (!descriptor.block_size && descriptor.first_data_block)
                    UnitTests::fail("IndexedDataStore integrity zero-sized block file has data");

                reachable_files.insert(offset);

                unsigned long long current = descriptor.first_data_block;
                unsigned long long total_used = 0;
                unsigned long long last_seen = 0;
                std::set<unsigned long long> block_chain;

                if (!current) {
                    if (descriptor.last_data_block)
                        UnitTests::fail("IndexedDataStore integrity missing first block but last block set");
                }

                while (current) {
                    ensureOffsetInFile(current, sizeof(IndexedDataStore::DATA_BLOCK_DESCRIPTOR), "IndexedDataStore integrity data descriptor offset out of range");
                    if (block_chain.count(current))
                        UnitTests::fail("IndexedDataStore integrity data block cycle detected");

                    block_chain.insert(current);
                    reachable_data.insert(current);

                    IndexedDataStore::DATA_BLOCK_DESCRIPTOR block = readIndexedStoreStruct<IndexedDataStore::DATA_BLOCK_DESCRIPTOR>(file, current, "IndexedDataStore integrity failed to read data block descriptor");
                    if (block.magic_flag != MAGIC_FLAG_DATA)
                        UnitTests::fail("IndexedDataStore integrity invalid data block magic");
                    if (block.block_size != descriptor.block_size)
                        UnitTests::fail("IndexedDataStore integrity file/data block size mismatch");
                    if (block.used_bytes > block.block_size)
                        UnitTests::fail("IndexedDataStore integrity data block used bytes exceed block size");

                    ensureOffsetInFile(current + sizeof(IndexedDataStore::DATA_BLOCK_DESCRIPTOR), block.block_size, "IndexedDataStore integrity data payload out of range");

                    if (block.next_data_block && block.used_bytes != block.block_size)
                        UnitTests::fail("IndexedDataStore integrity non-terminal block is not full");

                    total_used += block.used_bytes;
                    last_seen = current;
                    current = block.next_data_block;
                }

                if (descriptor.first_data_block && descriptor.last_data_block && !block_chain.count(descriptor.last_data_block))
                    UnitTests::fail("IndexedDataStore integrity last data block points outside the reachable chain");
                if (descriptor.file_size > total_used)
                    UnitTests::fail("IndexedDataStore integrity file size exceeds reachable data");

                scanning_files.erase(offset);
            };

            visitIndex = [&](unsigned long long offset) {
                ensureOffsetInFile(offset, sizeof(IndexedDataStore::INDEX_DESCRIPTOR), "IndexedDataStore integrity index descriptor offset out of range");
                if (reachable_indexes.count(offset))
                    return;
                if (scanning_indexes.count(offset))
                    UnitTests::fail("IndexedDataStore integrity index descriptor cycle detected");

                scanning_indexes.insert(offset);

                IndexedDataStore::INDEX_DESCRIPTOR descriptor = readIndexedStoreStruct<IndexedDataStore::INDEX_DESCRIPTOR>(file, offset, "IndexedDataStore integrity failed to read index descriptor");
                if (descriptor.magic_flag != MAGIC_FLAG_INDEX)
                    UnitTests::fail("IndexedDataStore integrity invalid index descriptor magic");
                if (offset && (descriptor.range_start % BANK_SIZE))
                    UnitTests::fail("IndexedDataStore integrity invalid descriptor range start");

                reachable_indexes.insert(offset);

                for (int i=0; i<BANK_SIZE; i++) {
                    unsigned long long slot = descriptor.slot[i];
                    if (!slot)
                        continue;

                    if (slot & bank_map_flag) {
                        visitIndex(slot & ~bank_map_flag);
                    } else {
                        if (offset == 0 && (i == 0 || i == 1))
                            visitIndex(slot);
                        else
                            UnitTests::fail("IndexedDataStore integrity unflagged child slot encountered");
                    }
                }

                if (descriptor.file)
                    visitFile(descriptor.file);

                if (descriptor.next_index_descriptor) {
                    if (descriptor.next_index_descriptor & bank_map_flag)
                        visitBankMap(descriptor.next_index_descriptor & ~bank_map_flag);
                    else
                        visitIndex(descriptor.next_index_descriptor);
                }

                scanning_indexes.erase(offset);
            };

            visitIndex(0);

            IndexedDataStore::INDEX_DESCRIPTOR root = readIndexedStoreStruct<IndexedDataStore::INDEX_DESCRIPTOR>(file, 0, "IndexedDataStore integrity failed to reread root descriptor");
            if (root.slot[0] != sizeof(IndexedDataStore::INDEX_DESCRIPTOR))
                UnitTests::fail("IndexedDataStore integrity invalid system descriptor offset");
            if (root.slot[1] != sizeof(IndexedDataStore::INDEX_DESCRIPTOR) * 2ULL)
                UnitTests::fail("IndexedDataStore integrity invalid user descriptor offset");
            if (root.file != sizeof(IndexedDataStore::INDEX_DESCRIPTOR) * 3ULL)
                UnitTests::fail("IndexedDataStore integrity invalid recycled file descriptor offset");

            std::set<unsigned long long> scanned_indexes;
            std::set<unsigned long long> scanned_files;
            std::set<unsigned long long> scanned_banks;
            std::set<unsigned long long> scanned_data;

            unsigned long long offset = 0;
            while (offset < file_length) {
                ensureOffsetInFile(offset, sizeof(unsigned long), "IndexedDataStore integrity scan overran file");
                unsigned long magic = readIndexedStoreStruct<unsigned long>(file, offset, "IndexedDataStore integrity failed to read scanned magic");

                if (magic == MAGIC_FLAG_INDEX) {
                    ensureOffsetInFile(offset, sizeof(IndexedDataStore::INDEX_DESCRIPTOR), "IndexedDataStore integrity scanned index overflow");
                    scanned_indexes.insert(offset);
                    offset += sizeof(IndexedDataStore::INDEX_DESCRIPTOR);
                } else if (magic == MAGIC_FLAG_FILE) {
                    ensureOffsetInFile(offset, sizeof(IndexedDataStore::FILE_DESCRIPTOR), "IndexedDataStore integrity scanned file overflow");
                    scanned_files.insert(offset);
                    offset += sizeof(IndexedDataStore::FILE_DESCRIPTOR);
                } else if (magic == MAGIC_FLAG_BANK_MAP) {
                    ensureOffsetInFile(offset, sizeof(IndexedDataStore::BANK_MAP), "IndexedDataStore integrity scanned bank map overflow");
                    scanned_banks.insert(offset);
                    offset += sizeof(IndexedDataStore::BANK_MAP);
                } else if (magic == MAGIC_FLAG_DATA) {
                    IndexedDataStore::DATA_BLOCK_DESCRIPTOR block = readIndexedStoreStruct<IndexedDataStore::DATA_BLOCK_DESCRIPTOR>(file, offset, "IndexedDataStore integrity failed to read scanned data block");
                    unsigned long long record_length = sizeof(IndexedDataStore::DATA_BLOCK_DESCRIPTOR) + block.block_size;
                    if (offset + record_length > file_length) {
                        if (isRecoverableOrphanData(offset)) {
                            scanned_data.insert(offset);
                            offset = file_length;
                            continue;
                        }

                        UnitTests::fail("IndexedDataStore integrity scanned data block overflow");
                    }
                    scanned_data.insert(offset);
                    offset += record_length;
                } else {
                    UnitTests::fail("IndexedDataStore integrity encountered unknown on-disk record");
                }
            }

            if (offset != file_length)
                UnitTests::fail("IndexedDataStore integrity scan terminated at unexpected offset");

            for (std::set<unsigned long long>::iterator it = scanned_indexes.begin(); it != scanned_indexes.end(); ++it) {
                if (!reachable_indexes.count(*it))
                    UnitTests::fail("IndexedDataStore integrity orphaned index descriptor detected");
            }
            for (std::set<unsigned long long>::iterator it = scanned_files.begin(); it != scanned_files.end(); ++it) {
                if (!reachable_files.count(*it) && !isRecoverableOrphanFile(*it))
                    UnitTests::fail("IndexedDataStore integrity orphaned file descriptor detected");
            }
            for (std::set<unsigned long long>::iterator it = scanned_banks.begin(); it != scanned_banks.end(); ++it) {
                if (!reachable_banks.count(*it))
                    UnitTests::fail("IndexedDataStore integrity orphaned bank map detected");
            }
            for (std::set<unsigned long long>::iterator it = scanned_data.begin(); it != scanned_data.end(); ++it) {
                if (!reachable_data.count(*it) && !isRecoverableOrphanData(*it))
                    UnitTests::fail("IndexedDataStore integrity orphaned data block detected");
            }

            return true;
        }

        void verifyIndexedDataStoreShadow(IndexedDataStore &store, String keys[], ByteArray values[], bool active[], int slot_count) {
            for (int i=0; i<slot_count; i++) {
                if (!active[i])
                    continue;

                Memory expected = values[i];
                Memory actual = store.read(memoryFromString(keys[i]), (unsigned int)expected.length());
                if (!memoryEquals(actual, expected))
                    UnitTests::fail("IndexedDataStore persisted value mismatch");
            }
        }

        class MemoryStressTask : public Task {
        public:
            MemoryStressTask(std::atomic<int> *remaining, std::atomic<long long> *checksum, int seed, int iterations)
                : Task(true), remaining(remaining), checksum(checksum), seed(seed), iterations(iterations) {
            }

        protected:
            void run() {
                long long local_checksum = 0;

                for (int i=0; i<iterations; i++) {
                    threading::memory::Ref<StressPayload> payload(new StressPayload(seed + i));
                    payload->text = String("payload-%").arg(seed + i);
                    payload->bytes.append(Memory::getRandomBytes(128));

                    threading::memory::Ref<StressPayload> copy = payload;
                    threading::memory::Ref<StressPayload> copy2 = copy;

                    ByteArray bytes = copy2->bytes.subBytes(32);
                    bytes.shift(i % 7);

                    HashMap<String> map;
                    map.set(String("seed"), String("%").arg(copy2->seed));
                    map.set(String("text"), copy2->text);

                    Memory seed_key("seed", 4);
                    String stored = map.get(seed_key).get();

                    local_checksum += stored.length();
                    local_checksum += bytes.length();
                    local_checksum += copy2->seed & 0xFF;

                    copy.release();
                    copy2.release();
                    payload.release();
                }

                checksum->fetch_add(local_checksum, std::memory_order_relaxed);
                remaining->fetch_sub(1, std::memory_order_release);
                finished();
            }

        private:
            std::atomic<int> *remaining;
            std::atomic<long long> *checksum;
            int seed;
            int iterations;
        };

        bool testByteArrayShift() {
            ByteArray ba((char[]){0x01, 0x01, 0x01, 0x01}, 4);

            ba.shift(2);
            ba = ba.subBytes(1);

            ByteArray expected_right(
                (char[]){static_cast<char>(0x40), static_cast<char>(0x40), static_cast<char>(0x40), static_cast<char>(0x40)},
                4);
            if (ba != expected_right)
                UnitTests::fail("Right rotation failed");

            ba.shift(-2);
            ba = ba.subBytes(0, 4);

            ByteArray expected_left((char[]){0x01, 0x01, 0x01, 0x01}, 4);
            if (ba != expected_left)
                UnitTests::fail("Left rotation failed");

            return true;
        }

        bool testByteArrayAllocation() {
            ByteArray ba;

            for (int i=0; i<1024; i++) {
                ba.append(Memory::getRandomBytes(1024));
                ba = ba.subBytes(512);
            }

            return true;
        }

        bool testHashMap() {
            HashMap<String> map;

            for (int i=100; i<4096; i++) {
                String value = String("%").arg(i);
                map.set(value, value);
            }

            Memory key("1024", 4);
            if (!(map.get(key).get() == String("1024")))
                UnitTests::fail("HashMap lookup failed");

            return true;
        }

        bool testRingBuffer() {
            RingBuffer buffer(128);

            char data[128];
            for (int i=0; i<128; i++)
                data[i] = i % 64;

            if (buffer.append(data, 128) != 128)
                UnitTests::fail("RingBuffer append failed");

            Memory mem = buffer.fetch(64);
            if (mem.length() != 64)
                UnitTests::fail("RingBuffer fetch length failed");

            for (int i=0; i<64; i++) {
                if (mem[i] != data[i])
                    UnitTests::fail("RingBuffer fetch contents failed");
            }

            return true;
        }

        bool testBase58() {
            Memory src("12345678901234567890123456789012", 32);
            Memory enc = Base58::encode(src);
            Memory dec = Base58::decode(enc);

            if (!(dec == src))
                UnitTests::fail("Base58 encode/decode failed");

            return true;
        }

        bool testBase64() {
            Memory src("12345678901234567890123456789012", 32);
            Memory enc = Base64::encode(src);
            Memory dec = Base64::decode(enc);

            if (!(dec == src))
                UnitTests::fail("Base64 encode/decode failed");

            return true;
        }

        bool testIndexedDataStoreValidator() {
            String path = String("/tmp/elara-indexed-data-store-validator-%").arg((int)getpid());
            unlink((char*)path);

            {
                IndexedDataStore store(path);

                String primary_key("validator/root");
                ByteArray initial = buildIndexedDataStoreValue(101, 24);
                store.set(memoryFromString(primary_key), initial);

                Memory initial_read = store.read(memoryFromString(primary_key), (unsigned int)initial.length());
                if (!memoryEquals(initial_read, initial))
                    UnitTests::fail("IndexedDataStore validator initial roundtrip mismatch");

                ByteArray replacement = buildIndexedDataStoreValue(202, 9);
                store.set(memoryFromString(primary_key), replacement);

                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> primary_file = store.getFile(memoryFromString(primary_key));
                if (!primary_file.getPtr())
                    UnitTests::fail("IndexedDataStore validator missing primary file descriptor");

                if (store.getFileSize(primary_file) != (unsigned long long)replacement.length())
                    UnitTests::fail("IndexedDataStore validator file size did not track overwrite");

                Memory replacement_read = store.readFromFile(primary_file, 0, (unsigned long long)replacement.length());
                if (!memoryEquals(replacement_read, replacement))
                    UnitTests::fail("IndexedDataStore validator overwrite read mismatch");

                String append_key("validator/multiblock");
                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> append_file = store.getOrCreateFile(memoryFromString(append_key), 8);
                ByteArray append_expected;

                ByteArray block_a = buildIndexedDataStoreValue(700, 13);
                if (!store.writeToFile(append_file, block_a, 0, (unsigned long long)block_a.length()))
                    UnitTests::fail("IndexedDataStore validator failed first multiblock write");
                append_expected = block_a;

                ByteArray block_b = buildIndexedDataStoreValue(800, 19);
                if (!store.writeToFile(append_file, block_b, (unsigned long long)append_expected.length(), (unsigned long long)block_b.length()))
                    UnitTests::fail("IndexedDataStore validator failed append write");
                append_expected.append(block_b);

                ByteArray patch = buildIndexedDataStoreValue(900, 11);
                unsigned long long patch_offset = 5;
                if (!store.writeToFile(append_file, patch, patch_offset, (unsigned long long)patch.length()))
                    UnitTests::fail("IndexedDataStore validator failed interior patch write");
                for (int i=0; i<patch.length(); i++)
                    ((char*)append_expected)[patch_offset + i] = ((char*)patch)[i];

                if (store.getFileSize(append_file) != (unsigned long long)append_expected.length())
                    UnitTests::fail("IndexedDataStore validator multiblock file size mismatch");

                Memory append_read = store.readFromFile(append_file, 0, (unsigned long long)append_expected.length());
                if (!memoryEquals(append_read, append_expected))
                    UnitTests::fail("IndexedDataStore validator multiblock read mismatch");

                Memory window_read = store.readFromFile(append_file, 7, 17);
                Memory window_expected = append_expected.subBytes(7, 17);
                if (!memoryEquals(window_read, window_expected))
                    UnitTests::fail("IndexedDataStore validator multiblock window read mismatch");

                String int_key("validator/int");
                int int_default = 123456789;
                int int_initial = store.readOrSet(memoryFromString(int_key), int_default);
                if (int_initial != int_default)
                    UnitTests::fail("IndexedDataStore validator int readOrSet initial mismatch");
                int int_repeat = store.readOrSet(memoryFromString(int_key), 42);
                if (int_repeat != int_default)
                    UnitTests::fail("IndexedDataStore validator int readOrSet repeat mismatch");

                String uint_key("validator/uint");
                unsigned int uint_default = 4000000000U;
                unsigned int uint_initial = store.readOrSet(memoryFromString(uint_key), uint_default);
                if (uint_initial != uint_default)
                    UnitTests::fail("IndexedDataStore validator uint readOrSet initial mismatch");
                unsigned int uint_repeat = store.readOrSet(memoryFromString(uint_key), 7U);
                if (uint_repeat != uint_default)
                    UnitTests::fail("IndexedDataStore validator uint readOrSet repeat mismatch");

                String long_key("validator/longlong");
                long long long_default = 0x1122334455667788LL;
                long long long_initial = store.readOrSet(memoryFromString(long_key), long_default);
                if (long_initial != long_default)
                    UnitTests::fail("IndexedDataStore validator long long readOrSet initial mismatch");
                long long long_repeat = store.readOrSet(memoryFromString(long_key), 5LL);
                if (long_repeat != long_default)
                    UnitTests::fail("IndexedDataStore validator long long readOrSet repeat mismatch");

                String ulong_key("validator/ulonglong");
                unsigned long long ulong_default = 0xF122334455667788ULL;
                unsigned long long ulong_initial = store.readOrSet(memoryFromString(ulong_key), ulong_default);
                if (ulong_initial != ulong_default)
                    UnitTests::fail("IndexedDataStore validator unsigned long long readOrSet initial mismatch");
                unsigned long long ulong_repeat = store.readOrSet(memoryFromString(ulong_key), 9ULL);
                if (ulong_repeat != ulong_default)
                    UnitTests::fail("IndexedDataStore validator unsigned long long readOrSet repeat mismatch");

                String prefix("validator/bank/");
                String bank_keys[16];
                ByteArray bank_values[16];
                bool expected_children[256];
                for (int i=0; i<256; i++)
                    expected_children[i] = false;

                for (int i=0; i<16; i++) {
                    unsigned char suffix = (unsigned char)(i * 15);
                    bank_keys[i] = prefix + String((char)suffix) + String("/leaf");
                    bank_values[i] = buildIndexedDataStoreValue(500 + i, (i % 11) + 3);
                    store.set(memoryFromString(bank_keys[i]), bank_values[i]);
                    expected_children[suffix] = true;
                }

                if (!store.convertDescriptorListToBankMap(memoryFromString(prefix)))
                    UnitTests::fail("IndexedDataStore validator failed to convert descriptor list to bank map");

                for (int i=0; i<16; i++) {
                    Memory actual = store.read(memoryFromString(bank_keys[i]), (unsigned int)bank_values[i].length());
                    if (!memoryEquals(actual, bank_values[i]))
                        UnitTests::fail("IndexedDataStore validator bank-map read mismatch");
                }

                RefArray<int> children = store.getChildIndexes(memoryFromString(prefix));
                bool seen_children[256];
                for (int i=0; i<256; i++)
                    seen_children[i] = false;

                int child_count = 0;
                for (int i=0; children.getPtr() && children.getPtr()[i] != -1; i++) {
                    int child = children.getPtr()[i];
                    if (child < 0 || child > 255)
                        UnitTests::fail("IndexedDataStore validator child index out of range");
                    if (!expected_children[child])
                        UnitTests::fail("IndexedDataStore validator child index unexpected");
                    seen_children[child] = true;
                    child_count++;
                }

                if (child_count != 16)
                    UnitTests::fail("IndexedDataStore validator child index count mismatch");

                for (int i=0; i<256; i++) {
                    if (expected_children[i] != seen_children[i])
                        UnitTests::fail("IndexedDataStore validator child index set mismatch");
                }

                if (!validateIndexedDataStoreFile(path))
                    UnitTests::fail("IndexedDataStore validator integrity walk failed before reopen");
            }

            {
                IndexedDataStore reopened(path);
                String primary_key("validator/root");
                ByteArray replacement = buildIndexedDataStoreValue(202, 9);
                Memory replacement_read = reopened.read(memoryFromString(primary_key), (unsigned int)replacement.length());
                if (!memoryEquals(replacement_read, replacement))
                    UnitTests::fail("IndexedDataStore validator reopen mismatch");

                String append_key("validator/multiblock");
                ByteArray append_expected;
                ByteArray block_a = buildIndexedDataStoreValue(700, 13);
                ByteArray block_b = buildIndexedDataStoreValue(800, 19);
                ByteArray patch = buildIndexedDataStoreValue(900, 11);
                append_expected = block_a;
                append_expected.append(block_b);
                for (int i=0; i<patch.length(); i++)
                    ((char*)append_expected)[5 + i] = ((char*)patch)[i];

                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> append_file = reopened.getFile(memoryFromString(append_key));
                if (!append_file.getPtr())
                    UnitTests::fail("IndexedDataStore validator reopen missing multiblock descriptor");
                if (reopened.getFileSize(append_file) != (unsigned long long)append_expected.length())
                    UnitTests::fail("IndexedDataStore validator reopen multiblock size mismatch");
                Memory append_read = reopened.readFromFile(append_file, 0, (unsigned long long)append_expected.length());
                if (!memoryEquals(append_read, append_expected))
                    UnitTests::fail("IndexedDataStore validator reopen multiblock read mismatch");

                if (reopened.readInt(memoryFromString(String("validator/int"))) != 123456789)
                    UnitTests::fail("IndexedDataStore validator reopen int mismatch");
                if (reopened.readOrSet(memoryFromString(String("validator/uint")), 1U) != 4000000000U)
                    UnitTests::fail("IndexedDataStore validator reopen uint mismatch");
                if (reopened.readLongLong(memoryFromString(String("validator/longlong"))) != 0x1122334455667788LL)
                    UnitTests::fail("IndexedDataStore validator reopen long long mismatch");
                if (reopened.readOrSet(memoryFromString(String("validator/ulonglong")), 1ULL) != 0xF122334455667788ULL)
                    UnitTests::fail("IndexedDataStore validator reopen unsigned long long mismatch");

                String prefix("validator/bank/");
                RefArray<int> children = reopened.getChildIndexes(memoryFromString(prefix));
                bool seen_children[256];
                for (int i=0; i<256; i++)
                    seen_children[i] = false;

                int child_count = 0;
                for (int i=0; children.getPtr() && children.getPtr()[i] != -1; i++) {
                    int child = children.getPtr()[i];
                    if (child < 0 || child > 255)
                        UnitTests::fail("IndexedDataStore validator reopened child index out of range");
                    seen_children[child] = true;
                    child_count++;
                }

                if (child_count != 16)
                    UnitTests::fail("IndexedDataStore validator reopened child index count mismatch");

                for (int i=0; i<16; i++) {
                    unsigned char suffix = (unsigned char)(i * 15);
                    if (!seen_children[suffix])
                        UnitTests::fail("IndexedDataStore validator reopened child index missing");
                }
            }

            if (!validateIndexedDataStoreFile(path))
                UnitTests::fail("IndexedDataStore validator integrity walk failed after reopen");

            unlink((char*)path);
            return true;
        }

        bool testIndexedDataStoreIntegrityValidator() {
            String path = String("/tmp/elara-indexed-data-store-integrity-%").arg((int)getpid());
            unlink((char*)path);

            {
                IndexedDataStore store(path);

                for (int i=0; i<12; i++) {
                    String key = String("integrity/key/%").arg(i);
                    ByteArray value = buildIndexedDataStoreValue(300 + i, (i % 9) + 5);
                    store.set(memoryFromString(key), value);
                }

                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store.getOrCreateFile(memoryFromString(String("integrity/blob")), 16);
                ByteArray blob_a = buildIndexedDataStoreValue(900, 21);
                ByteArray blob_b = buildIndexedDataStoreValue(1200, 27);

                if (!store.writeToFile(file, blob_a, 0, (unsigned long long)blob_a.length()))
                    UnitTests::fail("IndexedDataStore integrity validator failed first blob write");
                if (!store.writeToFile(file, blob_b, (unsigned long long)blob_a.length(), (unsigned long long)blob_b.length()))
                    UnitTests::fail("IndexedDataStore integrity validator failed second blob write");

                String prefix("integrity/bank/");
                for (int i=0; i<16; i++) {
                    unsigned char suffix = (unsigned char)(i * 11);
                    String key = prefix + String((char)suffix) + String("/leaf");
                    ByteArray value = buildIndexedDataStoreValue(1500 + i, (i % 7) + 4);
                    store.set(memoryFromString(key), value);
                }

                if (!store.convertDescriptorListToBankMap(memoryFromString(prefix)))
                    UnitTests::fail("IndexedDataStore integrity validator bank-map conversion failed");
            }

            bool ok = validateIndexedDataStoreFile(path);
            unlink((char*)path);
            return ok;
        }

        bool testIndexedDataStoreCrashConsistencyValidator() {
            String path = String("/tmp/elara-indexed-data-store-crash-%").arg((int)getpid());
            unlink((char*)path);

            {
                IndexedDataStore store(path);
                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store.getOrCreateFile(memoryFromString(String("crash/blob")), 16);
                ByteArray base = buildIndexedDataStoreValue(2100, 64);
                if (!store.writeToFile(file, base, 0, (unsigned long long)base.length()))
                    UnitTests::fail("IndexedDataStore crash validator failed to seed base file");

                ByteArray stable = buildIndexedDataStoreValue(2200, 12);
                store.set(memoryFromString(String("crash/stable")), stable);
            }

            IndexedDataStore::CRASH_TEST_POINT points[] = {
                IndexedDataStore::CRASH_AFTER_DATA_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_DATA_PAYLOAD_WRITE,
                IndexedDataStore::CRASH_AFTER_FILE_DESCRIPTOR_WRITE
            };

            for (unsigned int point_index=0; point_index < (sizeof(points) / sizeof(points[0])); point_index++) {
                pid_t pid = fork();
                if (pid == -1)
                    UnitTests::fail("IndexedDataStore crash validator fork failed");

                if (pid == 0) {
                    IndexedDataStore child(path);
                    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = child.getFile(memoryFromString(String("crash/blob")));
                    if (!file.getPtr())
                        _exit(91);

                    ByteArray patch = buildIndexedDataStoreValue(2300 + (int)point_index, 21);
                    IndexedDataStore::setCrashTestPoint(points[point_index], 1);
                    child.writeToFile(file, patch, 7, (unsigned long long)patch.length());
                    IndexedDataStore::clearCrashTestPoint();
                    _exit(0);
                }

                int status = 0;
                if (waitpid(pid, &status, 0) == -1)
                    UnitTests::fail("IndexedDataStore crash validator waitpid failed");

                if (!WIFEXITED(status))
                    UnitTests::fail("IndexedDataStore crash validator child terminated abnormally");

                int exit_code = WEXITSTATUS(status);
                if (!(exit_code == 0 || exit_code == 86))
                    UnitTests::fail("IndexedDataStore crash validator child returned unexpected status");

                if (!validateIndexedDataStoreFile(path))
                    UnitTests::fail("IndexedDataStore crash validator integrity walk failed after injected crash");

                IndexedDataStore reopened(path);
                ByteArray stable = buildIndexedDataStoreValue(2200, 12);
                Memory stable_read = reopened.read(memoryFromString(String("crash/stable")), (unsigned int)stable.length());
                if (!memoryEquals(stable_read, stable))
                    UnitTests::fail("IndexedDataStore crash validator corrupted unrelated committed data");

                Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = reopened.getFile(memoryFromString(String("crash/blob")));
                if (!file.getPtr())
                    UnitTests::fail("IndexedDataStore crash validator lost crash/blob descriptor");
                if (reopened.getFileSize(file) != 64ULL)
                    UnitTests::fail("IndexedDataStore crash validator changed file size across interrupted in-place write");
            }

            IndexedDataStore::CRASH_TEST_POINT allocation_points[] = {
                IndexedDataStore::CRASH_AFTER_CREATE_FILE_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_INDEX_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_FIRST_DATA_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_FIRST_DATA_PAYLOAD_WRITE,
                IndexedDataStore::CRASH_AFTER_DATA_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_FILE_DESCRIPTOR_WRITE,
                IndexedDataStore::CRASH_AFTER_BANK_MAP_WRITE
            };

            for (unsigned int point_index=0; point_index < (sizeof(allocation_points) / sizeof(allocation_points[0])); point_index++) {
                String alloc_path = String("/tmp/elara-indexed-data-store-crash-alloc-%-%").arg((int)getpid()).arg((int)point_index);
                unlink((char*)alloc_path);

                {
                    IndexedDataStore seeded(alloc_path);
                    ByteArray stable = buildIndexedDataStoreValue(3200, 18);
                    seeded.set(memoryFromString(String("crash/stable")), stable);

                    Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> growth_file = seeded.getOrCreateFile(memoryFromString(String("crash/growth")), 16);
                    ByteArray base = buildIndexedDataStoreValue(3300, 16);
                    if (!seeded.writeToFile(growth_file, base, 0, (unsigned long long)base.length()))
                        UnitTests::fail("IndexedDataStore crash validator failed to seed growth file");
                }

                pid_t pid = fork();
                if (pid == -1)
                    UnitTests::fail("IndexedDataStore crash allocation validator fork failed");

                if (pid == 0) {
                    IndexedDataStore child(alloc_path);
                    IndexedDataStore::setCrashTestPoint(allocation_points[point_index], 1);

                    switch (allocation_points[point_index]) {
                    case IndexedDataStore::CRASH_AFTER_CREATE_FILE_DESCRIPTOR_WRITE:
                    case IndexedDataStore::CRASH_AFTER_INDEX_DESCRIPTOR_WRITE:
                    {
                        ByteArray value = buildIndexedDataStoreValue(3400 + (int)point_index, 13);
                        child.set(memoryFromString(String("crash/newkey")), value);
                        break;
                    }
                    case IndexedDataStore::CRASH_AFTER_FIRST_DATA_DESCRIPTOR_WRITE:
                    case IndexedDataStore::CRASH_AFTER_FIRST_DATA_PAYLOAD_WRITE:
                    {
                        Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = child.getOrCreateFile(memoryFromString(String("crash/firstblock")), 16);
                        ByteArray value = buildIndexedDataStoreValue(3500 + (int)point_index, 12);
                        child.writeToFile(file, value, 0, (unsigned long long)value.length());
                        break;
                    }
                    case IndexedDataStore::CRASH_AFTER_DATA_DESCRIPTOR_WRITE:
                    case IndexedDataStore::CRASH_AFTER_FILE_DESCRIPTOR_WRITE:
                    {
                        Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = child.getFile(memoryFromString(String("crash/growth")));
                        if (!file.getPtr())
                            _exit(92);
                        ByteArray value = buildIndexedDataStoreValue(3600 + (int)point_index, 16);
                        child.writeToFile(file, value, 16, (unsigned long long)value.length());
                        break;
                    }
                    case IndexedDataStore::CRASH_AFTER_BANK_MAP_WRITE:
                    {
                        String prefix("crash/bank/");
                        for (int i=0; i<16; i++) {
                            unsigned char suffix = (unsigned char)(i * 7);
                            String key = prefix + String((char)suffix) + String("/leaf");
                            ByteArray value = buildIndexedDataStoreValue(3700 + i, (i % 5) + 4);
                            child.set(memoryFromString(key), value);
                        }
                        child.convertDescriptorListToBankMap(memoryFromString(prefix));
                        break;
                    }
                    default:
                        break;
                    }

                    IndexedDataStore::clearCrashTestPoint();
                    _exit(0);
                }

                int status = 0;
                if (waitpid(pid, &status, 0) == -1)
                    UnitTests::fail("IndexedDataStore crash allocation validator waitpid failed");
                if (!WIFEXITED(status))
                    UnitTests::fail("IndexedDataStore crash allocation validator child terminated abnormally");

                int exit_code = WEXITSTATUS(status);
                if (!(exit_code == 0 || exit_code == 86))
                    UnitTests::fail("IndexedDataStore crash allocation validator child returned unexpected status");

                if (!validateIndexedDataStoreFile(alloc_path))
                    UnitTests::fail("IndexedDataStore crash allocation validator integrity walk failed");

                IndexedDataStore reopened(alloc_path);
                ByteArray stable = buildIndexedDataStoreValue(3200, 18);
                Memory stable_read = reopened.read(memoryFromString(String("crash/stable")), (unsigned int)stable.length());
                if (!memoryEquals(stable_read, stable))
                    UnitTests::fail("IndexedDataStore crash allocation validator corrupted committed stable data");

                unlink((char*)alloc_path);
            }

            unlink((char*)path);
            return true;
        }

        bool runThreadedMemoryStressBatch(int thread_count, int task_count, int iterations) {
            std::atomic<int> remaining(task_count);
            std::atomic<long long> checksum(0);

            Task::staticInit();
            Thread::init(thread_count);

            for (int i=0; i<task_count; i++)
                Thread::runTask(new MemoryStressTask(&remaining, &checksum, i * 7919, iterations));

            int waited_ms = 0;
            const int timeout_ms = 30000;
            while (remaining.load(std::memory_order_acquire) > 0 && waited_ms < timeout_ms) {
                usleep(1000);
                waited_ms++;
            }

            if (remaining.load(std::memory_order_acquire) != 0)
                UnitTests::fail("Threaded memory stress timed out; possible deadlock or stalled task");

            Thread::stopAllThreads();
            Thread::staticCleanUp();
            Task::staticCleanup();

            if (checksum.load(std::memory_order_relaxed) == 0)
                UnitTests::fail("Threaded memory stress produced an invalid checksum");

            return true;
        }

        bool testThreadedMemoryStress(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int batches = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                if (!runThreadedMemoryStressBatch(16, 256, 256))
                    return false;
                batches++;
            }

            if (!batches)
                UnitTests::fail("Threaded memory stress did not execute any batches");

            return true;
        }

        bool testThreadedMemoryStressHeavy(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int batches = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                if (!runThreadedMemoryStressBatch(32, 512, 512))
                    return false;
                batches++;
            }

            if (!batches)
                UnitTests::fail("Heavy threaded memory stress did not execute any batches");

            return true;
        }

        bool testByteArrayFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                ByteArray original(Memory::getRandomBytes((iterations % 255) + 1));
                ByteArray mutated = original;

                mutated.append(Memory::getRandomBytes((iterations % 31) + 1));
                mutated = mutated.subBytes(iterations % mutated.length());
                mutated.shift(iterations % 7);
                mutated.insert(0, original.subBytes(0, original.length() > 16 ? 16 : original.length()));

                if (!mutated.length())
                    UnitTests::fail("ByteArray fuzz produced an empty mutation unexpectedly");

                ByteArray snapshot = mutated.subBytes(0, mutated.length());
                if (!(snapshot == mutated))
                    UnitTests::fail("ByteArray fuzz snapshot mismatch");

                iterations++;
            }

            if (!iterations)
                UnitTests::fail("ByteArray fuzz did not execute any iterations");

            return true;
        }

        bool testHashMapFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;
            HashMap<String> map;
            String keys[32];
            bool active[32];

            for (int i=0; i<32; i++)
                active[i] = false;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                int slot = iterations % 32;
                String key = String("key-%").arg(slot);
                String value = buildAlphaString(iterations + slot, (iterations % 23) + 1);
                Memory key_memory((char*)key, key.length());

                keys[slot] = key;
                map.set(key, value);
                active[slot] = true;

                Ref<String> stored = map.get(key_memory);
                if (!stored.getPtr() || !(stored.get() == value))
                    UnitTests::fail("HashMap fuzz set/get mismatch");

                if ((iterations % 5) == 0) {
                    map.remove(key_memory);
                    active[slot] = false;

                    if (map.get(key_memory).getPtr())
                        UnitTests::fail("HashMap fuzz remove mismatch");
                }

                LinkedList< Ref<HashMap<String>::MAPENTRY> > entries = map.getEntries(Memory());
                LinkedListState< Ref<HashMap<String>::MAPENTRY> > state(&entries);
                Ref<HashMap<String>::MAPENTRY> *entry;
                int entry_count = 0;

                while (state.iterate(&entry)) {
                    entry_count++;
                    if (!entry->getPtr()->obj.getPtr())
                        UnitTests::fail("HashMap fuzz returned a null entry object");
                }

                int expected_count = 0;
                for (int i=0; i<32; i++) {
                    if (active[i])
                        expected_count++;
                }

                if (entry_count != expected_count)
                    UnitTests::fail("HashMap fuzz entry count mismatch");

                iterations++;
            }

            if (!iterations)
                UnitTests::fail("HashMap fuzz did not execute any iterations");

            return true;
        }

        bool testRingBufferFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;
            RingBuffer buffer(512);
            ByteArray shadow;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                ByteArray incoming(Memory::getRandomBytes((iterations % 47) + 1));
                size_t appended = buffer.append((char*)incoming, (size_t)incoming.length());
                if (appended > (size_t)incoming.length())
                    UnitTests::fail("RingBuffer fuzz append overflow");

                if (appended) {
                    shadow.append((char*)incoming, (int)appended);
                }

                if ((iterations % 3) == 0 && shadow.length()) {
                    size_t fetch_length = (size_t)((iterations % ((int)shadow.length())) + 1);
                    Memory fetched = buffer.fetch(fetch_length);
                    ByteArray expected = shadow.subBytes(0, (int)fetch_length);

                    if (fetched.length() != fetch_length)
                        UnitTests::fail("RingBuffer fuzz fetch length mismatch");

                    if (!(ByteArray(fetched) == expected))
                        UnitTests::fail("RingBuffer fuzz fetch contents mismatch");

                    shadow = shadow.subBytes((int)fetch_length);
                } else if ((iterations % 7) == 0 && shadow.length()) {
                    int drop_length = (iterations % (int)shadow.length()) + 1;
                    buffer.drop(drop_length);
                    shadow = shadow.subBytes(drop_length);
                }

                if (buffer.length() != (size_t)shadow.length())
                    UnitTests::fail("RingBuffer fuzz length mismatch");

                if (buffer.freeSpace() != buffer.size() - buffer.length())
                    UnitTests::fail("RingBuffer fuzz free-space mismatch");

                Memory preview = buffer.getDataUntilEnd();
                if (preview.length() > buffer.length())
                    UnitTests::fail("RingBuffer fuzz preview length mismatch");

                iterations++;
            }

            if (!iterations)
                UnitTests::fail("RingBuffer fuzz did not execute any iterations");

            return true;
        }

        bool testStringFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                String base = buildAlphaString(iterations, (iterations % 19) + 5);
                String suffix = buildAlphaString(iterations + 7, (iterations % 11) + 1);
                String combined = base;

                combined.append(suffix);
                if (!(combined.substr(0, base.length()) == base))
                    UnitTests::fail("String fuzz append/substr mismatch");

                String inserted = base;
                inserted.insert(inserted.length() / 2, suffix);
                if (inserted.length() != base.length() + suffix.length())
                    UnitTests::fail("String fuzz insert length mismatch");

                String replaced = combined;
                replaced.replace(suffix, String("ZZ"), 0, 1);
                if (replaced.indexOf(String("ZZ")) == -1)
                    UnitTests::fail("String fuzz replace mismatch");

                String padded = String("  ") + combined + String("  ");
                String trimmed = padded.trim();
                if (!(trimmed == combined))
                    UnitTests::fail("String fuzz trim mismatch");

                String upper = combined.toUpperCase();
                String lower = upper.toLowerCase();
                if (!(lower.toUpperCase() == upper))
                    UnitTests::fail("String fuzz case conversion mismatch");

                if (!combined.startsWith(base.substr(0, base.length() > 3 ? 3 : base.length())))
                    UnitTests::fail("String fuzz startsWith mismatch");

                if (!combined.endsWith(suffix))
                    UnitTests::fail("String fuzz endsWith mismatch");

                iterations++;
            }

            if (!iterations)
                UnitTests::fail("String fuzz did not execute any iterations");

            return true;
        }

        bool testLinkedListFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;
            LinkedList<int> list;
            const int shadow_capacity = 256;
            int shadow[shadow_capacity];
            int shadow_length = 0;
            void (LinkedList<int>::*remove_by_index)(int) = static_cast<void (LinkedList<int>::*)(int)>(&LinkedList<int>::remove);

            while ((getCurrentTimeMicros() - start) < duration_us) {
                int op = iterations % 6;
                int value = ((iterations * 37) % 100003) - 50001;

                if (shadow_length >= shadow_capacity - 4)
                    op = shadow_length ? 3 : 0;

                if (op == 0 || !shadow_length) {
                    list.add(value);
                    shadow[shadow_length++] = value;
                } else if (op == 1) {
                    int index = iterations % shadow_length;
                    LINKEDLIST_NODE_HANDLE node = list.firstNode();
                    for (int i = 0; i < index; i++)
                        node = list.nextNode(node);

                    int inserted = value ^ 0x55AA;
                    list.insert(node, inserted);
                    for (int i = shadow_length; i > index + 1; i--)
                        shadow[i] = shadow[i - 1];
                    shadow[index + 1] = inserted;
                    shadow_length++;
                } else if (op == 2) {
                    int index = iterations % shadow_length;
                    int updated = value ^ 0x1234;
                    list.set(index, updated);
                    shadow[index] = updated;
                } else if (op == 3) {
                    int index = iterations % shadow_length;
                    (list.*remove_by_index)(index);
                    for (int i = index; i < shadow_length - 1; i++)
                        shadow[i] = shadow[i + 1];
                    shadow_length--;
                } else if (op == 4) {
                    int index = iterations % shadow_length;
                    LINKEDLIST_NODE_HANDLE node = list.firstNode();
                    for (int i = 0; i < index; i++)
                        node = list.nextNode(node);
                    list.removeNode(node);
                    for (int i = index; i < shadow_length - 1; i++)
                        shadow[i] = shadow[i + 1];
                    shadow_length--;
                } else {
                    LinkedList<int> extra;
                    int extra_values[4];
                    int extra_count = (iterations % 4) + 1;
                    if (shadow_length + extra_count > shadow_capacity)
                        extra_count = shadow_capacity - shadow_length;

                    if (!extra_count) {
                        iterations++;
                        continue;
                    }

                    for (int i = 0; i < extra_count; i++) {
                        extra_values[i] = value + i + 1;
                        extra.add(extra_values[i]);
                    }

                    list.append(extra);
                    for (int i = 0; i < extra_count; i++)
                        shadow[shadow_length++] = extra_values[i];
                }

                if (list.length() != shadow_length)
                    UnitTests::fail("LinkedList fuzz length mismatch");

                if (!shadow_length) {
                    if (list.firstNode() || list.lastNode())
                        UnitTests::fail("LinkedList fuzz empty node mismatch");
                } else {
                    if (!list.firstNode() || !list.lastNode())
                        UnitTests::fail("LinkedList fuzz missing node handles");

                    for (int i = 0; i < shadow_length; i++) {
                        if (list.get(i) != shadow[i])
                            UnitTests::fail("LinkedList fuzz indexed get mismatch");
                    }

                    LinkedListState<int> state(&list);
                    int *iter_value = 0;
                    int iter_index = 0;
                    while (state.iterate(&iter_value)) {
                        if (iter_index >= shadow_length || *iter_value != shadow[iter_index])
                            UnitTests::fail("LinkedList fuzz iteration mismatch");
                        iter_index++;
                    }

                    if (iter_index != shadow_length)
                        UnitTests::fail("LinkedList fuzz iteration length mismatch");

                    if (list.get(list.firstNode()) != shadow[0])
                        UnitTests::fail("LinkedList fuzz first-node mismatch");

                    if (list.get(list.lastNode()) != shadow[shadow_length - 1])
                        UnitTests::fail("LinkedList fuzz last-node mismatch");
                }

                iterations++;
            }

            if (!iterations)
                UnitTests::fail("LinkedList fuzz did not execute any iterations");

            return true;
        }

        bool testInstancePoolFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int iterations = 0;
            threading::memory::InstancePool<int> pool;
            const int shadow_capacity = 64;
            int available[shadow_capacity];
            int available_count = 0;
            int busy[shadow_capacity];
            threading::memory::InstancePool<int>::HANDLE busy_handles[shadow_capacity];
            int busy_count = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                int op = iterations % 5;
                int value = ((iterations * 53) % 100003) - 50001;

                if (available_count + busy_count >= shadow_capacity)
                    op = busy_count ? 2 : 1;

                if (op == 0 || (!available_count && !busy_count)) {
                    pool.add(value);
                    available[available_count++] = value;
                } else if (op == 1) {
                    threading::memory::InstancePool<int>::HANDLE handle = pool.acquire(false);

                    if (!available_count) {
                        if (handle)
                            UnitTests::fail("InstancePool fuzz acquired from empty pool");
                    } else {
                        if (!handle)
                            UnitTests::fail("InstancePool fuzz failed to acquire available instance");

                        int acquired = pool.getInstance(handle);
                        if (acquired != available[0])
                            UnitTests::fail("InstancePool fuzz acquire returned unexpected instance");

                        for (int i = 0; i < available_count - 1; i++)
                            available[i] = available[i + 1];
                        available_count--;

                        busy[busy_count] = acquired;
                        busy_handles[busy_count] = handle;
                        busy_count++;
                    }
                } else if (op == 2) {
                    if (busy_count) {
                        int index = iterations % busy_count;
                        int released = busy[index];
                        pool.release(busy_handles[index]);

                        available[available_count++] = released;
                        for (int i = index; i < busy_count - 1; i++) {
                            busy[i] = busy[i + 1];
                            busy_handles[i] = busy_handles[i + 1];
                        }
                        busy_count--;
                    } else {
                        if (pool.acquire(false))
                            UnitTests::fail("InstancePool fuzz acquired unexpectedly while no busy handles existed");
                    }
                } else if (op == 3) {
                    if (available_count) {
                        int removed = available[iterations % available_count];
                        pool.remove(removed);

                        bool removed_once = false;
                        int write_index = 0;
                        for (int i = 0; i < available_count; i++) {
                            if (!removed_once && available[i] == removed) {
                                removed_once = true;
                                continue;
                            }
                            available[write_index++] = available[i];
                        }
                        available_count = write_index;
                    } else if (busy_count) {
                        int index = iterations % busy_count;
                        int retired = busy[index];
                        threading::memory::InstancePool<int>::HANDLE handle = busy_handles[index];

                        pool.remove(retired);
                        pool.release(handle);

                        for (int i = index; i < busy_count - 1; i++) {
                            busy[i] = busy[i + 1];
                            busy_handles[i] = busy_handles[i + 1];
                        }
                        busy_count--;
                    }
                } else {
                    if (pool.length() != available_count + busy_count)
                        UnitTests::fail("InstancePool fuzz length mismatch");

                    for (int i = 0; i < pool.length(); i++) {
                        int current = pool.get(i);
                        bool found = false;

                        for (int x = 0; x < available_count; x++) {
                            if (available[x] == current) {
                                found = true;
                                break;
                            }
                        }

                        if (!found) {
                            for (int x = 0; x < busy_count; x++) {
                                if (busy[x] == current) {
                                    found = true;
                                    break;
                                }
                            }
                        }

                        if (!found)
                            UnitTests::fail("InstancePool fuzz get(index) returned unknown instance");
                    }
                }

                if (pool.length() != available_count + busy_count)
                    UnitTests::fail("InstancePool fuzz final length mismatch");

                iterations++;
            }

            while (busy_count) {
                pool.release(busy_handles[busy_count - 1]);
                busy_count--;
            }

            if (!iterations)
                UnitTests::fail("InstancePool fuzz did not execute any iterations");

            return true;
        }

        bool testIndexedDataStoreFuzz(long long duration_us) {
            long long start = getCurrentTimeMicros();
            int batches = 0;

            while ((getCurrentTimeMicros() - start) < duration_us) {
                const int slot_count = 8;
                String keys[slot_count];
                ByteArray values[slot_count];
                bool active[slot_count];
                String path = String("/tmp/elara-indexed-data-store-fuzz-%-%").arg((int)getpid()).arg(batches);
                unlink((char*)path);

                for (int i=0; i<slot_count; i++)
                    active[i] = false;

                IndexedDataStore *store = new IndexedDataStore(path);

                for (int step=0; step<32; step++) {
                    int slot = step % slot_count;
                    keys[slot] = String("fuzz/batch/%/key/%").arg(batches).arg(slot);

                    if (!active[slot]) {
                        ByteArray value = buildIndexedDataStoreValue((batches * 97) + step + slot, (step % 48) + 1);
                        store->set(memoryFromString(keys[slot]), value);
                        values[slot] = value;
                        active[slot] = true;
                    } else {
                        Ref<IndexedDataStore::LOADED_FILE_DESCRIPTOR> file = store->getOrCreateFile(memoryFromString(keys[slot]), (unsigned int)(8 << (step % 3)));
                        ByteArray updated = values[slot];
                        ByteArray patch = buildIndexedDataStoreValue((batches * 193) ^ (step + slot), (step % 17) + 1);
                        unsigned long long offset = updated.length() ? (unsigned long long)(step % (updated.length() + 1)) : 0;

                        if (!store->writeToFile(file, patch, offset, (unsigned long long)patch.length()))
                            UnitTests::fail("IndexedDataStore writeToFile rejected a valid write");

                        int required = (int)(offset + patch.length());
                        if (updated.length() < required)
                            updated.append(required - updated.length());

                        for (int i=0; i<patch.length(); i++)
                            ((char*)updated)[(size_t)offset + i] = ((char*)patch)[i];

                        values[slot] = updated;

                        Memory actual = store->readFromFile(file, 0, (unsigned long long)updated.length());
                        if (!memoryEquals(actual, updated))
                            UnitTests::fail("IndexedDataStore file write/read mismatch");
                    }

                    if ((step % 5) == 4) {
                        verifyIndexedDataStoreShadow(*store, keys, values, active, slot_count);
                        delete store;
                        store = new IndexedDataStore(path);
                        verifyIndexedDataStoreShadow(*store, keys, values, active, slot_count);
                    }
                }

                verifyIndexedDataStoreShadow(*store, keys, values, active, slot_count);
                delete store;
                unlink((char*)path);
                batches++;
            }

            if (!batches)
                UnitTests::fail("IndexedDataStore fuzz did not execute any iterations");

            return true;
        }

    }

    int addRuntimeTests(UnitTests &tests, String selector) {
        int count = 0;

        if (matchesSelector(selector, "bytearray")) {
            tests.addTest("runtime.bytearray.shift", testByteArrayShift);
            tests.addTest("runtime.bytearray.allocation", testByteArrayAllocation);
            count += 2;
        }

        if (matchesSelector(selector, "hashmap")) {
            tests.addTest("runtime.hashmap.lookup", testHashMap);
            count++;
        }

        if (matchesSelector(selector, "ringbuffer")) {
            tests.addTest("runtime.ringbuffer.fetch", testRingBuffer);
            count++;
        }

        if (matchesSelector(selector, "base58")) {
            tests.addTest("runtime.base58.roundtrip", testBase58);
            count++;
        }

        if (matchesSelector(selector, "base64")) {
            tests.addTest("runtime.base64.roundtrip", testBase64);
            count++;
        }

        if (selector == String("runtime.indexeddatastore.integrity")) {
            tests.addTest("runtime.indexeddatastore.integrity", testIndexedDataStoreIntegrityValidator);
            count++;
        } else if (selector == String("runtime.indexeddatastore.crash")) {
            tests.addTest("runtime.indexeddatastore.crash", testIndexedDataStoreCrashConsistencyValidator);
            count++;
        } else if (selector == String("runtime.indexeddatastore.persistence")) {
            tests.addTest("runtime.indexeddatastore.persistence", testIndexedDataStoreValidator);
            count++;
        } else if (matchesSelector(selector, "indexeddatastore")) {
            tests.addTest("runtime.indexeddatastore.persistence", testIndexedDataStoreValidator);
            tests.addTest("runtime.indexeddatastore.integrity", testIndexedDataStoreIntegrityValidator);
            tests.addTest("runtime.indexeddatastore.crash", testIndexedDataStoreCrashConsistencyValidator);
            count++;
            count++;
            count++;
        }

        if (matchesSelector(selector, "stress")) {
            if (selector == String("stress-memory"))
                tests.addStressTest("runtime.stress.heavy", testThreadedMemoryStressHeavy);
            else
                tests.addStressTest("runtime.stress.standard", testThreadedMemoryStress);
            count++;
        }

        if (!selector.length() || selector == String("all") || selector == String("runtime") || selector == String("runtime.fuzz") || selector == String("runtime.fuzz.bytearray")) {
            tests.addFuzzTest("runtime.fuzz.bytearray", testByteArrayFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.hashmap")) {
            tests.addFuzzTest("runtime.fuzz.hashmap", testHashMapFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.ringbuffer")) {
            tests.addFuzzTest("runtime.fuzz.ringbuffer", testRingBufferFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.string")) {
            tests.addFuzzTest("runtime.fuzz.string", testStringFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.linkedlist")) {
            tests.addFuzzTest("runtime.fuzz.linkedlist", testLinkedListFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.instancepool")) {
            tests.addFuzzTest("runtime.fuzz.instancepool", testInstancePoolFuzz);
            count++;
        }

        if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.indexeddatastore")) {
            tests.addFuzzTest("runtime.fuzz.indexeddatastore", testIndexedDataStoreFuzz);
            count++;
        }

        return count;
    }

    void addRuntimeMetadata(UnitTests &tests, String selector) {
        if (selector == String("stress-memory")) {
            tests.addRunMetadata("thread_count", "32");
            tests.addRunMetadata("task_count", "512");
            tests.addRunMetadata("iterations", "512");
        } else if (!selector.length() || selector == String("all") || selector == String("runtime") || selector == String("runtime.stress")) {
            tests.addRunMetadata("thread_count", "16");
            tests.addRunMetadata("task_count", "256");
            tests.addRunMetadata("iterations", "256");
        } else if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.bytearray") || selector == String("runtime.fuzz.hashmap") || selector == String("runtime.fuzz.ringbuffer") || selector == String("runtime.fuzz.string") || selector == String("runtime.fuzz.linkedlist") || selector == String("runtime.fuzz.instancepool") || selector == String("runtime.fuzz.indexeddatastore")) {
            if (selector == String("runtime.fuzz.bytearray"))
                tests.addRunMetadata("fuzz_target", "bytearray");
            else if (selector == String("runtime.fuzz.hashmap"))
                tests.addRunMetadata("fuzz_target", "hashmap");
            else if (selector == String("runtime.fuzz.ringbuffer"))
                tests.addRunMetadata("fuzz_target", "ringbuffer");
            else if (selector == String("runtime.fuzz.string"))
                tests.addRunMetadata("fuzz_target", "string");
            else if (selector == String("runtime.fuzz.linkedlist"))
                tests.addRunMetadata("fuzz_target", "linkedlist");
            else if (selector == String("runtime.fuzz.instancepool"))
                tests.addRunMetadata("fuzz_target", "instancepool");
            else if (selector == String("runtime.fuzz.indexeddatastore"))
                tests.addRunMetadata("fuzz_target", "indexeddatastore");
            else
                tests.addRunMetadata("fuzz_target", "multi");
        }
    }

}
