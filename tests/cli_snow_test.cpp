// `ge snow` (engine/cli/snow.hpp): a saved layer shown (its tiles, the samples pressed, the mass
// held, three maps written) and a column read at a point; a point off the layer, a malformed
// --at and a file that is no snapshot each come back as a finding that says what to do.
//
// ge:summary Test: ge snow show and ge snow column over a saved layer, and their errors
// ge:keywords test cli snow show column snapshot png findings
// ge:skill testing agent-interface snow
// ge:test tests/cli_snow_test.cpp
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "cli/finding.hpp"
#include "cli/snow.hpp"
#include "snow/press.hpp"
#include "snow/reference.hpp"
#include "snow/snapshot.hpp"

namespace fs = std::filesystem;
using namespace ge::cli;
namespace sn = ge::snow;

namespace {

fs::path saved_yard() {
  const fs::path dir = fs::temp_directory_path() / "ge_cli_snow_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  sn::LayerSpec spec;
  spec.region = {-8, -8, 8, 8};
  spec.cover.depth = 0.4;
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, spec);
  for (int i = 0; i < 12; i++) {
    sn::press(layer,
              sn::place(sn::reference_contact(2).imprint, i % 2 ? 0.12 : -0.12, -4 + 0.75 * i, 0),
              i);
  }
  const std::vector<uint8_t> bytes = sn::save(layer);
  const fs::path file = dir / "yard.snow";
  std::ofstream(file, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return file;
}

}  // namespace

TEST(a_saved_layer_is_shown_with_its_maps) {
  const fs::path file = saved_yard();
  const Outcome o = run_snow_show(file.string(), "");
  CHECK(count_of(o.findings, Severity::error) == 0);
  CHECK(o.written.size() == 3);
  for (const std::string& p : o.written) CHECK(fs::exists(p));
  CHECK(o.result.find("\"mass_held\":true") != std::string::npos);
  CHECK(o.text.find("pressed samples") != std::string::npos);
}

TEST(a_column_is_read_at_a_point) {
  const fs::path file = saved_yard();
  const Outcome o = run_snow_column(file.string(), "0.12,-3.25");
  CHECK(count_of(o.findings, Severity::error) == 0);
  CHECK(o.result.find("\"touched\":true") != std::string::npos);
}

TEST(a_bad_point_or_file_says_what_to_do) {
  const fs::path file = saved_yard();
  Outcome o = run_snow_column(file.string(), "40,0");
  CHECK(count_of(o.findings, Severity::error) == 1 &&
        o.findings[0].what.find("outside the layer") != std::string::npos);
  o = run_snow_column(file.string(), "nowhere");
  CHECK(o.exit == 2 && o.findings[0].next.find("--at=<x,z>") != std::string::npos);
  const fs::path junk = file.parent_path() / "junk.snow";
  std::ofstream(junk) << "not snow";
  o = run_snow_show(junk.string(), "");
  CHECK(count_of(o.findings, Severity::error) == 1 &&
        o.findings[0].what.find("GESNOW1") != std::string::npos);
}
