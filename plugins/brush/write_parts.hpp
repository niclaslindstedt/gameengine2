// The fixture writers' shared compile: a fixture's faces, planes, tree, visibility and luxels,
// family-free, which each family's writer lays out in its own records.
//
// ge:summary The fixture compile every BSP writer shares: faces, planes, tree, visibility, luxels
// ge:keywords fixture compile faces planes tree split leaves visibility luxels rle packer
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "brush/bytes.hpp"
#include "brush/write.hpp"

namespace ge::brush::detail {

struct CPlane {
  int axis = 0;  // the normal is +axis
  float d = 0;
};

struct CFace {
  uint32_t room = 0;
  uint32_t plane = 0;
  bool back = false;  // the face looks along −plane normal
  float normal[3] = {};
  uint32_t texture = 0;
  std::vector<std::array<float, 3>> poly;  // clockwise seen from the room
  float vecs[2][4] = {};                   // s and t: xyz and offset
  int64_t base[2] = {}, size[2] = {};      // the lightmap: first luxel and luxels each way
  std::vector<uint8_t> luxels;             // grey, size[0] × size[1], rows of size[0]
};

struct CNode {
  uint32_t plane = 0;
  int32_t child[2] = {};  // >= 0 a node; < 0 the leaf -(c + 1); leaf 0 solid, leaf r + 1 room r
  float mins[3] = {}, maxs[3] = {};
};

struct Compiled {
  std::vector<std::string> textures;
  std::vector<CPlane> planes;
  std::vector<CFace> faces;
  std::vector<std::vector<uint32_t>> room_faces;
  std::vector<CNode> nodes;
  std::vector<std::vector<uint8_t>> sees;  // rooms × rooms
  std::string entities;
  float mins[3] = {}, maxs[3] = {};
};

bool compile(const Fixture& fx, Compiled* out, std::string* err);
// The luxel brightness at `p` on a surface facing `n` in room `room`.
uint8_t light_at(const Fixture& fx, uint32_t room, const double p[3], const float n[3]);
// The room `p` is in, or −1.
int room_of(const Fixture& fx, const float p[3]);
// One visibility row (a bit a cluster) run-length coded.
std::vector<uint8_t> rle_row(const std::vector<uint8_t>& row);
// A room's visibility row: one bit a room.
std::vector<uint8_t> vis_row(const Compiled& c, uint32_t room);

// Rectangles packed into 128 × 128 pages by shelves.
struct Packer {
  uint32_t page = 0, x = 0, y = 0, shelf = 0;
  // Where a `w` × `h` block goes; false if it can never fit a page.
  bool place(uint32_t w, uint32_t h, uint32_t* p, uint32_t* px, uint32_t* py);
};

// The family writers.
bool write_v29(const Fixture& fx, const Compiled& c, bool bsp2, Out* out, std::string* err);
bool write_v38(const Fixture& fx, const Compiled& c, Out* out, std::string* err);
bool write_v46(const Fixture& fx, const Compiled& c, Out* out, std::string* err);

}  // namespace ge::brush::detail
