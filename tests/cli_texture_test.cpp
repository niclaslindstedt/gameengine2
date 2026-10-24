// engine/cli/texture.hpp: `ge texture kinds` lists the layer table, `check` turns a recipe's
// mistakes into findings at their lines, `bake` writes a variant once and reuses it after.
//
// ge:summary Tests ge texture: kinds, check findings, bake and reuse in a cache directory
// ge:keywords test cli texture kinds check bake findings cache
// ge:skill testing agent-interface textures
#include "cli/texture.hpp"

#include <filesystem>
#include <fstream>

#include "check.hpp"

namespace fs = std::filesystem;
using namespace ge::cli;

TEST(kinds_lists_every_layer_kind) {
  const Outcome o = run_texture_kinds();
  CHECK(o.text.find("layer bricks") != std::string::npos);
  CHECK(o.text.find("layer glow") != std::string::npos);
  CHECK(exit_of(o) == 0);
}

TEST(check_and_bake_through_the_cli) {
  const fs::path repo = fs::temp_directory_path() / "ge_cli_texture_test";
  fs::remove_all(repo);
  fs::create_directories(repo);
  std::ofstream(repo / "good.tex") << "texture good\nsize 32\nparam age 0.5 0 1\n"
                                      "layer bricks rows=2 cols=2\nlayer grime amount=$age\n";
  std::ofstream(repo / "bad.tex") << "texture bad\nlayer fill colr=#ffffff\n";
  const Outcome bad = run_texture_check(repo, {"good.tex", "bad.tex"});
  CHECK(exit_of(bad) == 1);
  CHECK(bad.findings.size() == 1 && bad.findings[0].where == "bad.tex:2");
  const Outcome first = run_texture_bake(repo, "good.tex", "seed=4 age=0.9", "", "cache");
  CHECK(exit_of(first) == 0);
  CHECK(first.written.size() == 5);  // albedo, normal, orm, height, meta: it does not glow
  const Outcome again = run_texture_bake(repo, "good.tex", "seed=4 age=0.9", "", "cache");
  CHECK(again.written.empty() && again.summary.find("reused") != std::string::npos);
  const Outcome wrong = run_texture_bake(repo, "good.tex", "wear=1", "", "cache");
  CHECK(exit_of(wrong) == 1);
  const Outcome sized = run_texture_bake(repo, "good.tex", "", "100", "cache");
  CHECK(exit_of(sized) == 1);
  fs::remove_all(repo);
}
