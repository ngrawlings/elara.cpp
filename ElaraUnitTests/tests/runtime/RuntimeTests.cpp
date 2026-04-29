#include "RuntimeTests.h"

#include <libelaracore/encoding/Base58.h>
#include <libelaracore/encoding/Base64.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/LinkedList.h>
#include <libelaracore/memory/RingBuffer.h>
#include <libelaracore/memory/String.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelarathreads/memory/InstancePool.h>
#include <libelarathreads/memory/Ref.h>

#include <atomic>
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
        } else if (selector == String("runtime.fuzz") || selector == String("runtime.fuzz.bytearray") || selector == String("runtime.fuzz.hashmap") || selector == String("runtime.fuzz.ringbuffer") || selector == String("runtime.fuzz.string") || selector == String("runtime.fuzz.linkedlist") || selector == String("runtime.fuzz.instancepool")) {
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
            else
                tests.addRunMetadata("fuzz_target", "multi");
        }
    }

}
