// REPLICATION'S MESSAGES, stated once for the server (replicate_server.cpp) and the client
// (replicate_client.cpp), and not installed: a game replicates through replicate.hpp.
//
//   snapshot  kind 1, tick u32, baseline u32 (0: whole), applied input u32, own entity u16,
//             slack i8 (the fewest inputs the client's queue held over the last window when
//             one was applied; -1: a tick went without), entries varint, then the entries as BITS
//             (least significant first):
//               id 16, op 2:  update  a field mask, then each changed field in its bits
//                             new     every field in its bits
//                             gone    nothing more
//             a row of the baseline no entry names is kept as it was
//   snapshot with inputs  kind 3: a snapshot (as kind 1), then a TRAILER: for each other player
//             near the client's entity (THE PREDICTED WORLD), id u16 and the last input the server
//             applied for it (input_size), then the trailer's byte count u16, last
//   input     kind 2, acked snapshot u32 (0: none), view tick x256 u32, rate u32 (bytes a
//             second, 0: no cap), update rate u16 (snapshots a second, 0: the server's), newest
//             input u32, count u8, then the last `count` inputs, oldest first, input_size each
//   event     kind 4 (reliable, server to the client that caused it), event kind u8, the input
//             that caused it u32, payload size u8, then the payload (at most kMaxEventPayload)
//
// Both little endian (wire.hpp) up to the bits.
//
// ge:summary Replication's messages: the snapshot's delta entries as bits, the redundant inputs
// ge:keywords net replicate wire snapshot delta bits bitstream bit-writer input ack encode decode
// ge:skill net
// ge:test tests/net_replicate_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace ge::net::replicate_wire {

constexpr uint8_t kSnapshot = 1, kInput = 2, kSnapshotInputs = 3, kEvent = 4;
constexpr uint8_t kUpdate = 0, kNew = 1, kGone = 2;
constexpr size_t kSnapshotHeader = 1 + 4 + 4 + 4 + 2 + 1;
constexpr size_t kInputHeader = 1 + 4 + 4 + 4 + 2 + 4 + 1;
constexpr size_t kSnapshotFloor = 64;  // the least a rate leaves a snapshot

// Bits written least significant first into a caller's buffer; a write past its end sets
// `overflow` and writes nothing more.
struct BitWriter {
  uint8_t* data;
  size_t capacity;  // bytes
  size_t bits = 0;
  bool overflow = false;
};

inline void put_bits(BitWriter& w, uint32_t v, int n) {
  if (w.overflow || w.bits + static_cast<size_t>(n) > w.capacity * 8) {
    w.overflow = true;
    return;
  }
  for (int i = 0; i < n; i++) {
    const size_t at = w.bits + static_cast<size_t>(i);
    const auto bit = static_cast<uint8_t>(1u << (at & 7));
    if ((v >> i) & 1u) {
      w.data[at >> 3] |= bit;
    } else {
      w.data[at >> 3] &= static_cast<uint8_t>(~bit);
    }
  }
  w.bits += static_cast<size_t>(n);
}

inline size_t bytes_of(const BitWriter& w) { return (w.bits + 7) / 8; }

struct BitReader {
  const uint8_t* data;
  size_t size;  // bytes
  size_t bits = 0;
  bool bad = false;
};

inline uint32_t get_bits(BitReader& r, int n) {
  if (r.bad || r.bits + static_cast<size_t>(n) > r.size * 8) {
    r.bad = true;
    return 0;
  }
  uint32_t v = 0;
  for (int i = 0; i < n; i++) {
    const size_t at = r.bits + static_cast<size_t>(i);
    v |= static_cast<uint32_t>((r.data[at >> 3] >> (at & 7)) & 1u) << i;
  }
  r.bits += static_cast<size_t>(n);
  return v;
}

}  // namespace ge::net::replicate_wire
