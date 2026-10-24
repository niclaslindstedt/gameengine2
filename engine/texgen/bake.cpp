// The bake's steps: the layers' arguments resolved against the reference, the surface painted,
// the maps derived off it, the canonical text and its key, and the files on disk.
//
// ge:summary The bake: resolved layer arguments, the derived maps, the key and the disk cache
// ge:keywords bake args resolve normal-map occlusion orm canonical key cache meta png
// ge:skill textures
// ge:test tests/texgen_bake_test.cpp
#include "texgen/bake.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "core/num.hpp"
#include "core/num_text.hpp"
#include "lab/png.hpp"
#include "texgen/layers.hpp"

namespace ge::texgen {
namespace {

constexpr std::array<const char*, kMapCount> kMapNames = {"albedo", "normal", "orm", "glow",
                                                          "height"};

uint8_t byte(double v) { return static_cast<uint8_t>(core::num::round(clamp01(v) * 255)); }

std::string hex_colour(const Rgb& c) {
  static const char* digits = "0123456789abcdef";
  std::string out = "#";
  for (const double ch : {c.r, c.g, c.b}) {
    const int b = byte(ch);
    out += digits[b >> 4];
    out += digits[b & 15];
  }
  return out;
}

Args resolve(const LayerLine& line, const Recipe& recipe, const std::vector<double>& params,
             uint32_t seed) {
  const LayerKind& kind = layer_kinds()[line.kind];
  Args a;
  a.keys = kind.keys;
  a.seed = seed;
  const size_t n = kind.keys.size();
  a.num.assign(n, 0);
  a.rgb.assign(n, {});
  a.word.assign(n, {});
  a.written.assign(n, 0);
  for (size_t k = 0; k < n; ++k) {
    const KeyDef& d = kind.keys[k];
    a.num[k] = d.def;
    if (d.type == KeyType::colour) {
      parse_colour(d.text, a.rgb[k]);  // the table's colours are well formed (a test holds it)
    }
    if (d.type == KeyType::word) a.word[k] = d.text;
  }
  for (const LayerArg& arg : line.args) {
    for (size_t k = 0; k < n; ++k) {
      if (arg.key != kind.keys[k].name) continue;
      const KeyDef& d = kind.keys[k];
      a.written[k] = 1;
      switch (arg.value.type) {
        case Value::Type::number:
          a.num[k] = std::clamp(arg.value.number, d.lo, d.hi);
          break;
        case Value::Type::param:
          for (size_t p = 0; p < recipe.params.size(); ++p) {
            if (recipe.params[p].name == arg.value.word)
              a.num[k] = std::clamp(params[p] * arg.value.number, d.lo, d.hi);
          }
          break;
        case Value::Type::colour:
          a.rgb[k] = arg.value.colour;
          break;
        case Value::Type::word:
          a.word[k] = arg.value.word;
          break;
      }
    }
  }
  return a;
}

std::string value_text(const Value& v) {
  switch (v.type) {
    case Value::Type::number:
      return core::num::to_string(v.number);
    case Value::Type::colour:
      return hex_colour(v.colour);
    case Value::Type::word:
      return v.word;
    case Value::Type::param:
      return "$" + v.word + (v.number == 1 ? "" : "*" + core::num::to_string(v.number));
  }
  return {};
}

uint64_t fnv(uint64_t h, std::string_view bytes) {
  for (const char c : bytes) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ull;
  }
  return h;
}

}  // namespace

const char* map_name(MapKind kind) { return kMapNames[static_cast<size_t>(kind)]; }

void paint_layer(Surface& s, const Recipe& recipe, const TextureRef& ref,
                 const std::vector<double>& params, int k) {
  const LayerLine& line = recipe.layers[k];
  // A layer with a salt draws the salt's pattern, so two layers can share one (the glow in
  // the cracks the cracks layer cut); any other draws its own, by its place.
  int place = k;
  for (const LayerArg& arg : line.args) {
    if (arg.key == "salt" && arg.value.type == Value::Type::number)
      place = 100000 + static_cast<int>(arg.value.number);
  }
  const Args a = resolve(line, recipe, params, layer_seed(recipe, ref, place));
  layer_kinds()[line.kind].paint(s, a);
}

Surface paint(const Recipe& recipe, const TextureRef& ref, std::vector<Diagnostic>& errors,
              int layer_count) {
  const std::vector<double> params = resolve_params(recipe, ref, errors);
  Surface s = make_surface(recipe.size);
  const int count = layer_count < 0 ? static_cast<int>(recipe.layers.size())
                                    : std::min(layer_count, static_cast<int>(recipe.layers.size()));
  for (int k = 0; k < count; ++k) paint_layer(s, recipe, ref, params, k);
  return s;
}

Maps derive_maps(const Surface& s, double bump) {
  Maps m;
  m.size = s.size;
  if (s.size <= 0 || s.size > 4096) return m;  // a recipe's size is 16..4096 (the parser holds it)
  const size_t n = static_cast<size_t>(s.size) * s.size;
  for (auto& map : m.rgba) map.assign(n * 4, 255);
  const auto& h = s.plane[kHeight];
  // A height step of 1 over 32 texels at 1024 px reads as 45 degrees at bump 1; the slope is
  // taken per tile, so a recipe looks the same at every size.
  const double k = bump * s.size / 32.0;
  const std::vector<float> rel = relief(s, std::max(2, s.size / 64));
  for_texels(s.size, [&](int x, int y, double, double) {
    const size_t i = s.at(x, y), o = i * 4;
    for (int c = 0; c < 3; ++c) m.rgba[0][o + c] = byte(s.plane[kR + c][i]);
    const double dx = (h[s.wrapped(x + 1, y)] - h[s.wrapped(x - 1, y)]) * 0.5 * k;
    const double dy = (h[s.wrapped(x, y + 1)] - h[s.wrapped(x, y - 1)]) * 0.5 * k;
    // +y is up the tile (toward v = 0): a height rising downward tilts the normal up.
    const double nx = -dx, ny = dy, nz = 1;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    m.rgba[1][o] = byte(0.5 + 0.5 * nx / len);
    m.rgba[1][o + 1] = byte(0.5 + 0.5 * ny / len);
    m.rgba[1][o + 2] = byte(0.5 + 0.5 * nz / len);
    m.rgba[2][o] = byte(1 - clamp01(rel[i] * 3));
    m.rgba[2][o + 1] = byte(s.plane[kRough][i]);
    m.rgba[2][o + 2] = byte(s.plane[kMetal][i]);
    for (int c = 0; c < 3; ++c) m.rgba[3][o + c] = byte(s.plane[kGlowR + c][i]);
    const uint8_t hb = byte(h[i]);
    m.rgba[4][o] = m.rgba[4][o + 1] = m.rgba[4][o + 2] = hb;
  });
  bool glows = false;
  for (size_t o = 0; o < m.rgba[3].size() && !glows; o += 4)
    glows = m.rgba[3][o] | m.rgba[3][o + 1] | m.rgba[3][o + 2];
  m.glows = glows;
  return m;
}

BakeResult bake(const Recipe& recipe, const TextureRef& ref) {
  BakeResult r;
  const Surface s = paint(recipe, ref, r.errors);
  r.maps = derive_maps(s, recipe.bump);
  return r;
}

std::string canonical_text(const Recipe& recipe) {
  std::string out = "texture " + recipe.name + "\n";
  out += "size " + std::to_string(recipe.size) + "\n";
  out += "seed " + std::to_string(recipe.seed) + "\n";
  out += "surface " + recipe.surface + "\n";
  out += "scale " + core::num::to_string(recipe.scale_m) + "\n";
  out += "bump " + core::num::to_string(recipe.bump) + "\n";
  if (!recipe.tags.empty()) {
    out += "tags";
    for (const std::string& t : recipe.tags) out += " " + t;
    out += "\n";
  }
  for (const Param& p : recipe.params) {
    out += "param " + p.name + " " + core::num::to_string(p.value) + " " +
           core::num::to_string(p.lo) + " " + core::num::to_string(p.hi) + "\n";
  }
  for (const LayerLine& l : recipe.layers) {
    const LayerKind& kind = layer_kinds()[l.kind];
    out += std::string("layer ") + kind.name;
    for (const KeyDef& d : kind.keys) {
      for (const LayerArg& a : l.args) {
        if (a.key == d.name) out += " " + a.key + "=" + value_text(a.value);
      }
    }
    out += "\n";
  }
  return out;
}

std::string bake_key(const Recipe& recipe, const TextureRef& ref) {
  std::vector<Diagnostic> ignored;
  const std::vector<double> params = resolve_params(recipe, ref, ignored);
  uint64_t h = fnv(14695981039346656037ull, canonical_text(recipe));
  h = fnv(h, "seed=" + std::to_string(ref.seed.value_or(0)));
  for (size_t i = 0; i < params.size(); ++i)
    h = fnv(h, recipe.params[i].name + "=" + core::num::to_string(params[i]));
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i, h >>= 4) out[i] = digits[h & 15];
  return out;
}

uint64_t maps_digest(const Maps& maps) {
  uint64_t h = 14695981039346656037ull;
  for (const auto& map : maps.rgba) {
    for (const uint8_t b : map) {
      h ^= b;
      h *= 1099511628211ull;
    }
  }
  return h;
}

DiskResult bake_to_disk(const Recipe& recipe, const TextureRef& ref, const std::string& dir) {
  DiskResult out;
  const std::string stem = dir + "/" + recipe.name + "-" + bake_key(recipe, ref);
  const std::string meta = stem + ".meta";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    out.errors.push_back({0, "could not create `" + dir + "` (" + ec.message() +
                                 "); pass a writable --out directory."});
    return out;
  }
  if (std::filesystem::exists(meta)) {
    // The meta is written last, so its presence says every map before it is complete.
    std::ifstream in(meta);
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind("file ", 0) == 0) out.files.push_back(dir + "/" + line.substr(5));
    }
    out.files.push_back(meta);
    out.reused = true;
    return out;
  }
  BakeResult b = bake(recipe, ref);
  if (!b.ok()) {
    out.errors = std::move(b.errors);
    return out;
  }
  std::string text = "texture " + recipe.name + "\nref " + ref_text(ref) + "\nkey " +
                     bake_key(recipe, ref) + "\nsize " + std::to_string(recipe.size) +
                     "\nsurface " + recipe.surface + "\nscale " +
                     core::num::to_string(recipe.scale_m) + "\n";
  if (!recipe.tags.empty()) {
    text += "tags";
    for (const std::string& t : recipe.tags) text += " " + t;
    text += "\n";
  }
  std::vector<Diagnostic> ignored;
  const std::vector<double> params = resolve_params(recipe, ref, ignored);
  for (size_t i = 0; i < params.size(); ++i)
    text += "param " + recipe.params[i].name + " " + core::num::to_string(params[i]) + "\n";
  for (int k = 0; k < kMapCount; ++k) {
    if (static_cast<MapKind>(k) == MapKind::glow && !b.maps.glows) continue;
    const std::string file = stem + "_" + kMapNames[k] + ".png";
    lab::Picture pic{b.maps.size, b.maps.size, std::move(b.maps.rgba[k])};
    if (!lab::write_png(file, pic)) {
      out.errors.push_back({0, "could not write `" + file + "`; check the disk and --out."});
      return out;
    }
    out.files.push_back(file);
    text += "file " + std::filesystem::path(file).filename().string() + "\n";
  }
  std::ofstream f(meta, std::ios::binary);
  f << text;
  if (!f) {
    out.errors.push_back({0, "could not write `" + meta + "`; check the disk and --out."});
    return out;
  }
  out.files.push_back(meta);
  return out;
}

}  // namespace ge::texgen
