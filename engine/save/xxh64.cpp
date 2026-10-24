// XXH64's rounds: four lanes of 8-byte words over 32-byte stripes, then the tail in 8, 4 and
// 1-byte steps and the final avalanche. Reads are little-endian loads through memcpy, so the
// input needs no alignment.
//
// ge:summary XXH64's definition: the stripes, the tail and the avalanche
// ge:keywords save checksum hash xxh64 stripe lane avalanche
// ge:skill save
// ge:test tests/save_xxh64_test.cpp
#include "save/xxh64.hpp"

#include <bit>
#include <cstring>

namespace ge::save {
namespace {

static_assert(std::endian::native == std::endian::little, "saves are little-endian loads");

constexpr uint64_t kP1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kP2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kP3 = 0x165667B19E3779F9ull;
constexpr uint64_t kP4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kP5 = 0x27D4EB2F165667C5ull;

inline uint64_t load64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
inline uint32_t load32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
inline uint64_t round(uint64_t acc, uint64_t in) {
  acc += in * kP2;
  acc = std::rotl(acc, 31);
  return acc * kP1;
}
inline uint64_t merge(uint64_t acc, uint64_t v) {
  acc ^= round(0, v);
  return acc * kP1 + kP4;
}

}  // namespace

uint64_t xxh64(const void* data, size_t n, uint64_t seed) {
  const auto* p = static_cast<const uint8_t*>(data);
  const uint8_t* const end = p + n;
  uint64_t h;
  if (n >= 32) {
    uint64_t v1 = seed + kP1 + kP2, v2 = seed + kP2, v3 = seed, v4 = seed - kP1;
    const uint8_t* const limit = end - 32;
    do {
      v1 = round(v1, load64(p));
      v2 = round(v2, load64(p + 8));
      v3 = round(v3, load64(p + 16));
      v4 = round(v4, load64(p + 24));
      p += 32;
    } while (p <= limit);
    h = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
    h = merge(h, v1);
    h = merge(h, v2);
    h = merge(h, v3);
    h = merge(h, v4);
  } else {
    h = seed + kP5;
  }
  h += static_cast<uint64_t>(n);
  while (p + 8 <= end) {
    h ^= round(0, load64(p));
    h = std::rotl(h, 27) * kP1 + kP4;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= static_cast<uint64_t>(load32(p)) * kP1;
    h = std::rotl(h, 23) * kP2 + kP3;
    p += 4;
  }
  while (p < end) {
    h ^= (*p) * kP5;
    h = std::rotl(h, 11) * kP1;
    p++;
  }
  h ^= h >> 33;
  h *= kP2;
  h ^= h >> 29;
  h *= kP3;
  h ^= h >> 32;
  return h;
}

}  // namespace ge::save
