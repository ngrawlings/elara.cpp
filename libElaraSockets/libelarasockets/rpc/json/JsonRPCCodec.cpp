#include "JsonRPCCodec.h"

#include <stdint.h>

#include <libelaraformat/json/Json.h>
#include <libelaraformat/json/types/JsonString.h>

namespace elara {
namespace sockets {
namespace rpc {
namespace json {

    String JsonRPCCodec::escapeJsonString(const String &value) {
        String escaped;
        String value_copy(value);

        for (int i=0; i<value_copy.length(); i++) {
            char ch = value_copy.operator char *()[i];
            switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += String(ch);
                break;
            }
        }

        return escaped;
    }

    String JsonRPCCodec::buildRequest(const String &id, const String &method, String params_json) {
        if (!params_json.length())
            params_json = "null";

        return String("{\"id\":\"") + escapeJsonString(id) + String("\",\"method\":\"") + escapeJsonString(method) + String("\",\"params\":") + params_json + String("}");
    }

    String JsonRPCCodec::buildNotification(const String &method, String params_json) {
        if (!params_json.length())
            params_json = "null";

        return String("{\"method\":\"") + escapeJsonString(method) + String("\",\"params\":") + params_json + String("}");
    }

    String JsonRPCCodec::buildSuccessResponse(const String &id, String result_json) {
        if (!result_json.length())
            result_json = "null";

        return String("{\"id\":\"") + escapeJsonString(id) + String("\",\"ok\":true,\"result\":") + result_json + String("}");
    }

    String JsonRPCCodec::buildErrorResponse(const String &id, const String &code, const String &message) {
        return String("{\"id\":\"") + escapeJsonString(id) + String("\",\"ok\":false,\"error\":{\"code\":\"") + escapeJsonString(code) + String("\",\"message\":\"") + escapeJsonString(message) + String("\"}}");
    }

    bool JsonRPCCodec::parseRequest(const String &json, String &id, String &method, String &params_json, String &error_message) {
        Json root(json);
        Ref<JsonValue> id_value = root.getJsonValue("id");
        Ref<JsonValue> method_value = root.getJsonValue("method");
        Ref<JsonValue> params_value = root.getJsonValue("params");

        if (!id_value.getPtr() || id_value->getType() != JsonValue::STRING) {
            error_message = "Missing or invalid request id";
            return false;
        }
        if (!method_value.getPtr() || method_value->getType() != JsonValue::STRING) {
            error_message = "Missing or invalid request method";
            return false;
        }

        id = ((JsonString*)id_value.getPtr())->getValue();
        method = ((JsonString*)method_value.getPtr())->getValue();
        if (params_value.getPtr())
            params_json = params_value->toString();
        else
            params_json = "null";

        return true;
    }

    bool JsonRPCCodec::parseNotification(const String &json, String &method, String &params_json, String &error_message) {
        Json root(json);
        Ref<JsonValue> id_value = root.getJsonValue("id");
        Ref<JsonValue> method_value = root.getJsonValue("method");
        Ref<JsonValue> params_value = root.getJsonValue("params");

        if (id_value.getPtr() && id_value->getType() == JsonValue::STRING) {
            error_message = "Has request id — not a notification";
            return false;
        }
        if (!method_value.getPtr() || method_value->getType() != JsonValue::STRING) {
            error_message = "Missing or invalid notification method";
            return false;
        }

        method = ((JsonString*)method_value.getPtr())->getValue();
        if (params_value.getPtr())
            params_json = params_value->toString();
        else
            params_json = "null";

        return true;
    }

    bool JsonRPCCodec::parseResponse(const String &json, String &id, bool &ok, String &result_json, String &error_code, String &error_message, String &parse_error_message) {
        Json root(json);
        Ref<JsonValue> id_value = root.getJsonValue("id");
        Ref<JsonValue> ok_value = root.getJsonValue("ok");

        if (!id_value.getPtr() || id_value->getType() != JsonValue::STRING) {
            parse_error_message = "Missing or invalid response id";
            return false;
        }
        if (!ok_value.getPtr()) {
            parse_error_message = "Missing response ok field";
            return false;
        }

        id = ((JsonString*)id_value.getPtr())->getValue();

        String ok_json = ok_value->toString().trim();
        if (ok_json == String("true")) {
            ok = true;
            Ref<JsonValue> result_value = root.getJsonValue("result");
            if (result_value.getPtr())
                result_json = result_value->toString();
            else
                result_json = "null";
            return true;
        }

        if (!(ok_json == String("false"))) {
            parse_error_message = "Invalid response ok field";
            return false;
        }

        ok = false;
        Ref<JsonValue> error_value = root.getJsonValue("error");
        if (!error_value.getPtr() || error_value->getType() != JsonValue::OBJECT) {
            parse_error_message = "Missing response error payload";
            return false;
        }

        Json error_json(error_value);
        Ref<JsonValue> code_value = error_json.getJsonValue("code");
        Ref<JsonValue> message_value = error_json.getJsonValue("message");
        if (!code_value.getPtr() || code_value->getType() != JsonValue::STRING) {
            parse_error_message = "Invalid response error code";
            return false;
        }
        if (!message_value.getPtr() || message_value->getType() != JsonValue::STRING) {
            parse_error_message = "Invalid response error message";
            return false;
        }

        error_code = ((JsonString*)code_value.getPtr())->getValue();
        error_message = ((JsonString*)message_value.getPtr())->getValue();

        return true;
    }

    bool JsonRPCCodec::getStringField(const String &json, const String &field, String &value) {
        Json root(json);
        Ref<JsonValue> field_value = root.getJsonValue(field);
        if (!field_value.getPtr() || field_value->getType() != JsonValue::STRING)
            return false;
        value = ((JsonString*)field_value.getPtr())->getValue();
        return true;
    }

    ByteArray JsonRPCCodec::framePayload(const String &payload) {
        ByteArray frame;
        String payload_copy(payload);
        uint32_t length = (uint32_t)payload_copy.length();
        char prefix[4];
        prefix[0] = (char)((length >> 24) & 0xFF);
        prefix[1] = (char)((length >> 16) & 0xFF);
        prefix[2] = (char)((length >> 8) & 0xFF);
        prefix[3] = (char)(length & 0xFF);
        frame.append(prefix, 4);
        frame.append((char*)payload_copy, payload_copy.length());
        return frame;
    }

    bool JsonRPCCodec::extractFramedPayload(ByteArray &buffer, String &payload) {
        if (buffer.length() < 4)
            return false;

        const char *bytes = (const char*)buffer;
        uint32_t length = ((uint32_t)(bytes[0] & 0xFF) << 24)
            | ((uint32_t)(bytes[1] & 0xFF) << 16)
            | ((uint32_t)(bytes[2] & 0xFF) << 8)
            | (uint32_t)(bytes[3] & 0xFF);

        if (buffer.length() < (int)(4 + length))
            return false;

        ByteArray message = buffer.subBytes(4, (int)length);
        payload = String((char*)message, message.length());
        buffer = buffer.subBytes((int)(4 + length));
        return true;
    }

}
}
}
}
