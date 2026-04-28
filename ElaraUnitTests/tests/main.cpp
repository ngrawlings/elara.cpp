#include <libelaracore/encoding/Base58.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/HashMap.h>
#include <libelaracore/memory/RingBuffer.h>
#include <libelaracore/memory/String.h>

#include <libelaradebug/UnitTests.h>

using namespace elara;

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
    if (map.get(key).get() != String("1024"))
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

    if (dec != src)
        UnitTests::fail("Base58 encode/decode failed");

    return true;
}

int main(int argc, const char *argv[]) {
    UnitTests tests;

    tests.addTest("testByteArrayShift", testByteArrayShift);
    tests.addTest("testByteArrayAllocation", testByteArrayAllocation);
    tests.addTest("testHashMap", testHashMap);
    tests.addTest("testRingBuffer", testRingBuffer);
    tests.addTest("testBase58", testBase58);

    return tests.run() ? 0 : 1;
}
