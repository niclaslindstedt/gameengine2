// A MAP DRAWN WITH ITS OWN PICTURES for the `bsp` lab: every surface of a 29 or BSP2 map
// textured from the palette-indexed pictures the map carries (`Texture::indexed`), through a
// 256-colour palette the caller hands in (a game's files hold it), and lit by its lightmap the
// way the family's light was meant: a luxel of 128 is the picture as painted, brighter up to
// twice, and the palette's last 32 colours glow at full brightness whatever the light. Sky is
// the sky picture's two layers seen along the view's direction; liquids are drawn unlit. Trigger
// volumes (brush models of a `trigger_` entity) are left out, as a game leaves them out.
//
// A perspective-correct software raster with a depth buffer, triangles cut at the near plane,
// drawn at twice the size and filtered down. A lab tool, not a renderer: the GPU's own map pass
// is docs/goals.md M18's.
//
// ge:summary Lab tool: a classic map drawn textured from its own pictures, palette and lightmaps
// ge:keywords lab bsp brush textured software raster palette miptex lightmap sky fullbright
// ge:keywords perspective-correct depth-buffer first-person view
// ge:skill brush labs
// ge:test tests/brush_read_test.cpp
#pragma once

#include <cstdint>
#include <span>

#include "brush/map.hpp"
#include "lab/raster.hpp"

namespace ge::brush::labs {

struct TexturedEye {
  double at[3] = {};     // file units, z up
  double yaw_deg = 0;    // 0 along +x, counter-clockwise from above (the entity's `angle`)
  double pitch_deg = 0;  // up positive
  double fov_deg = 90;   // across the panel's width
};

// Draws `map` into the panel at (x0, y0), w × h px of `p`, seen from `eye`; `palette` is 768
// bytes (256 RGB triples). Returns the triangles drawn.
size_t draw_textured(lab::Picture& p, int x0, int y0, int w, int h, const Map& map,
                     std::span<const uint8_t> palette, const TexturedEye& eye);

}  // namespace ge::brush::labs
