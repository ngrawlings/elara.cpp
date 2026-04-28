#include "RuntimeTests.h"

#include <libelaracore/encoding/Base58.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/RingBuffer.h>
#include <libelaracore/memory/String.h>

#include <libelarathreads/Task.h>
#include <libelarathreads/Thread.h>
#include <libelarathreads/memory/Ref.h>

#include <atomic>
#include <unistd.h>

namespace elara {

    namespace {

        bool matchesSelector(String selector, String name) {
            if (!selector.length() || selector == String("all"))
                return true;

            if (selector == String("runtime"))
                return true;

            if (selector == String("stress-memory"))
                return name == String("stress");

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

        bool runThreadedMemoryStress(int thread_count, int task_count, int iterations) {
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

        bool testThreadedMemoryStress() {
            return runThreadedMemoryStress(16, 512, 512);
        }

        bool testThreadedMemoryStressHeavy() {
            return runThreadedMemoryStress(32, 2048, 1024);
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

        if (matchesSelector(selector, "stress")) {
            if (selector == String("stress-memory"))
                tests.addTest("runtime.stress.heavy", testThreadedMemoryStressHeavy);
            else
                tests.addTest("runtime.stress.standard", testThreadedMemoryStress);
            count++;
        }

        return count;
    }

    void addRuntimeMetadata(UnitTests &tests, String selector) {
        if (selector == String("stress-memory")) {
            tests.addRunMetadata("thread_count", "32");
            tests.addRunMetadata("task_count", "2048");
            tests.addRunMetadata("iterations", "1024");
        } else if (!selector.length() || selector == String("all") || selector == String("runtime") || selector == String("runtime.stress")) {
            tests.addRunMetadata("thread_count", "16");
            tests.addRunMetadata("task_count", "512");
            tests.addRunMetadata("iterations", "512");
        }
    }

}
