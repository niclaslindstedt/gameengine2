// The pieces build.cpp and cave.cpp share, private to the builder: a room's openings in its own
// walls' terms, the emitter that keeps every triangle facing the way it was asked to, and boxes.
//
// ge:summary The room builder's shared parts: openings per wall, the facing emitter, boxes
// ge:keywords build openings emitter winding facing box oriented-box wall-frame
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rooms/build.hpp"
#include "rooms/level.hpp"

namespace ge::rooms::detail {

// One wall's frame, world metres: its first corner on the floor, the unit along it, the unit
// into the room (horizontal), its length and the room's height.
struct WallFrame {
  float o[3];
  float u[3];
  float n[3];
  float length, height;
};
WallFrame wall_frame(const Room& room, size_t wall);

// Signed distance from (px, pz) to the polygon of corners x/z, metres, negative inside.
float polygon_distance(const std::vector<float>& x, const std::vector<float>& z, float px,
                       float pz);

// An opening in one of a room's walls: along it from u0 to u1 and from v0 to v1 above the room's
// floor, metres; `trim` its frame's material (kNoMaterial: none); `own` when the room's own portal.
struct Opening {
  size_t wall;
  float u0, u1, v0, v1;
  uint8_t trim;
  bool own;
  size_t portal;  // its index in level.portals
};
std::vector<Opening> openings_of(const Level& level, size_t room);

struct Emitter {
  Mesh& m;
  uint32_t vertex(const float p[3], const float n[3], uint8_t mat);
  // A triangle wound so its front faces `facing`.
  void tri(uint32_t a, uint32_t b, uint32_t c, const float facing[3]);
  // A quad a-b-c-d (in order round it), split a-c.
  void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, const float facing[3]);
};

// A box on axes u, v, w (unit, orthogonal) from `o`: lo..hi along each, faces turned out, every
// face but those `skip` names (a bit a face: 0 -u, 1 +u, 2 -v, 3 +v, 4 -w, 5 +w).
void emit_box(Emitter& e, const float o[3], const float u[3], const float v[3], const float w[3],
              const float lo[3], const float hi[3], uint8_t mat, unsigned skip = 0);

// The frame round every trimmed opening the room owns.
void emit_frames(Emitter& e, const Level& level, size_t room, const std::vector<Opening>& open);

void build_prism(const Level& level, size_t room, const std::vector<Opening>& open, Mesh& out);
void build_cave(const Level& level, size_t room, const std::vector<Opening>& open, Mesh& out);

// A frame's size, metres: how far it reaches beyond the opening, how far into it (a cave's cell
// on either side, so the rock's edge is covered; 0 between prisms) and its half depth.
struct FrameSize {
  float out, in, half_depth;
};
FrameSize frame_size(const Level& level, const Portal& portal);

}  // namespace ge::rooms::detail
