// THE ROOM LEVEL: the engine's own level format, as data. A level is a few plain shapes that take
// little disk (rooms as a footprint polygon between a floor and a ceiling, openings between them,
// boxes standing in them) plus, in every room, the SEED its generated surfaces are grown from. A
// brick wall is stored and drawn as the flat wall it is; a rock wall or a cave is stored as the
// same few numbers and grown at load (build.hpp), the same bits on every machine, in about a
// millisecond a room.
//
// THE MODEL, stated here once (docs/levels.md is the prose):
// - Units: a stored coordinate is an integer number of kUnitM (1/128 m). A room's origin is
//   absolute (i32); its corners, floor, ceiling, openings and boxes are relative to it (i16, so a
//   room spans up to 256 m from its origin). World axes: x/z the plan, y up.
// - A ROOM is a simple polygon of corners, counter-clockwise seen from above (+x right, +z up the
//   page: the interior is on the left of each wall walked from its corner to the next), between
//   `floor_y` and `ceil_y`. Wall i runs from corner i to corner i + 1 and wears its corner's
//   material. Its `shape` is `prism` (flat floor, flat ceiling, straight walls, each wall grown
//   with its material's relief) or `cave` (the prism hollowed into rock by a seeded field:
//   build.hpp).
// - A MATERIAL is what a surface is made of: the texture reference the texture baker reads
//   (`<name> [seed=<u32>] [<param>=<n>...]`, the room's seed mixed in at bake), a fallback colour
//   for pictures drawn without textures, and the relief its surfaces grow (relief.hpp: rock, crag,
//   stones, ashlar, tiles, planks, corrugated, dents, or none), `relief_cm` deep with its largest
//   feature `scale_dm`, pushed into the room from a prism's walls, floor and ceiling.
// - A PORTAL is an opening in wall `wall` of room `room`: `offset` along the wall from its first
//   corner, `width` wide, from `sill` to `head` above the room's floor. `to` is the room it opens
//   into (kOutside for a window onto nothing); the wall of `to` it pierces is found by `link`,
//   never stored, so the two can never disagree. `trim` is the material of a frame round the
//   opening (kNoMaterial: none; a cave's mouth always gets one, to cover the rock's edge).
// - A SOLID is a box standing in a room (a pillar, a step, a crate), room-relative, one material.
//
// `validate` says what is wrong with a level in the diagnostics' shape (what, where, what to do);
// `link` fills the derived fields (each portal's far wall) a builder reads. The bytes on disk are
// codec.hpp's; the text form an agent writes is text.hpp's.
//
// ge:summary The room level format's data: rooms, materials, portals, solids; validate and link
// ge:keywords level room rooms level-format map portal opening doorway solid box material relief
// ge:keywords seed footprint polygon floor ceiling wall cave prism validate link units
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ge::rooms {

inline constexpr double kUnitM = 1.0 / 128.0;  // one stored coordinate step, m
inline constexpr uint16_t kOutside = 0xffff;   // a portal's `to` when it opens onto nothing
inline constexpr uint8_t kNoMaterial = 0xff;   // a portal's `trim` when it has no frame
inline constexpr size_t kMaxMaterials = 255;
inline constexpr size_t kMaxRooms = 0xfffe;
inline constexpr size_t kMaxCorners = 255;

// What a surface of a material grows into (relief.hpp has each one's shape).
enum class Relief : uint8_t {
  none = 0,
  rock = 1,
  crag = 2,
  stones = 3,
  ashlar = 4,
  tiles = 5,
  planks = 6,
  corrugated = 7,
  dents = 8,
};
enum class Shape : uint8_t { prism = 0, cave = 1 };

struct Material {
  std::string name;     // what the level calls it ("stone", "plaster"); unique in a level
  std::string texture;  // the texture reference: "<name> [seed=<u32>] [<param>=<n>...]"
  uint8_t rgb[3] = {160, 160, 160};  // the colour a picture without textures draws, sRGB
  Relief relief = Relief::none;
  uint8_t relief_cm = 0;  // how deep the relief reaches into the room, cm
  uint8_t scale_dm = 8;   // the relief's largest feature, dm
};

struct Corner {
  int16_t x = 0, z = 0;  // room-relative, units
  uint8_t wall = 0;      // the material of the wall from this corner to the next
};

struct Room {
  std::string name;               // unique in a level; what captions and portals say
  int32_t origin[3] = {0, 0, 0};  // absolute, units
  uint32_t seed = 0;              // what every generated surface of the room is grown from
  Shape shape = Shape::prism;
  uint8_t floor = 0, ceiling = 0;   // materials
  int16_t floor_y = 0, ceil_y = 0;  // room-relative, units
  // A cave's field: how far the rock reaches in from the prism (dm), its largest feature (dm),
  // and the cell the surface is found on (cm). Ignored by a prism.
  uint8_t cave_dm = 8, cave_scale_dm = 20, cave_cell_cm = 20;
  std::vector<Corner> corners;
};

struct Portal {
  uint16_t room = 0;
  uint8_t wall = 0;
  uint16_t to = kOutside;
  uint8_t trim = kNoMaterial;
  int16_t offset = 0, width = 0, sill = 0, head = 0;  // units, along and up wall `wall`
  // Derived by `link`, never stored: the wall of `to` this opening pierces (0 for kOutside).
  uint8_t to_wall = 0;
};

struct Solid {
  uint16_t room = 0;
  uint8_t material = 0;
  int16_t min[3] = {0, 0, 0}, max[3] = {0, 0, 0};  // room-relative, units
};

struct Level {
  std::vector<Material> materials;
  std::vector<Room> rooms;
  std::vector<Portal> portals;
  std::vector<Solid> solids;
};

// A stored coordinate in metres and back (the nearest step).
inline double metres(int64_t units) { return static_cast<double>(units) * kUnitM; }
int32_t units_of(double m);

// The room's corner `i` and the wall from it to the next, absolute metres in the plan (x, z).
void corner_m(const Room& room, size_t i, double out[2]);
double wall_length_m(const Room& room, size_t wall);

// Every problem with `level`, one line each in the diagnostics' shape; empty when it is sound.
// It checks the counts against the format's limits, every index, that each room's polygon is
// simple and counter-clockwise with its floor under its ceiling, that each portal fits its wall
// and its room, that a portal's far side is a wall of `to` lying on the same line, facing back,
// with the opening inside it, and that each solid is a box inside its room's height.
std::vector<std::string> validate(const Level& level);
// Fill the derived fields (each portal's `to_wall`). False, with `validate`'s first problem in
// `*error`, when the level is not sound.
bool link(Level& level, std::string* error);

// Index of the material or room named `name`, -1 when there is none.
int material_index(const Level& level, const std::string& name);
int room_index(const Level& level, const std::string& name);

}  // namespace ge::rooms
