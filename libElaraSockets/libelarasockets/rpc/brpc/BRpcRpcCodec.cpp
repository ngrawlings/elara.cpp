//
//  BRpcRpcCodec.cpp
//  libElaraSockets
//

#include "BRpcRpcCodec.h"
#include "BRpcCodec.h"

#include <string.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

// ── Helpers ───────────────────────────────────────────────────────────────────

static ByteArray wrap_array(const BRpcWriter& elements, uint32_t count) {
    BRpcWriter outer;
    outer.writeArray(elements, count);
    return outer.bytes();
}

static bool name_eq(const String& s, const char* cstr, size_t clen) {
    return (size_t)s.length() == clen
        && memcmp((const char*)s, cstr, clen) == 0;
}
#define NAME_EQ(s, lit) name_eq((s), (lit), sizeof(lit) - 1)

static const String NULL_STR("null");

// ── Encoders ──────────────────────────────────────────────────────────────────

ByteArray BRpcRpcCodec::buildRequest(
    const String& id, const String& method, const String& params_json
) {
    BRpcWriter f;
    f.writeNamedString(String("id"),     id);
    f.writeNamedString(String("method"), method);
    f.writeNamedString(String("params"), params_json.length() ? params_json : NULL_STR);
    return wrap_array(f, 3);
}

ByteArray BRpcRpcCodec::buildNotification(
    const String& method, const String& params_json
) {
    BRpcWriter f;
    f.writeNamedString(String("method"), method);
    f.writeNamedString(String("params"), params_json.length() ? params_json : NULL_STR);
    return wrap_array(f, 2);
}

ByteArray BRpcRpcCodec::buildSuccessResponse(
    const String& id, const String& result_json
) {
    BRpcWriter f;
    f.writeNamedString(String("id"),     id);
    f.writeNamedByte  (String("ok"),     1);
    f.writeNamedString(String("result"), result_json.length() ? result_json : NULL_STR);
    return wrap_array(f, 3);
}

ByteArray BRpcRpcCodec::buildErrorResponse(
    const String& id, const String& code, const String& message
) {
    BRpcWriter f;
    f.writeNamedString(String("id"),   id);
    f.writeNamedByte  (String("ok"),   0);
    f.writeNamedString(String("code"), code);
    f.writeNamedString(String("msg"),  message);
    return wrap_array(f, 4);
}

ByteArray BRpcRpcCodec::framePayload(const ByteArray& payload) {
    uint32_t len = (uint32_t)payload.length();
    uint8_t prefix[4] = {
        (uint8_t)(len >> 24), (uint8_t)(len >> 16),
        (uint8_t)(len >>  8), (uint8_t)(len      ),
    };
    ByteArray framed;
    framed.append(prefix, 4);
    framed.append(payload);
    return framed;
}

// ── Parser internals ──────────────────────────────────────────────────────────

struct RpcFields {
    bool    has_id     = false;
    bool    has_method = false;
    bool    has_ok     = false;
    uint8_t ok_val     = 0;
    String  id, method, params, result, code, msg;
};

static bool read_rpc_fields(
    const char* data, size_t len,
    RpcFields& f, uint32_t& count,
    String& error_message
) {
    BRpcReader r(data, len);
    uint32_t total_bytes;
    if (!r.readArrayHeader(total_bytes, count)) {
        error_message = "Expected BRPC array header";
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t type;
        if (!r.peekType(type)) {
            error_message = "Unexpected end of field list";
            return false;
        }
        if (type == BRPC_NAMED_STRING) {
            String name, value;
            if (!r.readNamedString(name, value)) {
                error_message = "Failed to read named string";
                return false;
            }
            if      (NAME_EQ(name, "id"))     { f.has_id     = true; f.id     = value; }
            else if (NAME_EQ(name, "method")) { f.has_method = true; f.method = value; }
            else if (NAME_EQ(name, "params")) { f.params  = value; }
            else if (NAME_EQ(name, "result")) { f.result  = value; }
            else if (NAME_EQ(name, "code"))   { f.code    = value; }
            else if (NAME_EQ(name, "msg"))    { f.msg     = value; }
        } else if (type == BRPC_NAMED_BYTE) {
            String name; uint8_t value;
            if (!r.readNamedByte(name, value)) {
                error_message = "Failed to read named byte";
                return false;
            }
            if (NAME_EQ(name, "ok")) { f.has_ok = true; f.ok_val = value; }
        } else {
            if (!r.skipValue()) {
                error_message = "Failed to skip unknown field";
                return false;
            }
        }
    }
    return true;
}

// ── Decoders ──────────────────────────────────────────────────────────────────

bool BRpcRpcCodec::parseRequest(
    const char* data, size_t len,
    String& id, String& method, String& params_json,
    String& error_message
) {
    RpcFields f; uint32_t count;
    if (!read_rpc_fields(data, len, f, count, error_message)) return false;
    if (!f.has_id || !f.has_method || f.has_ok) {
        error_message = "Not a request";
        return false;
    }
    id          = f.id;
    method      = f.method;
    params_json = f.params.length() ? f.params : NULL_STR;
    return true;
}

bool BRpcRpcCodec::parseNotification(
    const char* data, size_t len,
    String& method, String& params_json,
    String& error_message
) {
    RpcFields f; uint32_t count;
    if (!read_rpc_fields(data, len, f, count, error_message)) return false;
    if (!f.has_method || f.has_id) {
        error_message = "Not a notification";
        return false;
    }
    method      = f.method;
    params_json = f.params.length() ? f.params : NULL_STR;
    return true;
}

bool BRpcRpcCodec::parseResponse(
    const char* data, size_t len,
    String& id, bool& ok,
    String& result_json, String& error_code, String& error_message,
    String& parse_error_message
) {
    RpcFields f; uint32_t count;
    if (!read_rpc_fields(data, len, f, count, parse_error_message)) return false;
    if (!f.has_id || !f.has_ok) {
        parse_error_message = "Not a response";
        return false;
    }
    id = f.id;
    ok = (f.ok_val != 0);
    if (ok) {
        result_json = f.result.length() ? f.result : NULL_STR;
    } else {
        error_code    = f.code;
        error_message = f.msg;
    }
    return true;
}

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara
