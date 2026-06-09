#include "ElaraOsEpaFrame.h"

#include <libelarasockets/rpc/json/JsonRPCCodec.h>

namespace elara {

using sockets::rpc::json::JsonRPCCodec;

namespace {
String jsonQuoteFrame(const String &value) {
    return String("\"") + JsonRPCCodec::escapeJsonString(value) + String("\"");
}
}

uint32_t orangeFortressReadLeU32(const unsigned char *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

int32_t orangeFortressReadLeI32(const unsigned char *p) {
    return (int32_t)orangeFortressReadLeU32(p);
}

ElaraOsEpaFrameHeader orangeFortressParseEgressFrameHeader(const void *data, size_t len) {
    ElaraOsEpaFrameHeader header;
    const unsigned char *bytes = (const unsigned char *)data;

    header.total_bytes = (uint32_t)len;
    if(!bytes || len < ORANGE_FORTRESS_EPA_FRAME_HEADER_BYTES) {
        header.error = String("truncated_header");
        return header;
    }

    header.magic = orangeFortressReadLeU32(bytes + 0);
    header.version = orangeFortressReadLeU32(bytes + 4);
    header.width = orangeFortressReadLeU32(bytes + 8);
    header.height = orangeFortressReadLeU32(bytes + 12);
    header.frame_type = orangeFortressReadLeU32(bytes + 16);
    header.frame_id = orangeFortressReadLeU32(bytes + 20);
    header.record_count = orangeFortressReadLeU32(bytes + 24);
    header.payload_bytes = (uint32_t)(len - ORANGE_FORTRESS_EPA_FRAME_HEADER_BYTES);

    if(header.magic != ORANGE_FORTRESS_EPA_FRAME_MAGIC) {
        header.error = String("bad_magic");
        return header;
    }
    if(header.version != ORANGE_FORTRESS_EPA_FRAME_VERSION) {
        header.error = String("unsupported_version");
        return header;
    }

    header.valid = true;
    return header;
}

String orangeFortressFrameHeaderJson(const ElaraOsEpaFrameHeader &header, const String &direction, const String &schema) {
    String error_text = header.error.length() ? header.error : String("ok");
    return String("{")
        + String("\"schema\":") + jsonQuoteFrame(schema)
        + String(",\"direction\":") + jsonQuoteFrame(direction)
        + String(",\"valid\":") + String(header.valid ? "true" : "false")
        + String(",\"magic\":") + String((unsigned long long)header.magic)
        + String(",\"version\":") + String((int)header.version)
        + String(",\"header_bytes\":") + String((int)header.header_bytes)
        + String(",\"payload_bytes\":") + String((int)header.payload_bytes)
        + String(",\"total_bytes\":") + String((int)header.total_bytes)
        + String(",\"width\":") + String((int)header.width)
        + String(",\"height\":") + String((int)header.height)
        + String(",\"frame_type\":") + String((int)header.frame_type)
        + String(",\"frame_id\":") + String((int)header.frame_id)
        + String(",\"record_count\":") + String((int)header.record_count)
        + String(",\"error\":") + jsonQuoteFrame(error_text)
        + String("}");
}

String orangeFortressIngressFrameHeaderJson(
    const String &schema,
    const String &payload_type,
    uint32_t frame_id,
    uint32_t payload_bytes
) {
    return String("{")
        + String("\"schema\":") + jsonQuoteFrame(schema)
        + String(",\"direction\":\"ingress\"")
        + String(",\"valid\":true")
        + String(",\"magic\":") + String((unsigned long long)ORANGE_FORTRESS_EPA_FRAME_MAGIC)
        + String(",\"version\":") + String((int)ORANGE_FORTRESS_EPA_FRAME_VERSION)
        + String(",\"header_bytes\":0")
        + String(",\"payload_bytes\":") + String((int)payload_bytes)
        + String(",\"total_bytes\":") + String((int)payload_bytes)
        + String(",\"width\":0")
        + String(",\"height\":0")
        + String(",\"frame_type\":1")
        + String(",\"frame_id\":") + String((int)frame_id)
        + String(",\"record_count\":1")
        + String(",\"payload_type\":") + jsonQuoteFrame(payload_type)
        + String(",\"error\":\"ok\"")
        + String("}");
}

}
