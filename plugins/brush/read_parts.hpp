// The readers' shared parts: the lump directory, a refusal's wording, the visibility's run-length
// rows, a planar polygon fanned and wound, a face's lightmap extents, and the checks every family
// runs on the tree once it is read.
//
// ge:summary Shared parts of the BSP readers: lumps, refusals, rle rows, polygons, extents, checks
// ge:keywords lump directory refuse rle visibility polygon fan winding extents lightmap tree check
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "brush/bytes.hpp"
#include "brush/map.hpp"
#include "brush/read.hpp"

namespace ge::brush::detail {

struct Lump {
  uint32_t off = 0, len = 0;
  const char* name = "";
};

// Fills `*err` with "bsp <family>: <what>; next: <action>" and returns false.
bool refuse(std::string* err, Family f, const std::string& what, const char* next);

// `n` lumps (offset, length) at the cursor, each checked inside a file of `size` bytes.
bool read_lumps(Cursor& c, Family f, std::span<const char* const> names, size_t size,
                std::vector<Lump>* out, std::string* err);
// `lump`'s element count at `elem` bytes each; false (refused) when the length is not a multiple.
bool count_of(const Lump& lump, size_t elem, Family f, size_t* n, std::string* err);
// A cursor over one lump of `file`.
Cursor at_lump(std::span<const uint8_t> file, const Lump& lump);

// One run-length coded row (a non-zero byte is eight bits; a zero byte and a count stand for that
// many zero bytes) from `vis` at `offset`, `row_bytes` long, into `row`.
bool unpack_row(std::span<const uint8_t> vis, uint32_t offset, uint32_t row_bytes, uint8_t* row);

// Appends the polygon `poly` (vertex indices into map->vertices, at least 3) as a fan, wound
// counter-clockwise seen from the side `n` points to.
void fan(Map* map, std::span<const uint32_t> poly, const float n[3]);

// The texture-space bounds of a face's vertices and its lightmap's size: one luxel every 16
// texels, `w = ceil(max / 16) − floor(min / 16) + 1`; sizes of 0 when the coordinates are past
// ±10^7 or not numbers (the caller refuses the face's lightmap).
struct Extent {
  double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
  int64_t base[2] = {}, size[2] = {};
};
Extent extent_of(const Map& map, uint32_t first_vertex, uint32_t count);
// Each vertex's `lm` from its `st` and the extent.
void place_lightmap(Map* map, uint32_t first_vertex, uint32_t count, const Extent& e);

// The vertex, edge and surfedge lumps a 29, BSP2 or 38 face's polygon is read through, each
// index checked against what it names.
struct Polys {
  std::vector<std::array<float, 3>> verts;
  std::vector<std::array<uint32_t, 2>> edges;
  std::vector<int32_t> surfedges;
};
bool read_polys(std::span<const uint8_t> file, const Lump& verts, const Lump& edges,
                const Lump& surfedges, bool wide_edges, Family f, Polys* out, std::string* err);
// Whether `first`, `count` is a face's polygon inside the surfedges (3 to 1024 edges).
bool polygon_fits(const Polys& p, int32_t first, int32_t count);
// The face's polygon appended as vertices (texture coordinates from `vecs`, normal `nrm`) and
// fanned; returns its first vertex.
uint32_t add_polygon(Map* map, const Polys& p, int32_t first, int32_t count, const float nrm[3],
                     const float vecs[2][4]);

// The node tree, the leaves and the models checked against each other, and the visibility rows'
// size against the cluster count.
bool check_tree(const Map& map, std::string* err);

}  // namespace ge::brush::detail

namespace ge::brush {
// The three family readers (read.cpp dispatches to them).
bool read_v29(std::span<const uint8_t> file, bool bsp2, Map* out, std::string* err);
bool read_v38(std::span<const uint8_t> file, Map* out, std::string* err);
bool read_v46(std::span<const uint8_t> file, Map* out, std::string* err, const ReadOptions& o);
}  // namespace ge::brush
