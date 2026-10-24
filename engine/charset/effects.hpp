// THE EFFECT STACK that lights a run of text (text.hpp's coverage canvas) into colour: an ink
// gradient, a noise layer's fill, erosion by a layer, an outline, a drop shadow, a glow, a
// bevel and an overlay, in that order, every size a share of the text's cell height so a look
// is the same look at 12 px and at 120. Ten named LOOKS combine them for the jobs a game has
// (a readable HUD over snow, a CRT console, a rusted title plate, a worn stencil, ...).
//
// This is the CPU reference: the labs draw every font under every look with it, and a CPU
// picture can use it as it stands. A GPU renderer does the same per pixel off the atlas's
// coverage: the outline, shadow, glow and bevel read a distance field (here `distance_field`
// of the canvas; on the GPU one baked per atlas), the fills and the overlay sample the layer
// textures (layers.hpp). docs/charsets.md § Effects is the recipe.
//
// ge:summary The text effect stack and its ten looks: fill, erode, outline, shadow, glow, bevel
// ge:keywords effects look outline drop-shadow glow bevel emboss erosion fill overlay gradient
// ge:keywords distance-field edt compose looks find_look atmosphere
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "charset/charset.hpp"
#include "charset/layers.hpp"
#include "charset/text.hpp"

namespace ge::charset {

struct Rgb {
  double r, g, b;  // 0..1
};

// A layer applied at a strength: `scale` texels of the layer a canvas pixel.
struct LayerUse {
  bool on = false;
  Layer layer = Layer::grain;
  double strength = 0;
  double scale = 1;
};

// One look. Sizes are shares of the cell's height (0.05 of a 40 px cell is 2 px).
struct Look {
  const char* name = "";
  const char* use = "";
  Rgb top{1, 1, 1}, bottom{1, 1, 1};  // the ink, from the cap height to the baseline
  LayerUse fill;   // the ink multiplied by the layer: 1 - strength + strength * texel * 2
  LayerUse erode;  // ink worn away where the layer is under `strength`
  double outline = 0;
  Rgb outline_rgb{0, 0, 0};
  double shadow_dx = 0, shadow_dy = 0, shadow_blur = 0, shadow_alpha = 0;
  Rgb shadow_rgb{0, 0, 0};
  double glow = 0, glow_strength = 0;  // the glow's reach and how bright it starts
  Rgb glow_rgb{1, 1, 1};
  double bevel = 0, bevel_strength = 0;  // how far in the bevel reaches; light from the top left
  bool bevel_inset = false;              // carved in rather than raised
  LayerUse overlay;                      // over ink, outline and glow alike
};

enum class LookId : uint8_t {
  plain,
  shadow,
  outlined,
  rusted,
  phosphor,
  neon,
  worn,
  chrome,
  carved,
  amber,
  count,
};
inline constexpr int kLookCount = static_cast<int>(LookId::count);

std::span<const Look> looks();
const Look& look(LookId id);
const Look* find_look(std::string_view name);

// Every layer a look samples, built once at 256 px on first use (`make_layer`, seed 1).
struct LayerCache {
  std::array<Texture, kLayerCount> built;
  const Texture& get(Layer layer);
};

// The signed distance (px; negative inside) of every canvas pixel to the ink's edge at
// coverage one half: an exact Euclidean transform each side, the anti-aliased rim taken from
// the coverage itself.
std::vector<double> distance_field(const Canvas& canvas);

// `text` (drawn at `place` in a font with `metrics`) lit by `look` over `rgba` (`canvas`-sized,
// RGBA8, row-major: a lab::Picture's bytes), which it draws on in place.
void compose(const Canvas& text, const TextPlace& place, const VMetrics& metrics, const Look& look,
             LayerCache& layers, uint8_t* rgba);

// A whole run drawn and lit straight onto a picture (`width` x `height` RGBA8): `text` from
// `atlas` at `place` (in the picture's pixels) under `look`, its effects given room round it and
// clipped to the picture. What a CPU picture (a lab, a console snapshot) writes text with.
void paint_text(uint8_t* rgba, int width, int height, const Atlas& atlas, std::string_view text,
                const TextPlace& place, const Look& look, LayerCache& layers);

}  // namespace ge::charset
