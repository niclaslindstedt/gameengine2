// The presets, the recipe's parser and printer, and the far ground's colour.
//
// ge:summary Kinds of grass definitions: the presets, parse_kind, print_kind, far_colour
// ge:keywords grass kind preset recipe parse print far-colour
// ge:skill grass
// ge:test tests/grass_kind_test.cpp
#include "grass/kind.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "core/num_text.hpp"

namespace ge::grass {

namespace {

Kind make(const char* name, float height, float width, float density, float lean, float clump,
          float pull, float give, float sway, std::array<float, 3> root, std::array<float, 3> tip,
          float dry) {
  Kind k;
  k.name = name;
  k.height = height;
  k.width = width;
  k.density = density;
  k.lean = lean;
  k.clump = clump;
  k.pull = pull;
  k.give = give;
  k.sway = sway;
  k.root = root;
  k.tip = tip;
  k.dry = dry;
  return k;
}

// Linear RGB, measured off the hue of living grass in daylight (a mid green is about 0.1 to 0.3
// linear in green) and of hay.
const std::vector<Kind>& table() {
  static const std::vector<Kind> kinds = [] {
    std::vector<Kind> v;
    v.push_back(make("lawn", 0.07f, 0.003f, 900, 0.15f, 0.0f, 0.0f, 11, 2.8f,
                     {0.040f, 0.085f, 0.016f}, {0.120f, 0.260f, 0.045f}, 0.02f));
    v.push_back(make("meadow", 0.35f, 0.005f, 400, 0.25f, 0.25f, 0.4f, 6, 1.6f,
                     {0.050f, 0.090f, 0.018f}, {0.180f, 0.300f, 0.060f}, 0.08f));
    v.push_back(make("long", 0.85f, 0.007f, 220, 0.35f, 0.35f, 0.35f, 3.5f, 0.9f,
                     {0.045f, 0.080f, 0.020f}, {0.230f, 0.300f, 0.080f}, 0.18f));
    v.push_back(make("tussock", 0.5f, 0.004f, 450, 0.45f, 0.4f, 0.85f, 5, 1.3f,
                     {0.060f, 0.080f, 0.025f}, {0.300f, 0.290f, 0.110f}, 0.35f));
    v.push_back(make("steppe", 0.45f, 0.0025f, 300, 0.3f, 0.2f, 0.3f, 7, 1.4f,
                     {0.110f, 0.100f, 0.040f}, {0.420f, 0.360f, 0.150f}, 0.6f));
    v.push_back(make("reeds", 1.6f, 0.012f, 90, 0.08f, 0.5f, 0.1f, 9, 0.6f,
                     {0.060f, 0.080f, 0.030f}, {0.250f, 0.270f, 0.110f}, 0.25f));
    v[2].height_jitter = 0.35f;
    v[5].height_jitter = 0.2f;
    v[5].lean_jitter = 0.1f;
    return v;
  }();
  return kinds;
}

bool read_number(std::string_view text, float& out) {
  const std::string s(text);
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (s.empty() || end != s.c_str() + s.size()) return false;
  out = static_cast<float>(v);
  return true;
}

bool read_rgb(std::string_view text, std::array<float, 3>& out) {
  for (int i = 0; i < 3; ++i) {
    const size_t comma = text.find(',');
    if ((i < 2) != (comma != std::string_view::npos)) return false;
    if (!read_number(text.substr(0, comma), out[static_cast<size_t>(i)])) return false;
    if (i < 2) text.remove_prefix(comma + 1);
  }
  return true;
}

// A number as the recipe writes it: up to four decimals, trailing zeros dropped.
std::string short_number(float v) {
  std::string s = core::num::to_fixed(static_cast<double>(v), 4);
  while (s.size() > 1 && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}

struct Field {
  const char* name;
  float Kind::*number;
  std::array<float, 3> Kind::*rgb;
};

const Field kFields[] = {
    {"height", &Kind::height, nullptr}, {"height_jitter", &Kind::height_jitter, nullptr},
    {"width", &Kind::width, nullptr},   {"density", &Kind::density, nullptr},
    {"lean", &Kind::lean, nullptr},     {"lean_jitter", &Kind::lean_jitter, nullptr},
    {"clump", &Kind::clump, nullptr},   {"pull", &Kind::pull, nullptr},
    {"give", &Kind::give, nullptr},     {"sway", &Kind::sway, nullptr},
    {"dry", &Kind::dry, nullptr},       {"root", nullptr, &Kind::root},
    {"tip", nullptr, &Kind::tip},       {"dry_tip", nullptr, &Kind::dry_tip},
};

std::string names() {
  std::string all;
  for (const Kind& k : table()) all += (all.empty() ? "" : ", ") + k.name;
  return all;
}

}  // namespace

std::span<const Kind> presets() { return table(); }

std::optional<Kind> preset(std::string_view name) {
  for (const Kind& k : table()) {
    if (k.name == name) return k;
  }
  return std::nullopt;
}

std::optional<Kind> parse_kind(std::string_view recipe, std::string* error) {
  const auto fail = [&](const std::string& what) -> std::optional<Kind> {
    if (error) *error = what;
    return std::nullopt;
  };
  std::vector<std::string_view> words;
  for (size_t i = 0; i < recipe.size();) {
    while (i < recipe.size() && recipe[i] == ' ') ++i;
    size_t j = i;
    while (j < recipe.size() && recipe[j] != ' ') ++j;
    if (j > i) words.push_back(recipe.substr(i, j - i));
    i = j;
  }
  if (words.empty()) return fail("an empty grass recipe: write a preset's name (" + names() + ")");
  std::optional<Kind> kind = preset(words[0]);
  if (!kind) {
    return fail("unknown grass preset '" + std::string(words[0]) + "': write one of " + names());
  }
  for (size_t w = 1; w < words.size(); ++w) {
    const std::string_view word = words[w];
    const size_t eq = word.find('=');
    if (eq == std::string_view::npos) {
      return fail("grass recipe word '" + std::string(word) +
                  "' has no '=': write field=value, e.g. height=0.6");
    }
    const std::string_view key = word.substr(0, eq), value = word.substr(eq + 1);
    const Field* field = nullptr;
    for (const Field& f : kFields) {
      if (key == f.name) field = &f;
    }
    if (!field) {
      std::string all;
      for (const Field& f : kFields) all += (all.empty() ? "" : ", ") + std::string(f.name);
      return fail("unknown grass field '" + std::string(key) + "': write one of " + all);
    }
    const bool ok = field->number ? read_number(value, (*kind).*(field->number))
                                  : read_rgb(value, (*kind).*(field->rgb));
    if (!ok) {
      return fail("grass field '" + std::string(key) + "' has value '" + std::string(value) +
                  (field->number ? "': write a number" : "': write r,g,b in linear 0..1"));
    }
  }
  if (kind->height <= 0 || kind->width <= 0 || kind->density < 0 || kind->give <= 0 ||
      kind->sway <= 0) {
    return fail("grass recipe '" + std::string(recipe) +
                "': height, width, give and sway must be above 0 and density at least 0");
  }
  return kind;
}

std::string print_kind(const Kind& kind) {
  const std::optional<Kind> base = preset(kind.name);
  const Kind ref = base ? *base : Kind{};
  std::string out = kind.name;
  for (const Field& f : kFields) {
    if (f.number) {
      if (kind.*(f.number) != ref.*(f.number)) {
        out += " " + std::string(f.name) + "=" + short_number(kind.*(f.number));
      }
    } else if (kind.*(f.rgb) != ref.*(f.rgb)) {
      const auto& c = kind.*(f.rgb);
      out += " " + std::string(f.name) + "=" + short_number(c[0]) + "," + short_number(c[1]) + "," +
             short_number(c[2]);
    }
  }
  return out;
}

std::array<float, 3> far_colour(const Kind& kind) {
  // From afar a viewer sees mostly the upper half of the blades, which is closer to the tip, and
  // the shade a field casts into itself between them (about a third of the light, judged against
  // the drawn field in the gpu-grass lab's `reach` view).
  constexpr float kSelfShade = 0.68f;
  std::array<float, 3> c{};
  for (size_t i = 0; i < 3; ++i) {
    const float green = 0.3f * kind.root[i] + 0.7f * kind.tip[i];
    const float dried = 0.3f * kind.root[i] + 0.7f * kind.dry_tip[i];
    c[i] = (green + (dried - green) * kind.dry) * kSelfShade;
  }
  return c;
}

}  // namespace ge::grass
