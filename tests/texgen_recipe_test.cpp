// engine/texgen/recipe.hpp and library.hpp: every library recipe parses; each kind of mistake is
// a diagnostic naming its line and what to write; references parse, print and resolve; the
// canonical text reads back to itself; the layer table's own defaults are well formed.
//
// ge:summary Tests the recipe parser, its diagnostics, references, params and the layer table
// ge:keywords test texgen recipe parse diagnostic reference params canonical layer-table
// ge:skill testing textures
#include "texgen/recipe.hpp"

#include <string>

#include "check.hpp"
#include "texgen/bake.hpp"
#include "texgen/layers.hpp"
#include "texgen/library.hpp"

namespace tg = ge::texgen;

namespace {

std::string first_error(std::string_view text) {
  const tg::ParseResult r = tg::parse_recipe(text);
  return r.errors.empty() ? "" : r.errors[0].message;
}

bool has(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(every_library_recipe_parses) {
  const auto files = tg::list_recipes(ge::test::root_path(tg::kLibraryDir));
  CHECK(files.size() >= 12);
  for (const std::string& f : files) {
    const tg::ParseResult r = tg::load_recipe(f);
    CHECK_MSG(r.ok(), tg::format_diagnostics(r.errors, f));
    CHECK(!r.recipe.layers.empty());
  }
}

TEST(a_minimal_recipe_takes_defaults) {
  const tg::ParseResult r = tg::parse_recipe("texture plain\nlayer fill\n");
  CHECK(r.ok());
  CHECK(r.recipe.name == "plain" && r.recipe.size == 512 && r.recipe.seed == 1);
  CHECK(r.recipe.surface == "generic" && r.recipe.scale_m == 1);
}

TEST(mistakes_name_their_line_and_the_fix) {
  CHECK(has(first_error("texture t\nlayer fill colr=#ffffff\n"), "line 2: unknown key `colr`"));
  CHECK(has(first_error("texture t\nlayer fill colr=#ffffff\n"), "it takes color height"));
  CHECK(has(first_error("texture t\nlayer brick\n"), "unknown layer kind `brick`"));
  CHECK(has(first_error("texture t\nlayer fill color=red\n"), "#rrggbb"));
  CHECK(has(first_error("texture t\nlayer noise kind=simplex\n"), "takes one of: value perlin"));
  CHECK(has(first_error("texture t\nsize 500\nlayer fill\n"), "power of two"));
  CHECK(has(first_error("texture t\nlayer wear amount=$age\n"), "declare it: `param age"));
  CHECK(has(first_error("texture t\nlayer fill height=high\n"), "takes a number"));
  CHECK(has(first_error("texture t\nlayer fill color=$x\n"), "a param can only set a number"));
  CHECK(has(first_error("layer fill\n"), "no `texture <name>` line"));
  CHECK(has(first_error("texture t\n"), "no layers"));
  CHECK(has(first_error("texture t\nshine 4\nlayer fill\n"), "unknown line `shine`"));
  CHECK(has(first_error("texture t\nparam wear\nlayer fill\n"), "`param wear 0.3 0 1`"));
}

TEST(comments_and_colours_share_the_hash) {
  const tg::ParseResult r =
      tg::parse_recipe("# a comment\ntexture t  # trailing\nlayer fill color=#102030 # c\n");
  CHECK_MSG(r.ok(), tg::format_diagnostics(r.errors));
  CHECK(r.recipe.layers[0].args.size() == 1);
  CHECK(r.recipe.layers[0].args[0].value.colour.g == 0x20 / 255.0);
}

TEST(references_parse_print_and_resolve) {
  std::vector<tg::Diagnostic> errors;
  const tg::TextureRef ref = tg::parse_ref("brick_wall wear=0.8 seed=12 dirt=2", errors);
  CHECK(errors.empty());
  CHECK(ref.name == "brick_wall" && ref.seed && *ref.seed == 12);
  CHECK(tg::ref_text(ref) == "brick_wall seed=12 dirt=2 wear=0.8");
  const tg::ParseResult r = tg::parse_recipe(
      "texture brick_wall\nparam wear 0.3 0 1\nparam dirt 0.1 0 1\nlayer wear amount=$wear\n");
  const std::vector<double> p = tg::resolve_params(r.recipe, ref, errors);
  CHECK(errors.empty());
  CHECK(p.size() == 2 && p[0] == 0.8 && p[1] == 1);  // dirt clamped to its range
  tg::parse_ref("brick_wall age=1", errors);
  tg::resolve_params(r.recipe, tg::parse_ref("brick_wall age=1", errors), errors);
  CHECK(!errors.empty() && has(errors.back().message, "its params are: wear dirt"));
  std::vector<tg::Diagnostic> bad;
  tg::parse_ref("brick_wall seed=-1", bad);
  CHECK(!bad.empty());
}

TEST(layer_seeds_differ_by_place_and_room) {
  const tg::Recipe r = tg::parse_recipe("texture t\nlayer fill\n").recipe;
  const tg::TextureRef a{"t", 1, {}}, b{"t", 2, {}};
  CHECK(tg::layer_seed(r, a, 0) != tg::layer_seed(r, a, 1));
  CHECK(tg::layer_seed(r, a, 0) != tg::layer_seed(r, b, 0));
  CHECK(tg::layer_seed(r, a, 3) == tg::layer_seed(r, a, 3));
}

TEST(canonical_text_reads_back_to_itself) {
  for (const std::string& f : tg::list_recipes(ge::test::root_path(tg::kLibraryDir))) {
    const tg::ParseResult r = tg::load_recipe(f);
    const std::string once = tg::canonical_text(r.recipe);
    const tg::ParseResult again = tg::parse_recipe(once);
    CHECK_MSG(again.ok(), f + ": " + tg::format_diagnostics(again.errors));
    CHECK_MSG(tg::canonical_text(again.recipe) == once, f + " does not round-trip");
  }
}

TEST(the_layer_table_is_well_formed) {
  for (const tg::LayerKind& kind : tg::layer_kinds()) {
    CHECK(kind.paint != nullptr);
    CHECK(tg::find_layer_kind(kind.name) >= 0);
    for (const tg::KeyDef& k : kind.keys) {
      if (k.type == tg::KeyType::colour) {
        tg::Rgb c;
        CHECK_MSG(tg::parse_colour(k.text, c), std::string(kind.name) + "." + k.name);
      } else if (k.type == tg::KeyType::number) {
        CHECK_MSG(k.lo <= k.def && k.def <= k.hi, std::string(kind.name) + "." + k.name);
      } else if (*k.words) {
        const std::string words = std::string(" ") + k.words + " ";
        CHECK_MSG(has(words, std::string(" ") + k.text + " "),
                  std::string(kind.name) + "." + k.name + "'s default is not one of its words");
      }
    }
  }
}
