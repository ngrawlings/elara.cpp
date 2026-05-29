//
//  BRpcRpcCodec.h
//  libElaraSockets
//
// Encode/decode JSON-RPC-style envelopes using the BRpc binary format.
// The inner params/result values remain JSON strings — only the outer
// envelope (id, method, ok, …) is binary-encoded.
//
// Wire format per message type (each is a BRpc array of named fields):
//
//  Request  (3 fields):  namedString "id",     namedString "method",
//                        namedString "params"  (JSON string, may be "null")
//
//  Notification (2 fields): namedString "method",
//                            namedString "params"
//
//  Response OK (3 fields):   namedString "id", namedByte "ok"=1,
//                             namedString "result"  (JSON string, may be "null")
//
//  Response Error (4 fields): namedString "id", namedByte "ok"=0,
//                              namedString "code", namedString "msg"
//

#ifndef ElaraSockets_BRpcRpcCodec_h
#define ElaraSockets_BRpcRpcCodec_h

#include <stdint.h>
#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/String.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace brpc {

class BRpcRpcCodec {
public:
    // ── Encoders ──────────────────────────────────────────────────────────────

    static ByteArray buildRequest(
        const String& id,
        const String& method,
        const String& params_json
    );

    static ByteArray buildNotification(
        const String& method,
        const String& params_json
    );

    static ByteArray buildSuccessResponse(
        const String& id,
        const String& result_json
    );

    static ByteArray buildErrorResponse(
        const String& id,
        const String& code,
        const String& message
    );

    // Wraps a payload in a 4-byte big-endian length prefix (identical framing to JSON RPC).
    static ByteArray framePayload(const ByteArray& payload);

    // ── Decoders ──────────────────────────────────────────────────────────────

    static bool parseRequest(
        const char* data, size_t len,
        String& id, String& method, String& params_json,
        String& error_message
    );

    // Fails if an "id" field is present (use parseRequest for those).
    static bool parseNotification(
        const char* data, size_t len,
        String& method, String& params_json,
        String& error_message
    );

    static bool parseResponse(
        const char* data, size_t len,
        String& id,
        bool& ok,
        String& result_json,
        String& error_code,
        String& error_message,
        String& parse_error_message
    );

private:
    BRpcRpcCodec();
};

}  // namespace brpc
}  // namespace rpc
}  // namespace sockets
}  // namespace elara

#endif  // ElaraSockets_BRpcRpcCodec_h
