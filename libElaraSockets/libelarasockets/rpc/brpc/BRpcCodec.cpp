//
//  BRpcCodec.cpp
//  libElaraSockets
//

#include "BRpcCodec.h"

#include <string.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

// ── BRpcWriter private helpers ────────────────────────────────────────────────

void BRpcWriter::appendU8(uint8_t v) {
    _buf.append(&v, 1);
}

void BRpcWriter::appendU16(uint16_t v) {
    uint8_t b[2] = {
        (uint8_t)(v >> 8),
        (uint8_t)(v     ),
    };
    _buf.append(b, 2);
}

void BRpcWriter::appendU32(uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)(v >> 24),
        (uint8_t)(v >> 16),
        (uint8_t)(v >>  8),
        (uint8_t)(v      ),
    };
    _buf.append(b, 4);
}

void BRpcWriter::appendU64(uint64_t v) {
    uint8_t b[8] = {
        (uint8_t)(v >> 56),
        (uint8_t)(v >> 48),
        (uint8_t)(v >> 40),
        (uint8_t)(v >> 32),
        (uint8_t)(v >> 24),
        (uint8_t)(v >> 16),
        (uint8_t)(v >>  8),
        (uint8_t)(v      ),
    };
    _buf.append(b, 8);
}

void BRpcWriter::appendStringBody(const String& s) {
    uint32_t len = (uint32_t)s.length();
    appendU32(len);
    if (len > 0)
        _buf.append((const char*)s, (int)len);
}

void BRpcWriter::appendNamePrefix(const String& name) {
    appendStringBody(name);
}

// ── BRpcWriter public ─────────────────────────────────────────────────────────

void BRpcWriter::clear() {
    _buf.clear();
}

void BRpcWriter::writeByte(uint8_t v) {
    appendU8(BRPC_BYTE);
    appendU8(v);
}

void BRpcWriter::writeShort(int16_t v) {
    appendU8(BRPC_SHORT);
    appendU16((uint16_t)v);
}

void BRpcWriter::writeInt(int32_t v) {
    appendU8(BRPC_INT);
    appendU32((uint32_t)v);
}

void BRpcWriter::writeLong(int64_t v) {
    appendU8(BRPC_LONG);
    appendU64((uint64_t)v);
}

void BRpcWriter::writeString(const String& v) {
    appendU8(BRPC_STRING);
    appendStringBody(v);
}

void BRpcWriter::writeNamedByte(const String& name, uint8_t v) {
    appendU8(BRPC_NAMED_BYTE);
    appendNamePrefix(name);
    appendU8(v);
}

void BRpcWriter::writeNamedShort(const String& name, int16_t v) {
    appendU8(BRPC_NAMED_SHORT);
    appendNamePrefix(name);
    appendU16((uint16_t)v);
}

void BRpcWriter::writeNamedInt(const String& name, int32_t v) {
    appendU8(BRPC_NAMED_INT);
    appendNamePrefix(name);
    appendU32((uint32_t)v);
}

void BRpcWriter::writeNamedLong(const String& name, int64_t v) {
    appendU8(BRPC_NAMED_LONG);
    appendNamePrefix(name);
    appendU64((uint64_t)v);
}

void BRpcWriter::writeNamedString(const String& name, const String& v) {
    appendU8(BRPC_NAMED_STRING);
    appendNamePrefix(name);
    appendStringBody(v);
}

void BRpcWriter::writeArray(const BRpcWriter& elements, uint32_t element_count) {
    const ByteArray& elem_bytes = elements.bytes();
    uint32_t total = (uint32_t)elem_bytes.length();

    appendU8(BRPC_ARRAY);
    appendU32(total);
    appendU32(element_count);

    if (total > 0)
        _buf.append(elem_bytes);
}

// ── BRpcReader private helpers ────────────────────────────────────────────────

bool BRpcReader::readU8(uint8_t& v) {
    if (_pos >= _len) return false;
    v = (uint8_t)_data[_pos++];
    return true;
}

bool BRpcReader::readU16(uint16_t& v) {
    if (_pos + 2 > _len) return false;
    const uint8_t* p = (const uint8_t*)(_data + _pos);
    v = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    _pos += 2;
    return true;
}

bool BRpcReader::readU32(uint32_t& v) {
    if (_pos + 4 > _len) return false;
    const uint8_t* p = (const uint8_t*)(_data + _pos);
    v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
      | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    _pos += 4;
    return true;
}

bool BRpcReader::readU64(uint64_t& v) {
    if (_pos + 8 > _len) return false;
    const uint8_t* p = (const uint8_t*)(_data + _pos);
    v = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48)
      | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
      | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
      | ((uint64_t)p[6] <<  8) |  (uint64_t)p[7];
    _pos += 8;
    return true;
}

bool BRpcReader::readStringBody(String& v) {
    uint32_t len;
    if (!readU32(len)) return false;
    if (_pos + len > _len) return false;

    if (len == 0) {
        v = String("");
    } else {
        v = String(_data + _pos, len);
        _pos += len;
    }
    return true;
}

bool BRpcReader::readNameBody(String& name) {
    return readStringBody(name);
}

// ── BRpcReader public ─────────────────────────────────────────────────────────

BRpcReader::BRpcReader(const char* data, size_t len)
    : _data(data), _len(len), _pos(0) {}

BRpcReader::BRpcReader(const ByteArray& data)
    : _data((const char*)data), _len((size_t)data.length()), _pos(0) {}

bool BRpcReader::atEnd() const {
    return _pos >= _len;
}

bool BRpcReader::peekType(uint8_t& type) const {
    if (_pos >= _len) return false;
    type = (uint8_t)_data[_pos];
    return true;
}

// All read*() methods peek the type byte first and only advance _pos once the
// type matches.  A type mismatch leaves _pos unchanged so the caller can retry
// with a different read or call skipValue().

bool BRpcReader::readByte(uint8_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_BYTE) return false;
    _pos++;
    return readU8(v);
}

bool BRpcReader::readShort(int16_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_SHORT) return false;
    _pos++;
    uint16_t raw;
    if (!readU16(raw)) return false;
    v = (int16_t)raw;
    return true;
}

bool BRpcReader::readInt(int32_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_INT) return false;
    _pos++;
    uint32_t raw;
    if (!readU32(raw)) return false;
    v = (int32_t)raw;
    return true;
}

bool BRpcReader::readLong(int64_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_LONG) return false;
    _pos++;
    uint64_t raw;
    if (!readU64(raw)) return false;
    v = (int64_t)raw;
    return true;
}

bool BRpcReader::readString(String& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_STRING) return false;
    _pos++;
    return readStringBody(v);
}

bool BRpcReader::readNamedByte(String& name, uint8_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_NAMED_BYTE) return false;
    _pos++;
    if (!readNameBody(name)) return false;
    return readU8(v);
}

bool BRpcReader::readNamedShort(String& name, int16_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_NAMED_SHORT) return false;
    _pos++;
    if (!readNameBody(name)) return false;
    uint16_t raw;
    if (!readU16(raw)) return false;
    v = (int16_t)raw;
    return true;
}

bool BRpcReader::readNamedInt(String& name, int32_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_NAMED_INT) return false;
    _pos++;
    if (!readNameBody(name)) return false;
    uint32_t raw;
    if (!readU32(raw)) return false;
    v = (int32_t)raw;
    return true;
}

bool BRpcReader::readNamedLong(String& name, int64_t& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_NAMED_LONG) return false;
    _pos++;
    if (!readNameBody(name)) return false;
    uint64_t raw;
    if (!readU64(raw)) return false;
    v = (int64_t)raw;
    return true;
}

bool BRpcReader::readNamedString(String& name, String& v) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_NAMED_STRING) return false;
    _pos++;
    if (!readNameBody(name)) return false;
    return readStringBody(v);
}

bool BRpcReader::readArrayHeader(uint32_t& total_bytes, uint32_t& element_count) {
    if (_pos >= _len || (uint8_t)_data[_pos] != BRPC_ARRAY) return false;
    _pos++;
    if (!readU32(total_bytes)) return false;
    if (!readU32(element_count)) return false;
    return true;
}

bool BRpcReader::skipValue() {
    uint8_t type;
    if (!peekType(type)) return false;
    _pos++;  // consume type byte

    switch (type) {
    case BRPC_BYTE:
        if (_pos + 1 > _len) return false;
        _pos += 1;
        return true;

    case BRPC_SHORT:
        if (_pos + 2 > _len) return false;
        _pos += 2;
        return true;

    case BRPC_INT:
        if (_pos + 4 > _len) return false;
        _pos += 4;
        return true;

    case BRPC_LONG:
        if (_pos + 8 > _len) return false;
        _pos += 8;
        return true;

    case BRPC_STRING: {
        uint32_t len;
        if (!readU32(len)) return false;
        if (_pos + len > _len) return false;
        _pos += len;
        return true;
    }

    case BRPC_ARRAY: {
        uint32_t total_bytes, elem_count;
        if (!readU32(total_bytes)) return false;
        if (!readU32(elem_count)) return false;
        if (_pos + total_bytes > _len) return false;
        _pos += total_bytes;
        return true;
    }

    case BRPC_NAMED_BYTE: {
        // skip name
        uint32_t name_len;
        if (!readU32(name_len)) return false;
        if (_pos + name_len > _len) return false;
        _pos += name_len;
        // skip value
        if (_pos + 1 > _len) return false;
        _pos += 1;
        return true;
    }

    case BRPC_NAMED_SHORT: {
        uint32_t name_len;
        if (!readU32(name_len)) return false;
        if (_pos + name_len > _len) return false;
        _pos += name_len;
        if (_pos + 2 > _len) return false;
        _pos += 2;
        return true;
    }

    case BRPC_NAMED_INT: {
        uint32_t name_len;
        if (!readU32(name_len)) return false;
        if (_pos + name_len > _len) return false;
        _pos += name_len;
        if (_pos + 4 > _len) return false;
        _pos += 4;
        return true;
    }

    case BRPC_NAMED_LONG: {
        uint32_t name_len;
        if (!readU32(name_len)) return false;
        if (_pos + name_len > _len) return false;
        _pos += name_len;
        if (_pos + 8 > _len) return false;
        _pos += 8;
        return true;
    }

    case BRPC_NAMED_STRING: {
        // skip name
        uint32_t name_len;
        if (!readU32(name_len)) return false;
        if (_pos + name_len > _len) return false;
        _pos += name_len;
        // skip value string
        uint32_t val_len;
        if (!readU32(val_len)) return false;
        if (_pos + val_len > _len) return false;
        _pos += val_len;
        return true;
    }

    default:
        return false;
    }
}

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara
