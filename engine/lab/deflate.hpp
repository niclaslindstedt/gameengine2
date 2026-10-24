// THE DEFLATER the tree's committed pictures are packed with: a zlib stream (RFC 1950) of
// fixed-Huffman deflate blocks (RFC 1951) over a greedy LZ77 match finder with hash chains in a
// 32 KiB window. It is not zlib's ratio, but a glyph atlas or a tiled noise layer (long runs, a
// few edges) packs to a small fraction of its bytes, and the output is the same bytes on every
// platform, so a committed file can be rebuilt and compared byte for byte. The lab's own
// screenshots stay stored (png.hpp): they are written often and never committed.
//
// ge:summary The deflater: a zlib stream of fixed-Huffman blocks over LZ77 hash chains
// ge:keywords deflate zlib compress lz77 huffman adler32 deflate_zlib pack
// ge:skill labs
// ge:test tests/lab_raster_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ge::lab {

// `bytes` as a zlib stream: inflate_zlib (inflate.hpp) gives them back.
std::vector<uint8_t> deflate_zlib(std::span<const uint8_t> bytes);

uint32_t adler32(std::span<const uint8_t> bytes);

}  // namespace ge::lab
