// EBML's elements (ebml.hpp).
//
// ge:summary ebml's definitions: VINTs, big-endian numbers, masters and their sizes
// ge:keywords ebml vint big-endian master size
// ge:skill replay
// ge:test tests/capture_ebml_test.cpp
#include "capture/ebml.hpp"

#include <bit>
#include <cstring>

namespace ge::capture {
namespace {

int bytes_of(uint64_t v) {
  int n = 1;
  while (n < 8 && (v >> (8 * n)) != 0) n++;
  return n;
}

void put_be(std::vector<uint8_t>& out, uint64_t v, int width) {
  for (int i = width - 1; i >= 0; i--) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

}  // namespace

void put_vint(std::vector<uint8_t>& out, uint64_t v, int width) {
  if (width <= 0) {
    // A VINT of n bytes holds 7n bits, and all ones is reserved for "unknown".
    width = 1;
    while (width < 8 && v >= (uint64_t{1} << (7 * width)) - 1) width++;
  }
  put_be(out, v | (uint64_t{1} << (7 * width)), width);
}

void patch_be(std::vector<uint8_t>& out, size_t at, uint64_t v, int width) {
  for (int i = 0; i < width; i++) {
    out[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * (width - 1 - i)));
  }
}

void Ebml::id(uint32_t id) { put_be(bytes, id, bytes_of(id)); }

void Ebml::uint(uint32_t id, uint64_t v) {
  this->id(id);
  const int n = bytes_of(v);
  put_vint(bytes, static_cast<uint64_t>(n));
  put_be(bytes, v, n);
}

void Ebml::real(uint32_t id, double v) {
  this->id(id);
  put_vint(bytes, 8);
  put_be(bytes, std::bit_cast<uint64_t>(v), 8);
}

void Ebml::text(uint32_t id, std::string_view s) {
  this->id(id);
  put_vint(bytes, s.size());
  bytes.insert(bytes.end(), s.begin(), s.end());
}

void Ebml::bin(uint32_t id, std::span<const uint8_t> b) {
  this->id(id);
  put_vint(bytes, b.size());
  bytes.insert(bytes.end(), b.begin(), b.end());
}

size_t Ebml::open(uint32_t id) {
  this->id(id);
  const size_t at = bytes.size();
  bytes.resize(at + 8, 0);
  return at;
}

void Ebml::close(size_t at) { patch_size(at, body_since(at)); }

size_t Ebml::open_unknown(uint32_t id) {
  this->id(id);
  const size_t at = bytes.size();
  put_vint(bytes, kUnknownSize, 8);
  return at;
}

void Ebml::patch_size(size_t at, uint64_t size) {
  patch_be(bytes, at, size | (uint64_t{1} << 56), 8);
}

}  // namespace ge::capture
