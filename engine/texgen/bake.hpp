// THE BAKE: a recipe and a texture reference in, the maps a renderer samples out. The layers
// paint the surface in order, then the derived maps are taken off it:
//
//   albedo  the colour, sRGB, RGBA8 (alpha 255)
//   normal  tangent space off the height (+x right, +y up the tile, +z out), RGBA8
//   orm     occlusion (off the relief), roughness, metalness in R, G, B: the glTF packing
//   glow    the emitted light, sRGB; `glows` says whether any texel lights at all
//   height  the height, grey in RGBA8, for parallax and for a level's displacement
//
// A bake is a pure function of (recipe, reference): the same bytes on every compiler, OS and
// worker count (tests/texgen_bake_test.cpp holds a golden of each library recipe's digest).
// `bake_to_disk` writes `<dir>/<name>-<key>_<map>.png` and `<dir>/<name>-<key>.meta`, where the
// key is a hash of everything the bytes depend on, so a cache directory never serves a stale
// map and two rooms that name the same variant share one set of files.
//
// ge:summary Bakes a recipe and a reference to albedo, normal, ORM, glow and height maps
// ge:keywords bake texture maps albedo normal-map orm occlusion roughness metalness glow
// ge:keywords emissive height png cache meta key digest bake_to_disk texgen
// ge:skill textures
// ge:test tests/texgen_bake_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "texgen/recipe.hpp"
#include "texgen/surface.hpp"

namespace ge::texgen {

enum class MapKind : uint8_t { albedo, normal, orm, glow, height, count };
inline constexpr int kMapCount = static_cast<int>(MapKind::count);
const char* map_name(MapKind kind);  // "albedo", "normal", ...

struct Maps {
  int size = 0;
  bool glows = false;
  std::vector<uint8_t> rgba[kMapCount];  // size * size * 4 each, row-major, y down
};

struct BakeResult {
  Maps maps;
  std::vector<Diagnostic> errors;  // the reference's: unknown params; empty when it baked
  bool ok() const { return errors.empty(); }
};

// The surface after the first `layer_count` layers (all of them for -1): what the labs show
// layer by layer. Errors as bake's.
Surface paint(const Recipe& recipe, const TextureRef& ref, std::vector<Diagnostic>& errors,
              int layer_count = -1);

// Layer `k` of `recipe` painted onto `s`, with `params` as resolve_params gave them: one step
// of `paint`, for a lab that times or pictures each layer.
void paint_layer(Surface& s, const Recipe& recipe, const TextureRef& ref,
                 const std::vector<double>& params, int k);

// The finished surface's maps.
Maps derive_maps(const Surface& surface, double bump);

BakeResult bake(const Recipe& recipe, const TextureRef& ref);

// The hex key of a bake: an FNV-1a 64 over the recipe's canonical text, the resolved seed and
// params and the size. Changes whenever the bytes could.
std::string bake_key(const Recipe& recipe, const TextureRef& ref);

// An FNV-1a 64 over every map's bytes: what the tests and labs hold.
uint64_t maps_digest(const Maps& maps);

struct DiskResult {
  std::vector<std::string> files;  // what was written (or found already there)
  std::vector<Diagnostic> errors;
  bool reused = false;  // every file was already on disk under this key
  bool ok() const { return errors.empty(); }
};

// Bake into `dir` (created if missing) unless this key's files are already there.
DiskResult bake_to_disk(const Recipe& recipe, const TextureRef& ref, const std::string& dir);

// The recipe written back in canonical form: headers in a fixed order, every layer's keys in
// the table's order with defaults left out. What the key hashes, and what `ge texture fmt` prints.
std::string canonical_text(const Recipe& recipe);

}  // namespace ge::texgen
