//
//  BRpcTests.cpp
//  UnitTests
//
// Full coverage tests for the BRpcCodec encoder / decoder.
//

#include "BRpcTests.h"

#include <libelarasockets/rpc/brpc/BRpcCodec.h>
#include <libelaradebug/UnitTests.h>
#include <string.h>

using namespace elara;
using namespace elara::sockets::rpc::brpc;

// ── helpers ───────────────────────────────────────────────────────────────────

static bool streq(const String& a, const char* b) {
    if (!b) return a.length() == 0;
    return a.length() == (ssize_t)strlen(b)
        && memcmp((const char*)a, b, a.length()) == 0;
}

// ── Unnamed scalars ───────────────────────────────────────────────────────────

bool brpc_roundtrip_byte() {
    BRpcWriter w;
    w.writeByte(0xAB);

    BRpcReader r(w.bytes());
    uint8_t v = 0;
    if (!r.readByte(v)) return UnitTests::fail("readByte returned false");
    if (v != 0xAB)     return UnitTests::fail("byte value mismatch");
    if (!r.atEnd())    return UnitTests::fail("trailing bytes after byte");
    return true;
}

bool brpc_roundtrip_short() {
    BRpcWriter w;
    w.writeShort(-1234);

    BRpcReader r(w.bytes());
    int16_t v = 0;
    if (!r.readShort(v))  return UnitTests::fail("readShort returned false");
    if (v != -1234)       return UnitTests::fail("short value mismatch");
    if (!r.atEnd())       return UnitTests::fail("trailing bytes after short");
    return true;
}

bool brpc_roundtrip_int() {
    BRpcWriter w;
    w.writeInt(0x12345678);

    BRpcReader r(w.bytes());
    int32_t v = 0;
    if (!r.readInt(v))          return UnitTests::fail("readInt returned false");
    if (v != 0x12345678)        return UnitTests::fail("int value mismatch");
    if (!r.atEnd())             return UnitTests::fail("trailing bytes after int");
    return true;
}

bool brpc_roundtrip_long() {
    BRpcWriter w;
    int64_t original = (int64_t)0xDEADBEEFCAFEBABELL;
    w.writeLong(original);

    BRpcReader r(w.bytes());
    int64_t v = 0;
    if (!r.readLong(v))   return UnitTests::fail("readLong returned false");
    if (v != original)    return UnitTests::fail("long value mismatch");
    if (!r.atEnd())       return UnitTests::fail("trailing bytes after long");
    return true;
}

bool brpc_roundtrip_string() {
    BRpcWriter w;
    w.writeString(String("hello, world"));

    BRpcReader r(w.bytes());
    String v;
    if (!r.readString(v))          return UnitTests::fail("readString returned false");
    if (!streq(v, "hello, world")) return UnitTests::fail("string value mismatch");
    if (!r.atEnd())                return UnitTests::fail("trailing bytes after string");
    return true;
}

bool brpc_roundtrip_empty_string() {
    BRpcWriter w;
    w.writeString(String(""));

    BRpcReader r(w.bytes());
    String v;
    if (!r.readString(v)) return UnitTests::fail("readString (empty) returned false");
    if (v.length() != 0)  return UnitTests::fail("empty string has non-zero length");
    if (!r.atEnd())       return UnitTests::fail("trailing bytes after empty string");
    return true;
}

// ── Named scalars ─────────────────────────────────────────────────────────────

bool brpc_roundtrip_named_byte() {
    BRpcWriter w;
    w.writeNamedByte(String("flag"), 0x01);

    BRpcReader r(w.bytes());
    String name;
    uint8_t v = 0;
    if (!r.readNamedByte(name, v)) return UnitTests::fail("readNamedByte returned false");
    if (!streq(name, "flag"))      return UnitTests::fail("named byte name mismatch");
    if (v != 0x01)                 return UnitTests::fail("named byte value mismatch");
    if (!r.atEnd())                return UnitTests::fail("trailing bytes after named byte");
    return true;
}

bool brpc_roundtrip_named_short() {
    BRpcWriter w;
    w.writeNamedShort(String("port"), 8080);

    BRpcReader r(w.bytes());
    String name;
    int16_t v = 0;
    if (!r.readNamedShort(name, v)) return UnitTests::fail("readNamedShort returned false");
    if (!streq(name, "port"))       return UnitTests::fail("named short name mismatch");
    if (v != 8080)                  return UnitTests::fail("named short value mismatch");
    if (!r.atEnd())                 return UnitTests::fail("trailing bytes after named short");
    return true;
}

bool brpc_roundtrip_named_int() {
    BRpcWriter w;
    w.writeNamedInt(String("count"), -999);

    BRpcReader r(w.bytes());
    String name;
    int32_t v = 0;
    if (!r.readNamedInt(name, v)) return UnitTests::fail("readNamedInt returned false");
    if (!streq(name, "count"))    return UnitTests::fail("named int name mismatch");
    if (v != -999)                return UnitTests::fail("named int value mismatch");
    if (!r.atEnd())               return UnitTests::fail("trailing bytes after named int");
    return true;
}

bool brpc_roundtrip_named_long() {
    BRpcWriter w;
    int64_t ts = (int64_t)1700000000000LL;
    w.writeNamedLong(String("timestamp"), ts);

    BRpcReader r(w.bytes());
    String name;
    int64_t v = 0;
    if (!r.readNamedLong(name, v))  return UnitTests::fail("readNamedLong returned false");
    if (!streq(name, "timestamp"))  return UnitTests::fail("named long name mismatch");
    if (v != ts)                    return UnitTests::fail("named long value mismatch");
    if (!r.atEnd())                 return UnitTests::fail("trailing bytes after named long");
    return true;
}

bool brpc_roundtrip_named_string() {
    BRpcWriter w;
    w.writeNamedString(String("greeting"), String("bonjour"));

    BRpcReader r(w.bytes());
    String name, v;
    if (!r.readNamedString(name, v)) return UnitTests::fail("readNamedString returned false");
    if (!streq(name, "greeting"))    return UnitTests::fail("named string name mismatch");
    if (!streq(v, "bonjour"))        return UnitTests::fail("named string value mismatch");
    if (!r.atEnd())                  return UnitTests::fail("trailing bytes after named string");
    return true;
}

// ── Arrays ────────────────────────────────────────────────────────────────────

bool brpc_roundtrip_array_unnamed() {
    // [42, -7, 1000]
    BRpcWriter elems;
    elems.writeByte(42);
    elems.writeShort(-7);
    elems.writeInt(1000);

    BRpcWriter doc;
    doc.writeArray(elems, 3);

    BRpcReader r(doc.bytes());
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return UnitTests::fail("readArrayHeader failed");
    if (count != 3)                       return UnitTests::fail("element count mismatch");

    uint8_t  b; if (!r.readByte(b)  || b !=  42) return UnitTests::fail("array elem 0 mismatch");
    int16_t  s; if (!r.readShort(s) || s !=  -7) return UnitTests::fail("array elem 1 mismatch");
    int32_t  i; if (!r.readInt(i)   || i != 1000) return UnitTests::fail("array elem 2 mismatch");
    if (!r.atEnd()) return UnitTests::fail("trailing bytes after unnamed array");
    return true;
}

bool brpc_roundtrip_object() {
    // {"x": 42, "label": "test", "active": 1 (byte), "score": -32768 (short)}
    BRpcWriter fields;
    fields.writeNamedInt(String("x"), 42);
    fields.writeNamedString(String("label"), String("test"));
    fields.writeNamedByte(String("active"), 1);
    fields.writeNamedShort(String("score"), -32768);

    BRpcWriter doc;
    doc.writeArray(fields, 4);

    BRpcReader r(doc.bytes());
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return UnitTests::fail("object readArrayHeader failed");
    if (count != 4) return UnitTests::fail("object field count mismatch");

    {
        String name; int32_t v;
        if (!r.readNamedInt(name, v)) return UnitTests::fail("object field 0 read failed");
        if (!streq(name, "x"))        return UnitTests::fail("object field 0 name mismatch");
        if (v != 42)                  return UnitTests::fail("object field 0 value mismatch");
    }
    {
        String name, v;
        if (!r.readNamedString(name, v)) return UnitTests::fail("object field 1 read failed");
        if (!streq(name, "label"))       return UnitTests::fail("object field 1 name mismatch");
        if (!streq(v, "test"))           return UnitTests::fail("object field 1 value mismatch");
    }
    {
        String name; uint8_t v;
        if (!r.readNamedByte(name, v)) return UnitTests::fail("object field 2 read failed");
        if (!streq(name, "active"))    return UnitTests::fail("object field 2 name mismatch");
        if (v != 1)                    return UnitTests::fail("object field 2 value mismatch");
    }
    {
        String name; int16_t v;
        if (!r.readNamedShort(name, v)) return UnitTests::fail("object field 3 read failed");
        if (!streq(name, "score"))      return UnitTests::fail("object field 3 name mismatch");
        if (v != -32768)                return UnitTests::fail("object field 3 value mismatch");
    }

    if (!r.atEnd()) return UnitTests::fail("trailing bytes after object");
    return true;
}

bool brpc_roundtrip_nested_arrays() {
    // outer array contains an inner array of two ints
    BRpcWriter inner_elems;
    inner_elems.writeInt(11);
    inner_elems.writeInt(22);

    BRpcWriter outer_elems;
    outer_elems.writeArray(inner_elems, 2);
    outer_elems.writeInt(99);

    BRpcWriter doc;
    doc.writeArray(outer_elems, 2);  // 2 items: an array + an int

    BRpcReader r(doc.bytes());
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return UnitTests::fail("outer readArrayHeader failed");
    if (count != 2) return UnitTests::fail("outer count mismatch");

    // first element is the inner array
    uint32_t inner_total, inner_count;
    if (!r.readArrayHeader(inner_total, inner_count)) return UnitTests::fail("inner readArrayHeader failed");
    if (inner_count != 2) return UnitTests::fail("inner count mismatch");
    int32_t a, b;
    if (!r.readInt(a) || a != 11) return UnitTests::fail("inner elem 0 mismatch");
    if (!r.readInt(b) || b != 22) return UnitTests::fail("inner elem 1 mismatch");

    // second outer element
    int32_t c;
    if (!r.readInt(c) || c != 99) return UnitTests::fail("outer elem 1 mismatch");

    if (!r.atEnd()) return UnitTests::fail("trailing bytes after nested arrays");
    return true;
}

bool brpc_roundtrip_empty_array() {
    BRpcWriter empty;
    BRpcWriter doc;
    doc.writeArray(empty, 0);

    BRpcReader r(doc.bytes());
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return UnitTests::fail("empty array header failed");
    if (total != 0) return UnitTests::fail("empty array total_bytes != 0");
    if (count != 0) return UnitTests::fail("empty array count != 0");
    if (!r.atEnd()) return UnitTests::fail("trailing bytes after empty array");
    return true;
}

bool brpc_roundtrip_mixed_object() {
    // Simulate a JSON-like message:
    // { "method": "ui.event", "id": 7, "ts": 1700000000000LL, "ok": 1 }
    BRpcWriter fields;
    fields.writeNamedString(String("method"), String("ui.event"));
    fields.writeNamedInt(String("id"), 7);
    fields.writeNamedLong(String("ts"), (int64_t)1700000000000LL);
    fields.writeNamedByte(String("ok"), 1);

    BRpcWriter doc;
    doc.writeArray(fields, 4);

    BRpcReader r(doc.bytes());
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return UnitTests::fail("mixed object header failed");
    if (count != 4) return UnitTests::fail("mixed object count mismatch");

    String name;
    {
        String v;
        if (!r.readNamedString(name, v))   return UnitTests::fail("method field failed");
        if (!streq(name, "method"))        return UnitTests::fail("method name mismatch");
        if (!streq(v, "ui.event"))         return UnitTests::fail("method value mismatch");
    }
    {
        int32_t v;
        if (!r.readNamedInt(name, v))      return UnitTests::fail("id field failed");
        if (!streq(name, "id"))            return UnitTests::fail("id name mismatch");
        if (v != 7)                        return UnitTests::fail("id value mismatch");
    }
    {
        int64_t v;
        if (!r.readNamedLong(name, v))     return UnitTests::fail("ts field failed");
        if (!streq(name, "ts"))            return UnitTests::fail("ts name mismatch");
        if (v != (int64_t)1700000000000LL) return UnitTests::fail("ts value mismatch");
    }
    {
        uint8_t v;
        if (!r.readNamedByte(name, v))     return UnitTests::fail("ok field failed");
        if (!streq(name, "ok"))            return UnitTests::fail("ok name mismatch");
        if (v != 1)                        return UnitTests::fail("ok value mismatch");
    }

    if (!r.atEnd()) return UnitTests::fail("trailing bytes after mixed object");
    return true;
}

// ── Sequential reads ──────────────────────────────────────────────────────────

bool brpc_sequential_reads() {
    // Encode 5 different values back-to-back in a flat buffer, read them all.
    BRpcWriter w;
    w.writeByte(0xFF);
    w.writeShort(256);
    w.writeInt(-1);
    w.writeLong((int64_t)0x0102030405060708LL);
    w.writeString(String("seq"));

    BRpcReader r(w.bytes());

    uint8_t  b; if (!r.readByte(b)   || b != 0xFF)                        return UnitTests::fail("seq byte");
    int16_t  s; if (!r.readShort(s)  || s != 256)                         return UnitTests::fail("seq short");
    int32_t  i; if (!r.readInt(i)    || i != -1)                          return UnitTests::fail("seq int");
    int64_t  l; if (!r.readLong(l)   || l != (int64_t)0x0102030405060708LL) return UnitTests::fail("seq long");
    String   sv; if (!r.readString(sv) || !streq(sv, "seq"))              return UnitTests::fail("seq string");
    if (!r.atEnd()) return UnitTests::fail("seq trailing bytes");
    return true;
}

// ── skipValue ─────────────────────────────────────────────────────────────────

bool brpc_skip_value() {
    // Write several values, skip the middle ones, only read first and last.
    BRpcWriter w;
    w.writeInt(111);
    w.writeString(String("skip me"));
    w.writeLong((int64_t)9999999999LL);
    w.writeShort(222);
    w.writeByte(33);
    w.writeInt(999);

    BRpcReader r(w.bytes());

    // Read first
    int32_t first;
    if (!r.readInt(first) || first != 111) return UnitTests::fail("skip first");

    // Skip string, long, short, byte
    if (!r.skipValue()) return UnitTests::fail("skip string");
    if (!r.skipValue()) return UnitTests::fail("skip long");
    if (!r.skipValue()) return UnitTests::fail("skip short");
    if (!r.skipValue()) return UnitTests::fail("skip byte");

    // Read last
    int32_t last;
    if (!r.readInt(last) || last != 999) return UnitTests::fail("skip last");
    if (!r.atEnd()) return UnitTests::fail("skip trailing bytes");
    return true;
}

// ── Boundary values ───────────────────────────────────────────────────────────

bool brpc_boundary_values() {
    BRpcWriter w;
    w.writeByte(0);
    w.writeByte(255);
    w.writeShort(-32768);
    w.writeShort(32767);
    w.writeInt((int32_t)0x80000000);
    w.writeInt((int32_t)0x7FFFFFFF);
    w.writeLong((int64_t)0x8000000000000000LL);
    w.writeLong((int64_t)0x7FFFFFFFFFFFFFFFLL);

    BRpcReader r(w.bytes());
    uint8_t b; int16_t s; int32_t i; int64_t l;

    if (!r.readByte(b)  || b != 0)                         return UnitTests::fail("min byte");
    if (!r.readByte(b)  || b != 255)                       return UnitTests::fail("max byte");
    if (!r.readShort(s) || s != -32768)                    return UnitTests::fail("min short");
    if (!r.readShort(s) || s != 32767)                     return UnitTests::fail("max short");
    if (!r.readInt(i)   || i != (int32_t)0x80000000)       return UnitTests::fail("min int");
    if (!r.readInt(i)   || i != (int32_t)0x7FFFFFFF)       return UnitTests::fail("max int");
    if (!r.readLong(l)  || l != (int64_t)0x8000000000000000LL) return UnitTests::fail("min long");
    if (!r.readLong(l)  || l != (int64_t)0x7FFFFFFFFFFFFFFFLL) return UnitTests::fail("max long");
    if (!r.atEnd()) return UnitTests::fail("boundary trailing bytes");
    return true;
}

// ── Error / truncation ────────────────────────────────────────────────────────

bool brpc_truncated_input() {
    // A valid int is 5 bytes (1 type + 4 value).
    // Supply only 3 bytes — readInt must fail.
    uint8_t buf[3] = { (uint8_t)BRPC_INT, 0x00, 0x00 };
    BRpcReader r((const char*)buf, 3);
    int32_t v;
    if (r.readInt(v)) return UnitTests::fail("truncated int should have failed");

    // A string with claimed length 100 but only 2 actual content bytes — must fail.
    BRpcWriter w2;
    w2.writeString(String("hi"));
    // Corrupt the length field to claim 100 bytes
    const ByteArray& good = w2.bytes();
    // Byte layout: [type=4] [len_hi=0][len=0][len=0][len=2] [h][i]
    // We'll just build a raw truncated buffer manually.
    uint8_t buf2[6] = {
        (uint8_t)BRPC_STRING,
        0x00, 0x00, 0x00, 100,  // length = 100
        'x'                     // only 1 actual byte
    };
    BRpcReader r2((const char*)buf2, 6);
    String sv;
    if (r2.readString(sv)) return UnitTests::fail("truncated string should have failed");

    return true;
}

bool brpc_wrong_type_rejected() {
    // Write an int, try to read it as a string — must fail.
    BRpcWriter w;
    w.writeInt(42);

    BRpcReader r(w.bytes());
    String sv;
    if (r.readString(sv)) return UnitTests::fail("wrong type (int→string) should fail");

    // Position should be unchanged (type byte not consumed on failure).
    // Verify we can still read it as the correct type.
    int32_t iv;
    if (!r.readInt(iv) || iv != 42) return UnitTests::fail("re-read after type error failed");
    return true;
}

// ── Wire layout verification ──────────────────────────────────────────────────

bool brpc_wire_layout_byte() {
    // Byte 0xAB: [0x00] [0xAB]  — exactly 2 bytes
    BRpcWriter w;
    w.writeByte(0xAB);
    const ByteArray& b = w.bytes();
    if (b.length() != 2)                    return UnitTests::fail("byte wire length != 2");
    if ((uint8_t)((const char*)b)[0] != 0x00) return UnitTests::fail("byte type byte != 0x00");
    if ((uint8_t)((const char*)b)[1] != 0xAB) return UnitTests::fail("byte value wire != 0xAB");
    return true;
}

bool brpc_wire_layout_array_header() {
    // Empty array: [0x05] [0x00 0x00 0x00 0x00] [0x00 0x00 0x00 0x00] — 9 bytes
    BRpcWriter empty;
    BRpcWriter doc;
    doc.writeArray(empty, 0);
    const ByteArray& b = doc.bytes();
    if (b.length() != 9) return UnitTests::fail("empty array wire length != 9");
    const uint8_t* p = (const uint8_t*)(const char*)b;
    if (p[0] != 0x05) return UnitTests::fail("array type byte != 0x05");
    // total_bytes = 0
    if (p[1] || p[2] || p[3] || p[4]) return UnitTests::fail("array total_bytes not zero");
    // element_count = 0
    if (p[5] || p[6] || p[7] || p[8]) return UnitTests::fail("array elem_count not zero");
    return true;
}
