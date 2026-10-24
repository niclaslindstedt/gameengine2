// The deflater's match finder and its fixed-Huffman bit writer.
//
// ge:summary LZ77 hash chains and fixed-Huffman codes, written LSB first
// ge:keywords deflate lz77 hash-chain huffman bit-writer adler32
// ge:skill labs
// ge:test tests/lab_raster_test.cpp
#include "lab/deflate.hpp"

#include <algorithm>
#include <array>

namespace ge::lab {
namespace {

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kMaxChain = 64;  // candidates tried a position: the ratio against the time

constexpr std::array<uint16_t, 29> kLengthBase = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                  15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                  67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Bits {
  std::vector<uint8_t>& out;
  uint32_t acc = 0;
  int n = 0;
  void put(uint32_t v, int bits) {  // LSB first, as deflate's extra bits and headers are
    acc |= v << n;
    n += bits;
    while (n >= 8) {
      out.push_back(static_cast<uint8_t>(acc));
      acc >>= 8;
      n -= 8;
    }
  }
  void code(uint32_t c, int bits) {  // a Huffman code goes MSB first
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i) r |= ((c >> i) & 1u) << (bits - 1 - i);
    put(r, bits);
  }
  void flush() {
    if (n > 0) out.push_back(static_cast<uint8_t>(acc));
    acc = 0;
    n = 0;
  }
};

// The fixed literal/length code of RFC 1951 § 3.2.6.
void literal(Bits& b, int v) {
  if (v < 144)
    b.code(0x30 + v, 8);
  else if (v < 256)
    b.code(0x190 + (v - 144), 9);
  else if (v < 280)
    b.code(v - 256, 7);
  else
    b.code(0xc0 + (v - 280), 8);
}

void match(Bits& b, int length, int distance) {
  int li = 28;
  while (kLengthBase[li] > length) --li;
  literal(b, 257 + li);
  b.put(static_cast<uint32_t>(length - kLengthBase[li]), kLengthExtra[li]);
  int di = 29;
  while (kDistBase[di] > distance) --di;
  b.code(static_cast<uint32_t>(di), 5);
  b.put(static_cast<uint32_t>(distance - kDistBase[di]), kDistExtra[di]);
}

uint32_t hash3(const uint8_t* p) {
  return ((uint32_t{p[0]} << 10) ^ (uint32_t{p[1]} << 5) ^ p[2]) & ((1u << kHashBits) - 1);
}

}  // namespace

uint32_t adler32(std::span<const uint8_t> bytes) {
  uint32_t a = 1, b = 0;
  for (uint8_t v : bytes) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

std::vector<uint8_t> deflate_zlib(std::span<const uint8_t> bytes) {
  std::vector<uint8_t> out{0x78, 0x01};
  Bits bits{out};
  bits.put(1, 1);  // BFINAL: one block
  bits.put(1, 2);  // BTYPE 01: fixed Huffman
  const int n = static_cast<int>(bytes.size());
  std::vector<int> head(1u << kHashBits, -1);
  std::vector<int> prev(static_cast<size_t>(n), -1);
  auto insert = [&](int i) {
    if (i + kMinMatch > n) return;
    const uint32_t h = hash3(&bytes[i]);
    prev[i] = head[h];
    head[h] = i;
  };
  int i = 0;
  while (i < n) {
    int best = 0, best_dist = 0;
    if (i + kMinMatch <= n) {
      const int limit = std::min(kMaxMatch, n - i);
      int cand = head[hash3(&bytes[i])];
      for (int tries = 0; cand >= 0 && i - cand <= kWindow && tries < kMaxChain; ++tries) {
        int l = 0;
        while (l < limit && bytes[cand + l] == bytes[i + l]) ++l;
        if (l > best) {
          best = l;
          best_dist = i - cand;
          if (l == limit) break;
        }
        cand = prev[cand];
      }
    }
    if (best >= kMinMatch) {
      match(bits, best, best_dist);
      for (int k = 0; k < best; ++k) insert(i + k);
      i += best;
    } else {
      literal(bits, bytes[i]);
      insert(i);
      ++i;
    }
  }
  literal(bits, 256);  // end of block
  bits.flush();
  const uint32_t a = adler32(bytes);
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(a >> s));
  return out;
}

}  // namespace ge::lab
