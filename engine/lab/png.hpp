// THE PNG WRITER the labs save pictures with: RGBA8, deflate in STORED blocks (no compression,
// no dependency), the CRC-32 and the Adler-32 the format needs. A lab picture is a few
// megabytes at most, and a compression dependency is not worth the saving. Any viewer, and an
// agent's image reader, opens it.
//
// THE READER brings a lab's own pictures back for a diff (diff.hpp): 8-bit RGBA or RGB, grey or
// grey with alpha, not interlaced, any of the five row filters, through the labs' inflater, so
// it also reads a PNG another tool compressed.
//
// ge:summary The labs' PNG writer and reader: RGBA8, stored deflate, CRC-32 and Adler-32
// ge:keywords png encode decode read image file crc32 adler32 deflate screenshot grey
// ge:keywords encode_png_grey write_file
// ge:skill labs
// ge:test tests/lab_raster_test.cpp
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "lab/raster.hpp"

namespace ge::lab {

uint32_t crc32(const uint8_t* bytes, size_t n, uint32_t crc = 0);

std::vector<uint8_t> encode_png(const Picture& picture);

// Write `picture` to `path`; false (and nothing half-written left behind) if it could not.
bool write_png(const std::string& path, const Picture& picture);

// A COMMITTED picture's PNG: 8-bit grey, each row under the filter that packs it best, deflated
// (deflate.hpp), the same bytes on every platform. For files the tree keeps (an atlas, a
// texture layer), where the stored writer above would be megabytes.
std::vector<uint8_t> encode_png_grey(int width, int height, std::span<const uint8_t> grey);
// Write `bytes` to `path` as they are; false if it could not.
bool write_file(const std::string& path, std::span<const uint8_t> bytes);

// The picture a PNG holds, as RGBA8; nothing when the bytes are not a PNG the reader takes
// (16-bit, palette or interlaced images) or a checksum fails.
std::optional<Picture> decode_png(std::span<const uint8_t> bytes);
std::optional<Picture> read_png(const std::string& path);

}  // namespace ge::lab
