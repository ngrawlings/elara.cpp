#ifndef ELARAOSEPAFRAME_H
#define ELARAOSEPAFRAME_H

#include <stddef.h>
#include <stdint.h>
#include <libelaracore/memory/String.h>

namespace elara {

static const uint32_t ORANGE_FORTRESS_EPA_FRAME_MAGIC = 0x45465231u; /* EFR1 */
static const uint32_t ORANGE_FORTRESS_EPA_FRAME_VERSION = 1u;
static const size_t ORANGE_FORTRESS_EPA_FRAME_HEADER_BYTES = 28u;

struct ElaraOsEpaFrameHeader {
    bool valid;
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t frame_type;
    uint32_t frame_id;
    uint32_t record_count;
    uint32_t header_bytes;
    uint32_t payload_bytes;
    uint32_t total_bytes;
    String error;

    ElaraOsEpaFrameHeader()
        : valid(false),
          magic(0),
          version(0),
          width(0),
          height(0),
          frame_type(0),
          frame_id(0),
          record_count(0),
          header_bytes((uint32_t)ORANGE_FORTRESS_EPA_FRAME_HEADER_BYTES),
          payload_bytes(0),
          total_bytes(0),
          error() {
    }
};

uint32_t orangeFortressReadLeU32(const unsigned char *p);
int32_t orangeFortressReadLeI32(const unsigned char *p);
ElaraOsEpaFrameHeader orangeFortressParseEgressFrameHeader(const void *data, size_t len);
String orangeFortressFrameHeaderJson(const ElaraOsEpaFrameHeader &header, const String &direction, const String &schema);
String orangeFortressIngressFrameHeaderJson(
    const String &schema,
    const String &payload_type,
    uint32_t frame_id,
    uint32_t payload_bytes
);

}

#endif
