#include "strap_file.h"

#include <cstring>

namespace strap {

const uint8_t kFileHeader[kFileHeaderSize] = {'S', 'T', 'R', 'A', 'P', 0x00, kFormatVersion, 0x00};

size_t encodeRecord(RecordType type, uint32_t uptimeMs, const uint8_t* payload, size_t length,
                    uint8_t* out) {
    if (length > kMaxPayload) length = kMaxPayload;
    out[0] = static_cast<uint8_t>(type);
    for (int i = 0; i < 4; ++i) out[1 + i] = static_cast<uint8_t>(uptimeMs >> (8 * i));
    out[5] = static_cast<uint8_t>(length);
    out[6] = static_cast<uint8_t>(length >> 8);
    if (length > 0) std::memcpy(out + kRecordHeaderSize, payload, length);
    return kRecordHeaderSize + length;
}

}  // namespace strap
