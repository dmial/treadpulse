// The heart rate strap's own data for one walk (/walks/<walk>/strap.bin):
// everything the strap sends, kept as it arrived, so nothing is lost to a
// decoder that might be wrong today. tools/strap_decode.py turns it into CSV.
//
// The file is an 8-byte header, then records one after another:
//
//   header   "STRAP" 0x00 <format version> 0x00
//   record   type      uint8
//            uptimeMs  uint32 little-endian: the board's millis() on arrival
//            length    uint16 little-endian
//            payload   `length` bytes
//
// Records are in arrival order; the board's notes (walk start and end) may sit
// a second away from the strap records around them. A power cut can leave the
// last record cut short. Pure C++, host-testable.
#pragma once

#include <cstddef>
#include <cstdint>

namespace strap {

enum class RecordType : uint8_t {
    // Heart Rate Measurement notification (0x2A37), as received: pulse,
    // skin contact and the beat-to-beat (RR) intervals.
    HeartRate = 0x01,
    // Polar Measurement Data notification (pmd.h), as received: ECG or
    // accelerometer samples with the strap's own timestamp.
    PmdData = 0x02,
    // Polar Measurement Data control point, as received: the reply to a start
    // command, or the list of measurements the strap offers.
    PmdControl = 0x03,
    // The board's own note, ASCII text: walk start and end, connections.
    Note = 0x10,
    // uint32 little-endian: records lost because the board fell behind.
    Lost = 0x11,
};

constexpr uint8_t kFormatVersion = 1;
constexpr size_t kFileHeaderSize = 8;
extern const uint8_t kFileHeader[kFileHeaderSize];

constexpr size_t kRecordHeaderSize = 7;
// The longest notification with the board's 255-byte MTU; a Polar H10 ECG
// frame is 229 bytes.
constexpr size_t kMaxPayload = 252;
constexpr size_t kMaxRecordSize = kRecordHeaderSize + kMaxPayload;

// Writes one record into `out` (room for kRecordHeaderSize + length bytes) and
// returns its size. A payload over kMaxPayload is cut to kMaxPayload.
size_t encodeRecord(RecordType type, uint32_t uptimeMs, const uint8_t* payload, size_t length,
                    uint8_t* out);

}  // namespace strap
