// A BAKED TEXTURE LIT ON THE CPU: the maps shaded under one light, the way a renderer will
// light them, so an agent looking at a lab or `ge texture show` sees the relief, the shine and
// the glow together rather than five flat maps. One light from the top left of the tile at
// about 40 degrees, a little ambient scaled by the occlusion, a Blinn-Phong highlight sharpened
// by smoothness and tinted by metal, and the glow added on top. Tiles repeat the texture, so a
// seam is a line across the picture.
//
// ge:summary Lights baked maps on the CPU for labs and previews: diffuse, highlight, glow, tiles
// ge:keywords preview lit shade light render cpu tiled seam look texgen lit_preview map_picture
// ge:skill textures labs
// ge:test tests/texgen_bake_test.cpp
#pragma once

#include "lab/raster.hpp"
#include "texgen/bake.hpp"

namespace ge::texgen {

// `maps` lit, `tiles` x `tiles` repeats drawn into a `px` square picture.
lab::Picture lit_preview(const Maps& maps, int px, int tiles = 1);

// One map as it is stored, `tiles` x `tiles` repeats in a `px` square picture.
lab::Picture map_picture(const Maps& maps, MapKind kind, int px, int tiles = 1);

}  // namespace ge::texgen
