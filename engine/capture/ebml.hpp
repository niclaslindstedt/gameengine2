// EBML, the binary layout Matroska and WebM files are made of: every element is its ID, its size
// as a variable-length integer (a VINT: the count of leading zero bits in the first byte says how
// many more bytes follow) and its body; a MASTER element's body is more elements. This writer is
// the one the engine's muxer (capture/matroska.hpp) builds its files with.
//
// An ID is written as the bytes of its value (Matroska's IDs carry their own length marker:
// 0x1A45DFA3 is four bytes, 0xA3 one). A master is opened with `open`, which reserves an 8-byte
// size, and closed with `close`, which writes the size of what went in between; `open_unknown`
// writes the reserved "unknown size" instead (a segment or a cluster still being written, which
// a reader takes to run to the next element of its level or the end of the file), to be patched
// later or left as it is.
//
// ge:summary EBML: elements, VINT sizes, masters with sizes patched on close
// ge:keywords ebml vint element master matroska webm id size unknown-size patch
// ge:skill replay
// ge:test tests/capture_ebml_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ge::capture {

// The size field a reader takes as "unknown": an 8-byte VINT of all ones.
inline constexpr uint64_t kUnknownSize = 0x00FF'FFFF'FFFF'FFFFull;

// `v` as the shortest VINT that holds it (v < 2^56 - 1), or as `width` bytes when width > 0.
void put_vint(std::vector<uint8_t>& out, uint64_t v, int width = 0);
// `v` as `width` big-endian bytes written over out[at .. at + width).
void patch_be(std::vector<uint8_t>& out, size_t at, uint64_t v, int width);

class Ebml {
 public:
  void id(uint32_t id);
  void uint(uint32_t id, uint64_t v);  // the fewest bytes, at least one
  void real(uint32_t id, double v);    // 8 bytes, IEEE 754 big-endian
  void text(uint32_t id, std::string_view s);
  void bin(uint32_t id, std::span<const uint8_t> b);
  // A master's ID and an 8-byte size to be written by `close(at)`; returns where its size is.
  size_t open(uint32_t id);
  void close(size_t at);
  // A master whose size stays unknown until patched with `patch_size(at, size)`.
  size_t open_unknown(uint32_t id);
  void patch_size(size_t at, uint64_t size);
  // The body of the master opened at `at`: how many bytes follow its size field.
  size_t body_since(size_t at) const { return bytes.size() - at - 8; }

  std::vector<uint8_t> bytes;
};

}  // namespace ge::capture
