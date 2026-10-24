// A CLASSIC MAP CLONED INTO A ROOM LEVEL: its structure carried over as brushes, its textures
// replaced. The map's solid space is the leaves of its node tree; each solid, liquid or sky leaf is
// a convex cell bounded by the planes on its path from the root, so each becomes one brush of its
// contents. The brush models (doors, buttons, plats, trains, triggers) become groups of brushes the
// same way from their own trees (a `trigger_` entity's as trigger volumes), and the entities come
// across with their keys, an entity's `model` key turned into its group. Version 29 and BSP2 maps;
// 38 and 46 to come.
//
// TEXTURES ARE NEVER COPIED. A map's pictures belong to whoever painted them, so the clone takes
// each texture's place from a TEXTURE MAP an agent has approved (`<map>.texmap`, written and
// re-scored by the procedural texture matcher): one line a texture,
//
//   texture <name in the map> <recipe reference...>   (a procedural texture, `brick seed=3 ...`)
//   liquid <name> <water|slime|lava|teleport> [<recipe reference...>]
//   sky <name> [<recipe reference...>]
//   surface <name> <surface reference...>            (drawn in its place: `grass meadow ...`)
//
// a `#` at a line's start or after a space starting a comment (a pinned line's `# pin`, the
// matcher's scores), any other line ignored. A texture the map does not name is refused, naming
// it, unless the clone is asked for placeholders: then it is drawn plain and reported. The one
// thing read from the pixels is a texture's mean colour, the flat colour a far face is drawn in.
//
// COORDINATES go through `brush::to_engine` (metres, y up); a projection is recovered from the
// surface lying on a face (the texel coordinates of its largest triangle solved for the axes in
// the face's plane), so the face's texels land where the map put them. An ENTITY's `origin` is
// written in metres in the engine's frame and its `angle` (degrees anticlockwise from the map's
// +x) as `heading` (degrees clockwise from +z, the engine's), up and down kept as -1 and -2.
// Other keys come across as written (a door's `speed` and `lip` in map units: M32g reads them).
//
// SMOOTHING (`smooth`): every face is marked smooth, so neighbours under about 53 degrees apart
// are shaded as one curve (a faceted arch drawn round) while walls meeting at a right angle stay
// sharp; the geometry is the map's.
//
// ge:summary Clones a version 29 or BSP2 map into a room level: tree cells to brushes, texture map
// ge:keywords clone cloner bsp convert brush cell leaf node-tree texture-map texmap recipe
// ge:keywords placeholder entity origin angle heading model group liquid sky smooth arch
// ge:skill rooms brush
// ge:test tests/rooms_clone_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brush/map.hpp"
#include "rooms/level.hpp"

namespace ge::rooms {

enum class TextureKind : uint8_t { texture, liquid, sky };

struct TextureChoice {
  std::string name;  // the texture's name in the map
  TextureKind kind = TextureKind::texture;
  std::string ref;      // the recipe reference ("" for none)
  std::string liquid;   // a liquid's kind: water, slime, lava, teleport
  std::string surface;  // a surface drawn in its place ("" for none)
};

struct TextureMap {
  std::vector<TextureChoice> rows;
  const TextureChoice* find(const std::string& name) const;
};

// `text` as a texture map; false with "line <n>: <what>; <what to do>" in `*error`.
bool parse_texture_map(std::string_view text, TextureMap* out, std::string* error);

struct CloneOptions {
  bool placeholders = false;         // an unmapped texture drawn plain instead of refused
  bool smooth = false;               // every face shaded round with its smooth neighbours
  std::span<const uint8_t> palette;  // 768 bytes: the textures' mean colours (else grey)
  size_t workers = 1;
};

struct CloneReport {
  size_t cells = 0;     // solid, liquid and sky leaves met
  size_t brushes = 0;   // cells kept as brushes
  size_t open = 0;      // cells whose planes did not close (left out)
  size_t too_many = 0;  // cells of more than 255 planes (left out)
  size_t faces = 0;     // faces written
  size_t unseen = 0;    // faces with no drawn surface on them (hidden)
  size_t groups = 0;    // brush models
  size_t entities = 0;
  std::vector<std::string> placeholders;  // textures drawn plain
};

// `map` cloned into `*out` (linked). False, with what failed and what to do in `*error`, for a
// family the cloner does not read yet, a texture the map does not name (without placeholders),
// or more textures than a level's 255 materials.
bool clone_map(const brush::Map& map, const TextureMap& textures, const CloneOptions& options,
               Level* out, CloneReport* report, std::string* error);

}  // namespace ge::rooms
