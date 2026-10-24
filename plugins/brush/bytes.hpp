// Little-endian fields out of a byte span and into a byte vector, for the BSP readers and the
// fixture writers. A read past the end returns 0 and marks the cursor bad; the readers check
// lumps' bounds first, so a bad cursor is a reader's bug, refused the same way.
//
// ge:summary Little-endian byte cursor and writer for the BSP readers and fixture writers
// ge:keywords bytes little-endian cursor reader writer lump i32 u16 f32 bounds
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace ge::brush::detail {

struct Cursor {
  std::span<const uint8_t> b;
  size_t at = 0;
  bool bad = false;
  bool need(size_t n) {
    if (bad || n > b.size() || at > b.size() - n) {
      bad = true;
      return false;
    }
    return true;
  }
  uint8_t u8() { return need(1) ? b[at++] : uint8_t(0); }
  uint16_t u16() {
    if (!need(2)) return 0;
    const auto v = uint16_t(b[at] | uint16_t(b[at + 1]) << 8);
    at += 2;
    return v;
  }
  uint32_t u32() {
    if (!need(4)) return 0;
    const uint32_t v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 |
                       uint32_t(b[at + 3]) << 24;
    at += 4;
    return v;
  }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  float f32() {
    const uint32_t u = u32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  // A fixed-size name field: up to its first NUL.
  std::string name(size_t n) {
    if (!need(n)) return {};
    size_t len = 0;
    while (len < n && b[at + len] != 0) ++len;
    std::string s(reinterpret_cast<const char*>(b.data() + at), len);
    at += n;
    return s;
  }
  void skip(size_t n) {
    if (need(n)) at += n;
  }
};

struct Out {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) {
    u8(uint8_t(v));
    u8(uint8_t(v >> 8));
  }
  void u32(uint32_t v) {
    u16(uint16_t(v));
    u16(uint16_t(v >> 16));
  }
  void i16(int32_t v) { u16(static_cast<uint16_t>(static_cast<int16_t>(v))); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void f32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    u32(u);
  }
  void name(const std::string& s, size_t n) {
    for (size_t i = 0; i < n; ++i) u8(i < s.size() ? uint8_t(s[i]) : uint8_t(0));
  }
  void bytes(const std::vector<uint8_t>& v) { b.insert(b.end(), v.begin(), v.end()); }
  void align4() {
    while (b.size() % 4) b.push_back(0);
  }
  void put_i32(size_t at, int32_t v) {
    const auto u = static_cast<uint32_t>(v);
    for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(u >> (8 * k));
  }
};

}  // namespace ge::brush::detail
