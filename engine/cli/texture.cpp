// The `ge texture` commands over engine/texgen: the layer table printed, recipes checked into
// findings, and one variant baked to the cache.
//
// ge:summary ge texture's commands: the kinds table, recipe findings, a bake to the cache
// ge:keywords cli texture kinds check bake findings texgen
// ge:skill agent-interface textures
// ge:test tests/cli_texture_test.cpp
#include "cli/texture.hpp"

#include <chrono>

#include "cli/json.hpp"
#include "core/num_text.hpp"
#include "texgen/bake.hpp"
#include "texgen/layers.hpp"
#include "texgen/library.hpp"

namespace ge::cli {
namespace {

std::string default_text(const texgen::KeyDef& k) {
  if (k.type == texgen::KeyType::number) {
    return core::num::to_string(k.def) + " (" + core::num::to_string(k.lo) + ".." +
           core::num::to_string(k.hi) + ")";
  }
  if (k.type == texgen::KeyType::word && *k.words)
    return std::string(k.text) + " (" + k.words + ")";
  return k.text;
}

}  // namespace

Outcome run_texture_kinds() {
  Outcome o;
  std::vector<std::string> kinds;
  for (const texgen::LayerKind& kind : texgen::layer_kinds()) {
    o.text += std::string("layer ") + kind.name + "  [" + kind.group + "] " + kind.help + "\n";
    std::vector<std::string> keys;
    for (const texgen::KeyDef& k : kind.keys) {
      o.text += std::string("    ") + k.name + "=" + default_text(k) + "  " + k.help + "\n";
      keys.push_back(json_obj({{"key", json_str(k.name)},
                               {"default", json_str(default_text(k))},
                               {"help", json_str(k.help)}}));
    }
    kinds.push_back(json_obj({{"kind", json_str(kind.name)},
                              {"group", json_str(kind.group)},
                              {"help", json_str(kind.help)},
                              {"keys", json_arr(keys)}}));
  }
  o.result = json_arr(kinds);
  o.summary = std::to_string(kinds.size()) + " layer kinds";
  o.next = "write a recipe (docs/textures.md), then ge texture check <file.tex>";
  return o;
}

Outcome run_texture_check(const std::filesystem::path& repo,
                          const std::vector<std::string>& files) {
  Outcome o;
  std::vector<std::string> checked;
  for (const std::string& f : files) {
    const std::filesystem::path p = std::filesystem::absolute(repo / f);
    const texgen::ParseResult r = texgen::load_recipe(p.string());
    for (const texgen::Diagnostic& d : r.errors) {
      o.findings.push_back({Severity::error, d.message,
                            rel_path(repo, p) + (d.line ? ":" + std::to_string(d.line) : ""),
                            "fix the line (ge texture kinds lists every kind and key; skill: "
                            "textures)"});
    }
    if (r.ok()) {
      checked.push_back(
          json_obj({{"file", json_str(rel_path(repo, p))},
                    {"texture", json_str(r.recipe.name)},
                    {"layers", json_num(static_cast<long long>(r.recipe.layers.size()))}}));
      o.text += rel_path(repo, p) + ": " + r.recipe.name + ", " +
                std::to_string(r.recipe.layers.size()) + " layers, " +
                std::to_string(r.recipe.params.size()) + " params\n";
    }
  }
  o.result = json_arr(checked);
  o.summary = std::to_string(checked.size()) + " of " + std::to_string(files.size()) +
              " recipes parse clean";
  o.next =
      "ge texture bake <file.tex> --ref=\"seed=1\", then look at it in make lab "
      "LAB=texgen-specimens";
  return o;
}

Outcome run_texture_bake(const std::filesystem::path& repo, const std::string& file,
                         const std::string& ref_args, const std::string& size,
                         const std::string& out) {
  Outcome o;
  const std::filesystem::path p = std::filesystem::absolute(repo / file);
  texgen::ParseResult r = texgen::load_recipe(p.string());
  if (!r.ok()) {
    for (const texgen::Diagnostic& d : r.errors)
      o.findings.push_back({Severity::error, d.message,
                            rel_path(repo, p) + (d.line ? ":" + std::to_string(d.line) : ""),
                            "ge texture check " + file});
    return o;
  }
  if (!size.empty()) {
    const texgen::ParseResult sized =
        texgen::parse_recipe("texture t\nsize " + size + "\nlayer fill\n");
    if (!sized.ok()) {
      o.findings.push_back({Severity::error, "--size: " + sized.errors[0].message, "",
                            "pass a power of two such as --size=512"});
      return o;
    }
    r.recipe.size = sized.recipe.size;
  }
  std::vector<texgen::Diagnostic> errors;
  const texgen::TextureRef ref = texgen::parse_ref(r.recipe.name + " " + ref_args, errors);
  texgen::resolve_params(r.recipe, ref, errors);
  for (const texgen::Diagnostic& d : errors)
    o.findings.push_back({Severity::error, d.message, "", "fix --ref (\"seed=<n> <param>=<v>\")"});
  if (!errors.empty()) return o;
  const std::filesystem::path dir =
      std::filesystem::absolute(repo / (out.empty() ? "build/textures" : out));
  const auto t0 = std::chrono::steady_clock::now();
  const texgen::DiskResult d = texgen::bake_to_disk(r.recipe, ref, dir.string());
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  for (const texgen::Diagnostic& e : d.errors)
    o.findings.push_back({Severity::error, e.message, "", "check --out"});
  std::vector<std::string> files;
  for (const std::string& f : d.files) {
    files.push_back(rel_path(repo, f));
    if (!d.reused) o.written.push_back(rel_path(repo, f));
  }
  o.result = json_obj({{"ref", json_str(texgen::ref_text(ref))},
                       {"key", json_str(texgen::bake_key(r.recipe, ref))},
                       {"reused", json_bool(d.reused)},
                       {"files", json_strs(files)}});
  o.summary = texgen::ref_text(ref) +
              (d.reused ? ": already baked, reused "
                        : ": baked in " + core::num::to_fixed(ms, 1) + " ms, ") +
              std::to_string(files.size()) + " files";
  o.next = "load the maps from " + rel_path(repo, dir) +
           "; look at the recipe with make lab LAB=texgen-specimens";
  return o;
}

}  // namespace ge::cli
