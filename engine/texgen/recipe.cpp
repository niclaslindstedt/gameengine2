// The recipe's parser and the texture reference's: lines to a Recipe checked against the layer
// table, every mistake a Diagnostic naming the line, what is wrong and what to write.
//
// ge:summary Parses recipes and texture references; resolves params; mixes the layer seeds
// ge:keywords parse recipe reference params seed diagnostic tokenize texgen
// ge:skill textures
// ge:test tests/texgen_recipe_test.cpp
#include "texgen/recipe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/num_text.hpp"
#include "texgen/layers.hpp"
#include "texgen/noise.hpp"

namespace ge::texgen {
namespace {

std::vector<std::string_view> words(std::string_view line) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

bool parse_number(std::string_view text, double& out) {
  if (text.empty()) return false;
  const std::string s(text);
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end == s.c_str() + s.size() && std::isfinite(out);
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

bool parse_colour(std::string_view text, Rgb& out) {
  if (text.size() != 7 || text[0] != '#') return false;
  double ch[3];
  for (int k = 0; k < 3; ++k) {
    const int hi = hex_digit(text[1 + 2 * k]), lo = hex_digit(text[2 + 2 * k]);
    if (hi < 0 || lo < 0) return false;
    ch[k] = (hi * 16 + lo) / 255.0;
  }
  out = {ch[0], ch[1], ch[2]};
  return true;
}

namespace {

bool valid_name(std::string_view s) {
  if (s.empty()) return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

std::string key_list(const LayerKind& kind) {
  std::string out;
  for (const KeyDef& k : kind.keys) out += std::string(out.empty() ? "" : " ") + k.name;
  return out;
}

std::string kind_list() {
  std::string out;
  for (const LayerKind& k : layer_kinds()) out += std::string(out.empty() ? "" : " ") + k.name;
  return out;
}

struct Parser {
  ParseResult result;
  int line = 0;

  void error(std::string message) {
    result.errors.push_back({line, "line " + std::to_string(line) + ": " + std::move(message)});
  }

  void layer(const std::vector<std::string_view>& w) {
    if (w.size() < 2) {
      error("`layer` needs a kind: one of " + kind_list() + ".");
      return;
    }
    const int k = find_layer_kind(w[1]);
    if (k < 0) {
      error("unknown layer kind `" + std::string(w[1]) + "`; the kinds are " + kind_list() +
            " (`ge texture kinds` prints their keys).");
      return;
    }
    const LayerKind& kind = layer_kinds()[k];
    LayerLine out{line, k, {}};
    for (size_t i = 2; i < w.size(); ++i) {
      const size_t eq = w[i].find('=');
      if (eq == std::string_view::npos || eq == 0) {
        error("`" + std::string(w[i]) + "` is not key=value; " + kind.name + " takes " +
              key_list(kind) + ".");
        continue;
      }
      const std::string_view key = w[i].substr(0, eq), text = w[i].substr(eq + 1);
      const KeyDef* def = find_key(kind, key);
      if (!def) {
        error("unknown key `" + std::string(key) + "` for layer " + kind.name + "; it takes " +
              key_list(kind) + ".");
        continue;
      }
      if (std::any_of(out.args.begin(), out.args.end(),
                      [&](const LayerArg& a) { return a.key == key; })) {
        error("key `" + std::string(key) + "` is given twice; keep one.");
        continue;
      }
      Value v;
      if (!text.empty() && text[0] == '$') {
        std::string_view name = text.substr(1);
        v.type = Value::Type::param;
        v.number = 1;
        const size_t star = name.find('*');
        if (star != std::string_view::npos) {
          if (!parse_number(name.substr(star + 1), v.number)) {
            error("`" + std::string(text) + "`: write a param as $name or $name*number.");
            continue;
          }
          name = name.substr(0, star);
        }
        if (def->type != KeyType::number) {
          error("key `" + std::string(key) + "` takes a " +
                (def->type == KeyType::colour ? "colour (#rrggbb)" : "word") +
                "; a param can only set a number.");
          continue;
        }
        v.word = std::string(name);
      } else if (def->type == KeyType::number) {
        if (!parse_number(text, v.number)) {
          error("key `" + std::string(key) + "` takes a number (" + def->help + "), not `" +
                std::string(text) + "`.");
          continue;
        }
      } else if (def->type == KeyType::colour) {
        v.type = Value::Type::colour;
        if (!parse_colour(text, v.colour)) {
          error("key `" + std::string(key) + "` takes a colour written #rrggbb, not `" +
                std::string(text) + "`.");
          continue;
        }
      } else {
        v.type = Value::Type::word;
        v.word = std::string(text);
        const auto allowed = words(def->words);
        if (!allowed.empty() && std::find(allowed.begin(), allowed.end(), text) == allowed.end()) {
          error("key `" + std::string(key) + "` takes one of: " + def->words + "; not `" +
                std::string(text) + "`.");
          continue;
        }
      }
      out.args.push_back({std::string(key), std::move(v)});
    }
    result.recipe.layers.push_back(std::move(out));
  }

  void header(const std::vector<std::string_view>& w) {
    Recipe& r = result.recipe;
    const std::string_view head = w[0];
    auto one_number = [&](double& out, double lo, double hi) {
      double v = 0;
      if (w.size() != 2 || !parse_number(w[1], v) || v < lo || v > hi) {
        error("`" + std::string(head) + "` takes one number from " + std::to_string(lo) + " to " +
              std::to_string(hi) + ".");
        return false;
      }
      out = v;
      return true;
    };
    if (head == "texture") {
      if (w.size() != 2 || !valid_name(w[1])) {
        error("`texture` takes one name of a-z, 0-9, _ and - (`texture brick_wall`).");
        return;
      }
      r.name = std::string(w[1]);
    } else if (head == "size") {
      double v = 0;
      if (!one_number(v, 16, 4096)) return;
      const int n = static_cast<int>(v);
      if (n != v || (n & (n - 1)) != 0) {
        error("`size` must be a power of two from 16 to 4096 (256, 512, 1024).");
        return;
      }
      r.size = n;
    } else if (head == "seed") {
      double v = 0;
      if (!one_number(v, 0, 4294967295.0) || v != std::floor(v)) {
        if (result.errors.empty() || result.errors.back().line != line)
          error("`seed` takes a whole number from 0 to 4294967295.");
        return;
      }
      r.seed = static_cast<uint32_t>(v);
    } else if (head == "surface") {
      if (w.size() != 2 || !valid_name(w[1])) {
        error("`surface` takes one word naming the material: stone, metal, wood, snow, ...");
        return;
      }
      r.surface = std::string(w[1]);
    } else if (head == "scale") {
      one_number(r.scale_m, 0.01, 1000);
    } else if (head == "bump") {
      one_number(r.bump, 0, 8);
    } else if (head == "tags") {
      for (size_t i = 1; i < w.size(); ++i) {
        if (!valid_name(w[i])) {
          error("tag `" + std::string(w[i]) + "`: a tag is one word of a-z, 0-9, _ and -.");
          continue;
        }
        r.tags.emplace_back(w[i]);
      }
    } else if (head == "param") {
      Param p;
      if (w.size() != 3 && w.size() != 5) {
        error("`param` takes a name and a default, and optionally a range: `param wear 0.3 0 1`.");
        return;
      }
      if (!valid_name(w[1]) || !parse_number(w[2], p.value) ||
          (w.size() == 5 &&
           (!parse_number(w[3], p.lo) || !parse_number(w[4], p.hi) || p.lo > p.hi))) {
        error("`param` takes a name and a default, and optionally a range: `param wear 0.3 0 1`.");
        return;
      }
      p.name = std::string(w[1]);
      for (const Param& q : r.params) {
        if (q.name == p.name) {
          error("param `" + p.name + "` is declared twice; keep one.");
          return;
        }
      }
      p.value = std::clamp(p.value, p.lo, p.hi);
      r.params.push_back(std::move(p));
    } else if (head == "layer") {
      layer(w);
    } else {
      error("unknown line `" + std::string(head) +
            "`; a recipe has texture, size, seed, surface, scale, bump, tags, param and layer "
            "lines (docs/textures.md).");
    }
  }
};

}  // namespace

ParseResult parse_recipe(std::string_view text) {
  Parser p;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    ++p.line;
    const size_t hash = line.find('#');
    // `#` opens a comment unless it starts a colour (`color=#8c4a32`).
    for (size_t h = hash; h != std::string_view::npos; h = line.find('#', h + 1)) {
      if (h == 0 || line[h - 1] == ' ' || line[h - 1] == '\t') {
        line = line.substr(0, h);
        break;
      }
    }
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto w = words(line);
    if (!w.empty()) p.header(w);
    if (end == text.size()) break;
    start = end + 1;
  }
  p.line = 0;
  if (p.result.recipe.name.empty())
    p.error("the recipe has no `texture <name>` line; add one at the top.");
  if (p.result.recipe.layers.empty())
    p.error("the recipe has no layers; add `layer fill color=#...` and build up from it.");
  // Every $param a layer names must be declared.
  for (const LayerLine& l : p.result.recipe.layers) {
    for (const LayerArg& a : l.args) {
      if (a.value.type != Value::Type::param) continue;
      const auto& ps = p.result.recipe.params;
      if (std::none_of(ps.begin(), ps.end(),
                       [&](const Param& q) { return q.name == a.value.word; })) {
        p.line = l.line;
        p.error("`$" + a.value.word + "` names no param; declare it: `param " + a.value.word +
                " <default> [lo hi]`.");
      }
    }
  }
  return std::move(p.result);
}

TextureRef parse_ref(std::string_view text, std::vector<Diagnostic>& errors) {
  TextureRef ref;
  const auto w = words(text);
  auto fail = [&](std::string m) { errors.push_back({0, "texture reference: " + std::move(m)}); };
  if (w.empty() || !valid_name(w[0])) {
    fail("start with the recipe's name: `cave_rock seed=88123 wear=0.8`.");
    return ref;
  }
  ref.name = std::string(w[0]);
  for (size_t i = 1; i < w.size(); ++i) {
    const size_t eq = w[i].find('=');
    double v = 0;
    if (eq == std::string_view::npos || eq == 0 || !parse_number(w[i].substr(eq + 1), v)) {
      fail("`" + std::string(w[i]) + "` is not name=number.");
      continue;
    }
    const std::string_view key = w[i].substr(0, eq);
    if (key == "seed") {
      if (v < 0 || v > 4294967295.0 || v != std::floor(v)) {
        fail("seed takes a whole number from 0 to 4294967295.");
        continue;
      }
      ref.seed = static_cast<uint32_t>(v);
    } else {
      ref.params.emplace_back(std::string(key), v);
    }
  }
  std::sort(ref.params.begin(), ref.params.end());
  return ref;
}

std::string ref_text(const TextureRef& ref) {
  std::string out = ref.name;
  if (ref.seed) out += " seed=" + std::to_string(*ref.seed);
  for (const auto& [k, v] : ref.params) {
    out += " " + k + "=" + core::num::to_string(v);
  }
  return out;
}

std::vector<double> resolve_params(const Recipe& recipe, const TextureRef& ref,
                                   std::vector<Diagnostic>& errors) {
  std::vector<double> out;
  out.reserve(recipe.params.size());
  for (const Param& p : recipe.params) out.push_back(p.value);
  for (const auto& [k, v] : ref.params) {
    bool found = false;
    for (size_t i = 0; i < recipe.params.size(); ++i) {
      if (recipe.params[i].name == k) {
        out[i] = std::clamp(v, recipe.params[i].lo, recipe.params[i].hi);
        found = true;
      }
    }
    if (!found) {
      std::string names;
      for (const Param& p : recipe.params) names += " " + p.name;
      errors.push_back({0, "texture reference: `" + recipe.name + "` has no param `" + k +
                               "`; its params are:" + (names.empty() ? " (none)" : names) + "."});
    }
  }
  return out;
}

uint32_t layer_seed(const Recipe& recipe, const TextureRef& ref, int layer_index) {
  return hash_u32(recipe.seed, ref.seed.value_or(0),
                  0x51ed2701u + static_cast<uint32_t>(layer_index));
}

std::string format_diagnostics(const std::vector<Diagnostic>& diags, std::string_view file) {
  std::string out;
  for (const Diagnostic& d : diags) {
    if (!file.empty()) out += std::string(file) + ": ";
    out += d.message + "\n";
  }
  return out;
}

}  // namespace ge::texgen
