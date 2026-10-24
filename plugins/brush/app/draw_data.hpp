// THE CLASSIC MAP EXPERIMENT'S DATA (lab `gpu-classic`): a version 29 map out of a pack file made
// ready to draw on the GPU. Its textures' pixels (every mip the file stores) through the pack's
// palette into one atlas; its lightmaps into another; its surfaces as vertices with each one's
// texture rectangle and kind; its liquids' top surfaces grouped into bodies by kind and level; and
// its light entities as point lights.
//
// ge:summary The classic map experiment's data: atlases, vertices, liquid bodies, point lights
// ge:keywords lab classic bsp pack palette miptex atlas lightmap liquid-body point-light flicker
// ge:skill brush labs
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brush/map.hpp"

namespace ge::brush::app {

// A pack file: a directory of named files over one block of bytes.
struct Pack {
  struct Entry {
    std::string name;
    uint32_t offset = 0, size = 0;
  };
  std::vector<uint8_t> bytes;
  std::vector<Entry> files;
};
bool read_pack(const std::string& path, Pack* out, std::string* error);
// The file `name`'s bytes, empty when the pack has none of that name.
std::span<const uint8_t> pack_file(const Pack& pack, std::string_view name);

// What a surface is drawn as (the vertex's `kind`).
enum Kind : uint32_t {
  kSolid = 0,
  kSky = 1,
  kWater = 2,
  kSlime = 3,
  kLava = 4,
  kHidden = 5,
  kWarp = 6
};
inline constexpr int kKinds = 7;

// One vertex: engine metres, the texture's texels, the lightmap atlas's uv, the normal, the
// texture's rectangle in the atlas (mip 0 texels: x, y, w, h), its kind.
struct DrawVertex {
  float pos[3];
  float st[2];
  float lm[2];
  float nrm[3];
  float rect[4];
  uint32_t kind;
  uint32_t pad;
};
static_assert(sizeof(DrawVertex) == 64);

// A liquid's top drawn by our renderer: its kind, its level (m) and its box over the plan (x0, z0,
// x1, z1, m).
struct LiquidPatch {
  Kind kind = kWater;
  double level = 0;
  double box[4] = {0, 0, 0, 0};
  double area = 0;            // m^2 of the top surfaces in it
  double centre[2] = {0, 0};  // x, z of its largest top surface's middle, m
  double largest = 0;         // that surface's area, m^2
};

// How a light entity's brightness moves (our own functions, picked by the entity's style number).
enum class Flicker : uint8_t { steady, candle, pulse, strobe, buzz };

struct PointLight {
  double pos[3] = {0, 0, 0};  // engine metres
  double value = 300;         // the entity's brightness
  double reach = 7.5;         // m: where its light ends (value / wait map units)
  double colour[3] = {1, 1, 1};
  Flicker flicker = Flicker::steady;
  uint32_t seed = 0;      // the flicker's phase
  bool flame = false;     // a torch or a flame (warmer)
  bool physical = false;  // `value` is a luminous intensity, cd (a glowing liquid's light)
};
// The light's share of its brightness at time t, s.
double flicker_at(const PointLight& light, double t);

struct Atlas {
  uint32_t width = 0, height = 0, mips = 1;
  std::vector<std::vector<uint8_t>> levels;  // RGBA8 a level; alpha 255 a fullbright texel
};

struct Scene {
  std::string name;
  Map map;
  Atlas textures;  // sRGB colours, every mip the file stores
  Atlas light;     // the lightmaps' first style, R = the luxel, a luxel's border round each
  std::vector<DrawVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<LiquidPatch> liquids;
  std::vector<PointLight> lights;
  double mins[3] = {0, 0, 0}, maxs[3] = {0, 0, 0};  // engine metres
  // Where the player starts: engine metres and a heading.
  double start[3] = {0, 0, 0};
  double start_heading = 0;
  uint32_t sky_texture = UINT32_MAX;  // the first sky texture (the classic before's)
  uint32_t counts[kKinds] = {};       // triangles of each kind
};

// THE TEXTURE SOURCE: where a texture's pixels come from. The map's own (its miptex through the
// palette) unless `replace` is set and fills `rgba` (w x h x 4, sRGB; alpha 255 a texel that
// glows) for the texture `name` of the map's size w x h, from its own colours `indexed` (palette
// indices) — a procedural texture picked by its likeness, say. False keeps the map's own.
struct TextureSource {
  bool (*replace)(void* user, std::string_view name, uint32_t w, uint32_t h,
                  std::span<const uint8_t> indexed, std::span<const uint8_t> palette,
                  std::vector<uint8_t>* rgba) = nullptr;
  void* user = nullptr;
};

// The map `path` in `pack` read and made ready, with `palette` (768 bytes) for its pixels.
bool load_scene(const Pack& pack, std::string_view path, std::span<const uint8_t> palette,
                Scene* out, std::string* error, const TextureSource& source = {});

// The classic's heading (degrees counter-clockwise from +x, from above) as the engine's.
double heading_of(double angle_deg);

}  // namespace ge::brush::app
