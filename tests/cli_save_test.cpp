// `ge save inspect` (engine/cli/save.hpp): a save file's chunks listed with their tags, versions
// and bytes, and a damaged file refused as one error finding naming the file.
//
// ge:summary Test: ge save inspect on a good, a damaged and a missing save
// ge:keywords test cli save inspect chunk damaged finding
// ge:skill testing agent-interface save
#include <filesystem>
#include <fstream>
#include <string>

#include "check.hpp"
#include "cli/save.hpp"
#include "save/file.hpp"

namespace fs = std::filesystem;
using namespace ge::cli;

namespace {

fs::path write_save(const fs::path& path, bool damage) {
  std::vector<uint8_t> bytes;
  ge::save::Meta m;
  m.game = "demo";
  m.title = "Slot one";
  ge::save::FileWriter w(bytes, m);
  w.put(ge::save::tag("BODY"), 0, 3, std::vector<double>{1, 2, 3});
  w.finish();
  if (damage) bytes[70] ^= 1;
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return path;
}

}  // namespace

TEST(save_inspect_lists_every_chunk) {
  const fs::path dir = fs::temp_directory_path() / "ge_cli_save_test";
  fs::create_directories(dir);
  const Outcome o = run_save_inspect(write_save(dir / "good.save", false).string());
  CHECK(o.findings.empty());
  CHECK(o.text.find("BODY") != std::string::npos && o.text.find("META") != std::string::npos);
  CHECK(o.result.find("\"game\":\"demo\"") != std::string::npos);
  CHECK(o.result.find("\"version\":3") != std::string::npos);
}

TEST(save_inspect_refuses_a_damaged_or_missing_file) {
  const fs::path dir = fs::temp_directory_path() / "ge_cli_save_test";
  fs::create_directories(dir);
  const Outcome bad = run_save_inspect(write_save(dir / "bad.save", true).string());
  CHECK(bad.findings.size() == 1 && bad.findings[0].severity == Severity::error);
  CHECK(bad.findings[0].what.find("damaged") != std::string::npos);
  const Outcome none = run_save_inspect((dir / "none.save").string());
  CHECK(none.findings.size() == 1);
}
