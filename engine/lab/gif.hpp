// A LAB PICTURE MADE SMALL ENOUGH TO SHARE: shrunk to a width, cut to a palette of at most 256
// colours and written as a GIF (LZW-compressed, which every browser opens). The labs' own PNGs
// are stored uncompressed, a few megabytes each; a review page carrying a dozen of them inline
// would not fit. A lab picture is flat colour, lines and text, so its GIF is a tenth of the PNG
// or less, and every colour a picture uses up to 256 is kept exactly.
//
// THE PALETTE: a picture of 256 colours or fewer keeps them all. Past that, the colours are
// counted at five bits a channel and the buckets cut into 256 boxes (a median cut: the box with
// the most pixels across the widest span is halved by pixel count along that span), each box's
// colour the mean of its pixels. Alpha is laid over white.
//
// ge:summary A lab picture shrunk, cut to 256 colours and written as a GIF for a review page
// ge:keywords gif lzw palette quantize quantise shrink downscale box-filter encode web picture
// ge:keywords publish review page small
// ge:skill labs
// ge:test tests/lab_gif_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "lab/raster.hpp"

namespace ge::lab {

// `picture` shrunk to `width` pixels across (the height in proportion), each pixel the mean of
// the ones it covers; a picture already no wider comes back as it is.
Picture shrink(const Picture& picture, int width);

// `picture` as a GIF89a file: one frame, a global palette of at most 256 colours.
std::vector<uint8_t> encode_gif(const Picture& picture);

}  // namespace ge::lab
