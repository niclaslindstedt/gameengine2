// THE ROOM LEVEL ON DISK (`.gelv`): the bytes of a level.hpp `Level`, stated here once.
//
// Little-endian throughout; a string is a u8 length and that many bytes (no terminator).
//
//   header   "GELV", version u16 (1 or 2), flags u16 (0), then four u16 counts:
//            materials, rooms, portals, solids                                    16 bytes
//   material name str, texture str, rgb 3 u8, relief u8, relief_cm u8, scale_dm u8  6 + strs
//   room     name str, origin 3 i32, seed u32, shape u8, floor u8, ceiling u8,
//            floor_y i16, ceil_y i16, cave_dm u8, cave_scale_dm u8, cave_cell_cm u8,
//            corner count u8, then per corner x i16, z i16, wall u8           27 + str + 5 n
//   portal   room u16, wall u8, to u16, trim u8, offset i16, width i16, sill i16, head i16  14
//   solid    room u16, material u8, min 3 i16, max 3 i16                                    15
//   (version 2 only, from here: the brush half)
//   header+  after the four counts: projections u16, faces u32, brushes u32, groups u16,
//            entities u32                                                                   16
//   material (each, after scale_dm) tex_w u16, tex_h u16, surface str                  4 + str
//   projection s 4 f32, t 4 f32                                                             32
//   face     n 3 f32, d f32, material u8, projection u16, flags u8 (1 hidden, 2 smooth)   20
//   brush    group u16, contents u8, face count u8 (its faces follow the last brush's)       4
//   group    name str
//   entity   classname str, group u16, key count u8, then per key: key str, value str
//   (both versions)
//   trailer  FNV-1a 32 of every byte before it                                               4
//
// `encode` writes version 1 when the level has nothing of the brush half (no brushes, faces,
// projections, groups, entities, sized textures or surfaces), so a file of rooms alone is the
// same bytes it always was; version 2 otherwise. Floats are stored as their bits.
//
// A six-walled room is about 60 bytes; a level of a dozen rooms under 2 KB. What a room grows
// into (build.hpp) is never stored: its seed is.
//
// `decode` refuses anything it cannot vouch for, with the byte offset and the next action: a
// wrong magic or version, a short file, bytes after the trailer, a checksum that does not match,
// and a level `validate` finds unsound. `encode(decode(b)) == b` for every file it accepts.
//
// ge:summary The room level's bytes on disk (.gelv): encode and decode with checksum and checks
// ge:keywords codec gelv binary bytes disk file format encode decode checksum fnv load save
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "rooms/level.hpp"

namespace ge::rooms {

inline constexpr uint16_t kFormatVersion = 2;  // the newest this build reads

std::vector<uint8_t> encode(const Level& level);
// The level in `bytes`, linked (level.hpp `link`); false with the reason in `*error`.
bool decode(std::span<const uint8_t> bytes, Level* out, std::string* error);

}  // namespace ge::rooms
