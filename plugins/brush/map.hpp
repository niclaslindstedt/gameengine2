// A MAP IN ONE OF THE CLASSIC BSP FAMILIES, read into one view the rest of the engine uses
// whichever family the file was: what is drawn (surfaces as triangles over shared vertices, their
// textures and lightmaps), what is where (the planes, the node tree, its leaves and clusters, the
// visibility between clusters), the brush models, and the entity lump parsed into key/value
// blocks. Stated once here; read.hpp fills it, write.hpp makes fixture files that read into it.
//
// - **Families**: `v29` (version 29, one byte a luxel, 16-bit indices), `bsp2` (the same lumps
//   with 32-bit indices and float bounds), `v38` (texture names in the texinfo, RGB lightmaps,
//   clusters and areas), `v46` (shaders, vertices with their own texture and lightmap coordinates,
//   curved patches, lightmap pages, uncompressed cluster rows).
// - **Coordinates** are the file's: map units, z up. `to_engine` turns a point into the engine's
//   frame (x right, y up, z up the page seen from above) in metres at `kUnitM` a unit.
// - **Texture coordinates** (`Vertex::st`) are texels where the family stores them so (29, BSP2,
//   38: `st_texels`), whole textures in 46. **Lightmap coordinates** (`Vertex::lm`) are 0..1 across
//   the surface's lightmap: a face's own in 29 and 38, a 128 × 128 page in 46.
// - **Leaves**: `contents` is the family's own code (29: −1 empty, −2 solid, −3 water, −4 slime,
//   −5 lava, −6 sky; 38: a bit set, 1 solid; 46 stores none, 0), `solid` says it plainly (in 46: a
//   leaf with no cluster). A leaf's `cluster` is its row in the visibility (−1: none, so it sees
//   nothing and is seen by nothing).
// - **Children**: a node's child `c >= 0` is a node, `c < 0` the leaf `-(c + 1)`.
//
// Everything is checked when it is read (read.hpp), so every index here is in range.
//
// ge:summary The classic BSP families read into one view: surfaces, lightmaps, tree, PVS, entities
// ge:keywords bsp brush map family v29 bsp2 v38 v46 surface lightmap leaf cluster pvs visibility
// ge:keywords node plane entity model patch to_engine
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ge::brush {

enum class Family : uint8_t { v29, bsp2, v38, v46 };
const char* family_name(Family f);

// One map unit in metres: a standing figure of 56 units is 1.4 m.
inline constexpr double kUnitM = 0.025;

struct Vertex {
  float pos[3] = {};
  float st[2] = {};  // texture coordinates: texels (Map::st_texels) or whole textures
  float lm[2] = {};  // 0..1 across the surface's lightmap
  float nrm[3] = {};
};

enum class SurfaceKind : uint8_t { planar, patch, mesh, liquid, sky };

struct Surface {
  uint32_t texture = 0;
  uint32_t first = 0, count = 0;  // into Map::indices, three a triangle
  int32_t lightmap = -1;          // into Map::lightmaps
  SurfaceKind kind = SurfaceKind::planar;
  uint8_t styles = 0;  // light styles stored for the face (29, 38); the first is drawn
};

struct Texture {
  std::string name;
  uint32_t width = 0, height = 0;  // 0 when the family does not store it (38, 46)
  uint32_t flags = 0;              // the family's own surface flags
  // The full-size picture as palette indices, rows of `width` (29, BSP2, when the map carries its
  // pixels; empty otherwise). The palette is not in the map: a game's files hold it.
  std::vector<uint8_t> indexed;
};

struct Lightmap {
  uint32_t width = 0, height = 0;
  uint32_t offset = 0;  // into Map::light_rgb, three bytes a luxel, rows of `width`
};

struct Plane {
  float n[3] = {};
  float d = 0;
};

struct Node {
  uint32_t plane = 0;
  int32_t child[2] = {};  // [0] in front (n·p − d >= 0), [1] behind
};

struct Leaf {
  int32_t contents = 0;
  int32_t cluster = -1;
  bool solid = false;
  float mins[3] = {}, maxs[3] = {};
};

struct Model {
  float mins[3] = {}, maxs[3] = {};
  uint32_t first_surface = 0, surface_count = 0;
  int32_t head_node = 0;
};

struct Entity {
  std::vector<std::pair<std::string, std::string>> pairs;
  // The value of `key`, or "" when the block has none.
  const std::string& get(const std::string& key) const;
};

struct Map {
  Family family = Family::v29;
  int32_t version = 0;
  bool st_texels = true;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<Surface> surfaces;
  std::vector<Texture> textures;
  std::vector<Lightmap> lightmaps;
  std::vector<uint8_t> light_rgb;

  std::vector<Plane> planes;
  std::vector<Node> nodes;
  std::vector<Leaf> leaves;
  std::vector<Model> models;  // [0] the world

  int32_t clusters = 0;
  uint32_t row_bytes = 0;    // a cluster's row: one bit a cluster
  std::vector<uint8_t> pvs;  // clusters × row_bytes, decompressed

  std::string entity_text;
  std::vector<Entity> entities;

  // What was read but has no place in the view yet (clip hulls, brushes, light grid): counts, so
  // a lab can say it is there.
  uint32_t clipnodes = 0, brushes = 0, patches = 0, billboards = 0;

  size_t triangles() const { return indices.size() / 3; }
};

// The leaf `p` (file coordinates) falls in, walking model `model`'s tree.
uint32_t leaf_at(const Map& map, const float p[3], uint32_t model = 0);
// Whether cluster `from` can see cluster `to`; a cluster of −1 sees nothing. No visibility data:
// everything sees everything.
bool cluster_sees(const Map& map, int32_t from, int32_t to);
// `p` in file units to the engine's frame in metres.
void to_engine(const float p[3], double out[3]);
// An FNV-1a 64 digest of the whole view (geometry, lightmaps, tree, visibility, entities).
uint64_t map_digest(const Map& map);

}  // namespace ge::brush
