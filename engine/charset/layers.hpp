// THE NOISE LAYERS a game lays over its text to make it belong to a world: tileable grey
// textures, each a pure function of its kind, its size and a seed, built by the engine and
// committed at 256 and 512 px (engine/charset/layers/<name>_<size>.png) for a renderer to
// upload beside the atlases. effects.hpp says what a look does with one: multiply the ink by
// it (a fill), wear the ink away where it is dark (erosion), or lay it over the whole run of
// text (an overlay). Every layer wraps at its edges, so it tiles at any scale.
//
// ge:summary Tileable grey noise layers for text: grain, grunge, scratches, scanlines, rust...
// ge:keywords layer noise texture tileable grain grunge scratches scanlines rust cracks worley
// ge:keywords halftone brushed fbm make_layer layer_info layers find_layer layer_path
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ge::charset {

enum class Layer : uint8_t {
  grain,      // film grain: fine, even, mid-grey on average
  grunge,     // blotches of dirt and wear: what erosion eats
  scratches,  // thin bright scratches at random angles
  scanlines,  // a CRT's horizontal lines, with a faint flicker of noise
  rust,       // pitted, turbulent corrosion for metal plates
  cracks,     // a cellular web of cracks: stone, dried paint, ice
  halftone,   // a printed dot screen whose dots swell and shrink
  brushed,    // brushed metal: long streaks one way
  count,
};
inline constexpr int kLayerCount = static_cast<int>(Layer::count);
inline constexpr int kLayerSizes[] = {256, 512};

struct LayerInfo {
  Layer id;
  const char* name;  // the file stem: "grunge"
  const char* use;   // one line on what it is for
};

std::span<const LayerInfo> layers();
const LayerInfo& layer_info(Layer id);
const LayerInfo* find_layer(std::string_view name);

// A square grey texture that wraps at its edges.
struct Texture {
  int size = 0;
  std::vector<uint8_t> value;
  // The texel at (x, y), wrapped.
  uint8_t at(int x, int y) const {
    const int s = size;
    x = ((x % s) + s) % s;
    y = ((y % s) + s) % s;
    return value[static_cast<size_t>(y) * s + x];
  }
};

// `layer` at `size` px (a power of two from 64 up), drawn from `seed`.
Texture make_layer(Layer layer, int size, uint32_t seed = 1);

// The repository path of a committed layer: "engine/charset/layers/grunge_512.png".
std::string layer_path(Layer layer, int size);

}  // namespace ge::charset
