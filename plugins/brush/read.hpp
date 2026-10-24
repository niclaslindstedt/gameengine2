// READING A MAP of any classic BSP family into the one view (map.hpp). The family is told by the
// file's first bytes: version 29, the magic `BSP2`, or `IBSP` with version 38 or 46. Every lump is
// bounds-checked against the file and its element size before it is read, and every index the
// view keeps (planes, nodes, leaves, edges, vertices, textures, lightmaps, visibility offsets) is
// checked against what it points into, so a damaged or hostile file is refused with the lump,
// the record and the byte offset rather than read out of range.
//
// What a reader makes of each family:
// - **29, BSP2**: each face's polygon (its surfedges and edges) fanned into triangles, wound to
//   face the way its plane and side say; texture coordinates from the texinfo's two vectors; the
//   face's lightmap sized from its extents (one luxel every 16 texels, ceil and floor outward)
//   and its grey luxels repeated three times into `light_rgb`, so a `.lit` file (`read_lit`)
//   drops in place; the miptex names and sizes; leaf `i > 0` is cluster `i − 1`; the clip hulls'
//   nodes checked and counted (their trace is M18b's).
// - **38**: the same polygons; textures are the texinfo's names (sizes live in other files); RGB
//   luxels; the leaves' own clusters; the visibility's per-cluster offsets.
// - **46**: polygons and meshes from their mesh-vertex lists, curved patches tessellated
//   (biquadratic, `patch_level` steps across each 3 × 3 piece, shared edges evaluated alike so
//   neighbours meet), billboards counted; shaders as textures; 128 × 128 lightmap pages; the
//   uncompressed cluster rows. A 46 leaf stores no contents: `solid` is a leaf with no cluster.
//
// The entity lump is parsed into blocks (`parse_entities`); a lump that does not parse refuses the
// map, naming the line.
//
// Pure: no allocation beyond the view, no state, the same view on every platform.
//
// ge:summary Reads any classic BSP family (29, BSP2, 38, 46) into the one map view, refusing damage
// ge:keywords read load bsp v29 bsp2 v38 v46 lump refuse validate lit entities parse pvs rle patch
// ge:keywords tessellate bezier miptex texinfo shader
// ge:skill brush
// ge:test tests/brush_read_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brush/map.hpp"

namespace ge::brush {

struct ReadOptions {
  int patch_level = 6;  // steps across each 3 × 3 piece of a curved patch (1..32)
};

// The map in `bytes`; false with the reason in `*error` (`*out` is then unspecified).
bool read_bsp(std::span<const uint8_t> bytes, Map* out, std::string* error,
              const ReadOptions& opts = {});
// A `.lit` file's RGB luxels into a 29 or BSP2 map read from the same compile.
bool read_lit(std::span<const uint8_t> bytes, Map* map, std::string* error);
// `{ "key" "value" ... }` blocks; `//` comments to the end of a line are skipped.
bool parse_entities(std::string_view text, std::vector<Entity>* out, std::string* error);

}  // namespace ge::brush
