// engine/save/xxh64.hpp: the checksum is XXH64, held to the algorithm's published values for
// every path through it (under 4, 8 and 32 bytes, the stripes, a seed).
//
// ge:summary Test: XXH64 against the algorithm's published values
// ge:keywords test save xxh64 checksum hash vectors
// ge:skill save testing
#include "save/xxh64.hpp"

#include <cstring>
#include <vector>

#include "check.hpp"

using ge::save::xxh64;

TEST(xxh64_matches_the_published_values) {
  CHECK(xxh64("", 0) == 0xef46db3751d8e999ull);
  CHECK(xxh64("a", 1) == 0xd24ec4f1a98c6e5bull);
  CHECK(xxh64("abc", 3) == 0x44bc2cf5ad770999ull);
  const char* s = "Nobody inspects the spammish repetition";
  CHECK(xxh64(s, std::strlen(s)) == 0xfbcea83c8a378bf1ull);
  CHECK(xxh64("abc", 3, 1) == 0xbea9ca8199328908ull);
  std::vector<unsigned char> ramp(1024);
  for (size_t i = 0; i < ramp.size(); i++) ramp[i] = static_cast<unsigned char>(i);
  CHECK(xxh64(ramp.data(), 1024) == 0x6f3914f18fe4df57ull);
  CHECK(xxh64(ramp.data(), 100, 12345) == 0x28ba1ae2de4de27ull);
}

TEST(xxh64_reads_unaligned_bytes) {
  std::vector<unsigned char> a(200), b(201);
  for (size_t i = 0; i < a.size(); i++) a[i] = b[i + 1] = static_cast<unsigned char>(i * 7);
  CHECK(xxh64(a.data(), 200) == xxh64(b.data() + 1, 200));
}
