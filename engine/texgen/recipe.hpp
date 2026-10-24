// THE TEXTURE RECIPE: the text an agent writes to describe a texture, and its parser. A recipe
// is a few lines: what the texture is (its name, size, seed, the surface it stands for, the
// metres one tile covers, tags), the knobs a level may turn (`param`), and the layers that
// build it, bottom first. docs/textures.md is the format's guide; `ge texture kinds` prints
// every layer kind and key from the table in layers.cpp, which is the one place they are named.
//
//   # a brick wall that ages with the room
//   texture  brick_wall
//   size     512
//   seed     7
//   surface  stone
//   scale    2
//   tags     wall brick exterior
//   param    wear 0.3 0 1
//   layer    bricks rows=8 cols=4 color=#8c4a32 vary=0.18 mortar_color=#a49c8e
//   layer    noise kind=perlin period=16 octaves=5 amount=0.25 blend=overlay
//   layer    grime amount=0.6
//   layer    wear amount=$wear
//
// A value is a number, a colour (`#rrggbb`, sRGB), a word, or `$param` (optionally `$param*k`),
// resolved when the texture is baked. A TEXTURE REFERENCE is how a level names one baked
// variant: `<name> [seed=<u32>] [<param>=<number> ...]` (`cave_rock seed=88123 wear=0.8`); the
// reference's seed is mixed with the recipe's, so every room that names the same recipe with
// its own seed gets its own, stable texture.
//
// Every error is a Diagnostic with the line, what is wrong and what to write instead.
//
// ge:summary The texture recipe: its text format, parser, params and texture references
// ge:keywords texture recipe material description format parse param reference ref seed
// ge:keywords metadata surface tags scale diagnostic texgen parse_recipe parse_ref
// ge:skill textures
// ge:test tests/texgen_recipe_test.cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ge::texgen {

// A colour, sRGB, 0..1 a channel.
struct Rgb {
  double r = 0, g = 0, b = 0;
};

struct Diagnostic {
  int line = 0;         // 1-based; 0 for the reference or the recipe as a whole
  std::string message;  // what is wrong, then what to do: "line 4: unknown key ..."
};

// One key=value of a layer, as written.
struct Value {
  enum class Type : uint8_t { number, colour, word, param };
  Type type = Type::number;
  double number = 0;  // a number; for a param, its multiplier
  Rgb colour;
  std::string word;  // a word; for a param, its name
};

struct LayerArg {
  std::string key;
  Value value;
};

struct LayerLine {
  int line = 0;
  int kind = 0;  // an index into layer_kinds() (layers.hpp)
  std::vector<LayerArg> args;
};

struct Param {
  std::string name;
  double value = 0;  // the default
  double lo = 0, hi = 1;
};

struct Recipe {
  std::string name;
  int size = 512;  // a power of two, 16..4096
  uint32_t seed = 1;
  std::string surface = "generic";  // what the surface is made of: footsteps, decals, impacts
  double scale_m = 1;               // the metres one tile covers in the world
  double bump = 1;                  // the normal map's strength
  std::vector<std::string> tags;
  std::vector<Param> params;
  std::vector<LayerLine> layers;
};

// What a level names: a recipe, a seed and the params it turns.
struct TextureRef {
  std::string name;
  std::optional<uint32_t> seed;
  std::vector<std::pair<std::string, double>> params;
};

struct ParseResult {
  Recipe recipe;
  std::vector<Diagnostic> errors;
  bool ok() const { return errors.empty(); }
};

ParseResult parse_recipe(std::string_view text);

// A reference string; errors go to `errors`.
TextureRef parse_ref(std::string_view text, std::vector<Diagnostic>& errors);
// The reference written back in its canonical order (seed, then params by name).
std::string ref_text(const TextureRef& ref);

// The value of every param once `ref` has turned it, clamped to its range, in recipe order.
// A param the recipe lacks is an error.
std::vector<double> resolve_params(const Recipe& recipe, const TextureRef& ref,
                                   std::vector<Diagnostic>& errors);

// The seed a layer is drawn from: the recipe's, the reference's and the layer's place mixed,
// so changing one layer never re-rolls another.
uint32_t layer_seed(const Recipe& recipe, const TextureRef& ref, int layer_index);

// A colour written `#rrggbb`; false when it is not one.
bool parse_colour(std::string_view text, Rgb& out);

// "line 4: ..." lines, one a diagnostic.
std::string format_diagnostics(const std::vector<Diagnostic>& diags, std::string_view file = {});

}  // namespace ge::texgen
