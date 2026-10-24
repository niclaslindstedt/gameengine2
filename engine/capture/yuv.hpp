// A picture as I420 (planar YUV: the luma plane at full size, then the two chroma planes
// at half size each way), the layout every video encoder takes and the export's uncompressed
// track carries. The colours are BT.709 at limited range (luma 16..235, chroma 16..240), the
// matrix and range HD video assumes when a file names none; the arithmetic is integer, so a
// picture converts to the same bytes everywhere. Chroma is the mean of each 2x2 block.
//
// ge:summary RGBA to I420 and back: BT.709 limited range in integers, chroma per 2x2 block
// ge:keywords yuv i420 yuv420p bt709 limited-range luma chroma convert rgba planar
// ge:keywords from_i420 to_i420 decode
// ge:skill replay
// ge:test tests/capture_yuv_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "lab/raster.hpp"

namespace ge::capture {

// The bytes `p` takes as I420: width * height * 3 / 2 (both sides even).
inline size_t i420_bytes(int width, int height) {
  return static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
}

// `p` (even sides) as I420 into `out` (resized to i420_bytes).
void to_i420(const lab::Picture& p, std::vector<uint8_t>& out);
// The other way, for a lab that shows what a file holds: `i420` (width * height * 3 / 2 bytes)
// as an opaque picture, each chroma sample spread over its 2x2 block.
lab::Picture from_i420(const uint8_t* i420, int width, int height);

}  // namespace ge::capture
