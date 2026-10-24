// `ge save`'s body (save.hpp): a save file opened through engine/save and printed as a table,
// findings and one JSON result.
//
// ge:summary ge save's body: inspect a save file through the engine's own reader
// ge:keywords cli save inspect run_save run_save_inspect chunk table
// ge:skill agent-interface save
// ge:test tests/cli_save_test.cpp
#include "cli/save.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

#include "cli/json.hpp"
#include "core/num_text.hpp"
#include "save/file.hpp"

namespace ge::cli {

namespace {

const char* kNext =
    "a damaged save: load another (a slot's .bak, an older autosave); a newer one: a newer "
    "build of the game (skill: save)";

std::string hex(uint64_t v) {
  static const char* k = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = k[v & 15];
  return s;
}

std::string table(const std::vector<std::vector<std::string>>& rows) {
  std::vector<size_t> width;
  for (const auto& r : rows) {
    if (width.size() < r.size()) width.resize(r.size(), 0);
    for (size_t i = 0; i < r.size(); i++) width[i] = std::max(width[i], r[i].size());
  }
  std::string out;
  for (const auto& r : rows) {
    std::string line = " ";
    for (size_t i = 0; i < r.size(); i++) {
      line += " " + r[i];
      if (i + 1 < r.size()) line += std::string(width[i] - r[i].size() + 1, ' ');
    }
    out += line + "\n";
  }
  return out;
}

const char* kind_name(save::Kind k) {
  switch (k) {
    case save::Kind::run:
      return "run";
    case save::Kind::character:
      return "character";
  }
  return "the game's own";
}

}  // namespace

Outcome run_save_inspect(const std::string& file) {
  Outcome o;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    o.findings.push_back({Severity::error, "cannot read " + file, file,
                          "check the path; a store keeps slots as <dir>/<slot> (skill: save)"});
    return o;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
  save::FileReader r;
  if (!r.open(bytes, file)) {
    o.findings.push_back({Severity::error, r.problem(), file, kNext});
    return o;
  }
  const save::Header& h = r.header();
  const save::Meta& m = r.meta();
  o.text += "  " + file + ": a " + kind_name(m.kind) + " save of \"" + m.game +
            "\" (save version " + std::to_string(m.game_version) + ", format " +
            std::to_string(h.format) + "), " + std::to_string(bytes.size()) + " bytes, " +
            std::to_string(h.chunks) + " chunks\n";
  o.text += "  title \"" + m.title + "\", place \"" + m.place + "\", played " +
            core::num::to_fixed(m.play_s, 1) + " s, saved at " + std::to_string(m.saved_unix) +
            "\n";
  std::vector<std::vector<std::string>> rows = {
      {"tag", "instance", "version", "offset", "bytes", "checksum"}};
  std::vector<std::string> chunks;
  for (const save::ChunkInfo& c : r.chunks()) {
    rows.push_back({save::tag_name(c.tag), std::to_string(c.instance), std::to_string(c.version),
                    std::to_string(c.offset), std::to_string(c.stored), hex(c.checksum)});
    chunks.push_back(json_obj({{"tag", json_str(save::tag_name(c.tag))},
                               {"instance", json_num(c.instance)},
                               {"version", json_num(c.version)},
                               {"offset", json_num(static_cast<long long>(c.offset))},
                               {"bytes", json_num(static_cast<long long>(c.stored))},
                               {"checksum", json_str(hex(c.checksum))}}));
  }
  o.text += table(rows);
  o.result = json_obj({{"file", json_str(file)},
                       {"bytes", json_num(static_cast<long long>(bytes.size()))},
                       {"format", json_num(h.format)},
                       {"kind", json_str(kind_name(m.kind))},
                       {"game", json_str(m.game)},
                       {"game_version", json_num(m.game_version)},
                       {"title", json_str(m.title)},
                       {"place", json_str(m.place)},
                       {"play_s", core::num::to_string(m.play_s)},
                       {"saved_unix", json_num(static_cast<long long>(m.saved_unix))},
                       {"setup", json_str(m.setup)},
                       {"chunks", json_arr(chunks)}});
  o.summary = "every checksum matches: " + std::to_string(h.chunks) + " chunks";
  return o;
}

Outcome run_save(const Args& a) { return run_save_inspect(a.words[0]); }

}  // namespace ge::cli
