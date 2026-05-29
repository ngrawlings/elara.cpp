//
//  BRpcCodec.h
//  libElaraSockets
//
// Binary RPC codec — a compact binary replacement for JSON in RPC payloads.
//
// Type codes
// ──────────
//  Unnamed scalars
//   0  byte      — 1 byte value
//   1  short     — 2 byte value (big-endian)
//   2  int32     — 4 byte value (big-endian)
//   3  int64     — 8 byte value (big-endian)
//   4  string    — uint32 length prefix (big-endian) + UTF-8 bytes
//   5  array     — uint32 total-element-bytes (big-endian)
//               + uint32 element-count (big-endian)
//               + element bytes
//
//  Named scalars (key–value pairs; name is length-prefixed UTF-8, no type byte)
//  10  named byte
//  11  named short
//  12  named int32
//  13  named int64
//  14  named string
//
// A JSON-object equivalent is an array (type 5) of named values (types 10–14).
//

#ifndef ElaraSockets_BRpcCodec_h
#define ElaraSockets_BRpcCodec_h

#include <stdint.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/String.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

enum BRpcType : uint8_t {
    BRPC_BYTE         = 0,
    BRPC_SHORT        = 1,
    BRPC_INT          = 2,
    BRPC_LONG         = 3,
    BRPC_STRING       = 4,
    BRPC_ARRAY        = 5,
    BRPC_NAMED_BYTE   = 10,
    BRPC_NAMED_SHORT  = 11,
    BRPC_NAMED_INT    = 12,
    BRPC_NAMED_LONG   = 13,
    BRPC_NAMED_STRING = 14,
};

// ── Writer ────────────────────────────────────────────────────────────────────
//
// Streaming encoder.  Build an object-equivalent like this:
//
//   BRpcWriter fields;
//   fields.writeNamedInt("x", 42);
//   fields.writeNamedString("label", "hello");
//
//   BRpcWriter doc;
//   doc.writeArray(fields, 2);             // 2 named fields
//   send(doc.bytes(), doc.bytes().length());
//
class BRpcWriter {
public:
    BRpcWriter() {}

    // Unnamed scalars ─────────────────────────────────────────────────────────
    void writeByte(uint8_t v);
    void writeShort(int16_t v);
    void writeInt(int32_t v);
    void writeLong(int64_t v);
    void writeString(const String& v);

    // Named scalars ───────────────────────────────────────────────────────────
    void writeNamedByte(const String& name, uint8_t v);
    void writeNamedShort(const String& name, int16_t v);
    void writeNamedInt(const String& name, int32_t v);
    void writeNamedLong(const String& name, int64_t v);
    void writeNamedString(const String& name, const String& v);

    // Array ───────────────────────────────────────────────────────────────────
    // Wraps the bytes from |elements| in an array header.
    // element_count is the caller's count of top-level items inside elements.
    void writeArray(const BRpcWriter& elements, uint32_t element_count);

    const ByteArray& bytes() const { return _buf; }
    void clear();

private:
    ByteArray _buf;

    void appendU8(uint8_t v);
    void appendU16(uint16_t v);
    void appendU32(uint32_t v);
    void appendU64(uint64_t v);
    void appendStringBody(const String& s);  // length-prefix + bytes, no type byte
    void appendNamePrefix(const String& name);
};

// ── Reader ────────────────────────────────────────────────────────────────────
//
// Streaming decoder.  Reads one value at a time from a flat byte buffer.
//
//   BRpcReader r(buf, len);
//   uint8_t type;
//   while (!r.atEnd() && r.peekType(type)) {
//       if (type == BRPC_ARRAY) {
//           uint32_t total, count;
//           r.readArrayHeader(total, count);
//           for (uint32_t i = 0; i < count; i++) {
//               String name;  int32_t v;
//               r.readNamedInt(name, v);
//               ...
//           }
//       }
//   }
//
class BRpcReader {
public:
    BRpcReader(const char* data, size_t len);
    explicit BRpcReader(const ByteArray& data);

    bool atEnd() const;
    bool peekType(uint8_t& type) const;

    // Each read*() call consumes the type byte plus the value bytes.

    // Unnamed scalars ─────────────────────────────────────────────────────────
    bool readByte(uint8_t& v);
    bool readShort(int16_t& v);
    bool readInt(int32_t& v);
    bool readLong(int64_t& v);
    bool readString(String& v);

    // Named scalars ───────────────────────────────────────────────────────────
    bool readNamedByte(String& name, uint8_t& v);
    bool readNamedShort(String& name, int16_t& v);
    bool readNamedInt(String& name, int32_t& v);
    bool readNamedLong(String& name, int64_t& v);
    bool readNamedString(String& name, String& v);

    // Array header ────────────────────────────────────────────────────────────
    // Consumes type byte (0x05) + 8-byte header.
    // Caller then reads element_count values from this reader.
    bool readArrayHeader(uint32_t& total_bytes, uint32_t& element_count);

    // Skip the value at the current position (type byte + payload).
    bool skipValue();

    size_t position() const { return _pos; }

private:
    const char* _data;
    size_t      _len;
    size_t      _pos;

    bool readU8(uint8_t& v);
    bool readU16(uint16_t& v);
    bool readU32(uint32_t& v);
    bool readU64(uint64_t& v);
    bool readStringBody(String& v);   // reads length-prefix + bytes
    bool readNameBody(String& name);  // same as readStringBody
};

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara

#endif  // ElaraSockets_BRpcCodec_h
