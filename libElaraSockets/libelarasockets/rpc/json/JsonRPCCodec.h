#ifndef ElaraSockets_JsonRPCCodec_h
#define ElaraSockets_JsonRPCCodec_h

#include <libelaracore/memory/ByteArray.h>
#include <libelaracore/memory/String.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    class JsonRPCCodec {
    public:
        static String escapeJsonString(const String &value);

        static String buildRequest(const String &id, const String &method, String params_json);
        static String buildSuccessResponse(const String &id, String result_json);
        static String buildErrorResponse(const String &id, const String &code, const String &message);

        static bool parseRequest(const String &json, String &id, String &method, String &params_json, String &error_message);
        static bool parseResponse(const String &json, String &id, bool &ok, String &result_json, String &error_code, String &error_message, String &parse_error_message);
        static bool getStringField(const String &json, const String &field, String &value);

        static ByteArray framePayload(const String &payload);
        static bool extractFramedPayload(ByteArray &buffer, String &payload);

    private:
        static bool extractTopLevelField(String json, String field, String *value_json);
        static bool parseJsonString(String json, String *value);
    };

}
}
}
}

#endif
