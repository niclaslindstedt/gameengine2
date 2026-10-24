// WHERE GRASS GROWS AND EVERY BLADE OF IT (docs/grass.md § Cover): a COVER is a list of
// triangles, each with the kind of grass on it and how much of that kind's density it carries at
// each corner; the blades are SCATTERED on a triangle from a seed hashed off the triangle's own
// corners, so the same ground grows the same blades whatever order a cover lists it in, on the
// CPU and on the GPU (render/grass_scatter.wgsl does exactly this, with `hash` and `unit` below).
//
// - FROM A HEIGHTFIELD (`cover_heightfield`): a region of a field at a cell of its own, two
//   triangles a cell, each corner's kind and share asked of the game (a meadow, a lawn, nothing on
//   a road). A cell whose corners are all bare is left out.
// - FROM ANY MESH (`add_mesh`): a level's floors (the room format's grass material, a classic
//   map's grass-textured faces): every triangle that faces up enough (`min_up`, the normal's y) is
//   taken, its winding either way. Grass grows UP, never along a wall's normal.
// - Either way a triangle whose blades would pass half of kMaxBladesPerTri at its kind's density
//   is halved across its longest edge until they do not, so a cover's kinds are set first.
// - THE SCATTER (`scatter`): a triangle of area A under a kind of density D at the corners'
//   largest share S holds floor(A D S) blades and one more by its seed's chance; each blade is a
//   uniform point (the square-root fold of two hashed numbers), kept by the share at that point
//   over S, so a cover fades out where its share does. Its clump is the nearest of the jittered
//   centres of the kind's `clump` grid round it: the clump scales its height and pulls its lean
//   toward the clump's middle. A blade's RANK (0..1) is what the picture thins it by with distance
//   (the farther, the fewer, the ones with the lowest rank kept), so thinning never reshuffles.
//
// No transcendental is called and every number is a float in the GPU's order, so the CPU's
// blades and the GPU's agree (tests/render_grass_test.cpp) and the CPU's are golden
// (tests/grass_cover_test.cpp).
//
// ge:summary Where grass grows: covers of triangles from a heightfield or a mesh, the blade scatter
// ge:keywords grass cover scatter blade triangle heightfield mesh floor density share clump
// ge:keywords rank thinning hash unit cover_heightfield add_mesh scatter blade-count
// ge:skill grass
// ge:test tests/grass_cover_test.cpp
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "core/heightfield.hpp"
#include "grass/kind.hpp"

namespace ge::grass {

inline constexpr uint32_t kMaxKinds = 8;            // a cover's kinds (the GPU's table)
inline constexpr uint32_t kMaxBladesPerTri = 4096;  // a triangle's blades, before thinning

// The integer hash every blade is drawn from (PCG's output permutation of one LCG step): the
// same on the CPU and in WGSL, since both are 32-bit wrapping arithmetic.
inline uint32_t hash(uint32_t v) {
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
// A hash's top 24 bits as a float in [0, 1), exact in a float.
inline float unit(uint32_t h) { return static_cast<float>(h >> 8u) * (1.0f / 16777216.0f); }

// A triangle of cover: three corners (world m), the kind (an index into `Cover::kinds`) and the
// share of that kind's density at each corner (0..1).
struct Tri {
  float a[3], b[3], c[3];
  float share[3] = {1, 1, 1};
  uint32_t kind = 0;
};

struct Cover {
  std::vector<Kind> kinds;  // at most kMaxKinds
  std::vector<Tri> tris;
  uint32_t seed = 0;  // mixed into every triangle's own
};

// What grows at a corner: which kind, and its share (0: bare).
struct CoverAt {
  uint32_t kind = 0;
  float share = 0;
};

// A region of a heightfield, world m.
struct Region {
  double x0 = 0, z0 = 0, x1 = 0, z1 = 0;
};

// The cells of `region` at `cell` m, two triangles each (corner heights off the field), each
// corner's kind and share asked of `at`. A cell's kind is its first corner's; a cell whose corners
// are all bare is left out.
void cover_heightfield(Cover& cover, const core::Heightfield& field, const Region& region,
                       double cell, const std::function<CoverAt(double x, double z)>& at);

// Every triangle of a mesh (positions xyz, indices three a triangle) whose normal's y is at least
// `min_up` either way round, under `kind` (already in `cover.kinds`) at `share`.
void add_mesh(Cover& cover, std::span<const float> positions, std::span<const uint32_t> indices,
              uint32_t kind, float share = 1, float min_up = 0.6f);

// One blade, as render/grass_scatter.wgsl writes it (32 bytes).
struct Blade {
  float root[3];  // world m
  uint32_t seed;  // its own hash (low 24 bits) and its kind (high 8)
  float height;   // m
  float width;    // m, at the root
  float lean[2];  // its rest lean as a plan vector, rad (x, z)
};
static_assert(sizeof(Blade) == 32, "match grass_types.wgsl's Blade");

inline uint32_t kind_of(const Blade& b) { return b.seed >> 24u; }

// A triangle's own seed: its corners at a centimetre, hashed with the cover's.
uint32_t tri_seed(const Tri& tri, uint32_t cover_seed);

// The blades a triangle could hold before thinning by the shares (its count's ceiling).
uint32_t blade_slots(const Tri& tri, const Kind& kind, uint32_t seed);

// Blade `slot` of a triangle with its rank (0..1; the picture keeps the lowest), or false if the
// share at its point does not keep it.
bool scatter_one(const Tri& tri, const Kind& kind, uint32_t seed, uint32_t slot, Blade& out,
                 float& rank);

// Every blade of triangle `i` whose rank is under `keep`, appended to `out`.
void scatter(const Cover& cover, size_t i, std::vector<Blade>& out, float keep = 1);

// The whole cover's blades (every rank).
std::vector<Blade> scatter_all(const Cover& cover);

// The area of a triangle, m².
float tri_area(const Tri& tri);

}  // namespace ge::grass
