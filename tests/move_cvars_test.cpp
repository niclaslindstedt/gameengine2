// The movement knobs as cvars (engine/move/cvars.hpp), held to M28b's promises: every knob is a
// cvar under its owner prefix with the knob's kind, range and unit; binding moves no bit of any
// preset's row; a cfg retunes a run to the same digest as `--set` with the same values; a
// client's own set of a server's knob is refused while the server's info reaches the client's
// row; a cfg's mistakes come back with their file and line.
//
// ge:summary Tests: the movement knobs bound as cvars; cfg against --set; the server's tier
// ge:keywords test move movement cvars bind_knobs apply_cfg cfg server-info sv_gravity tier
// ge:skill move console testing
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.hpp"
#include "console/console.hpp"
#include "move/cvars.hpp"
#include "move/knobs.hpp"
#include "move/scenarios.hpp"

namespace con = ge::console;
namespace mv = ge::move;

namespace {

bool same_bits(const mv::MoveSpec& a, const mv::MoveSpec& b) {
  for (const mv::Knob& k : mv::knobs()) {
    const double x = mv::knob_value(a, k), y = mv::knob_value(b, k);
    if (std::bit_cast<uint64_t>(x) != std::bit_cast<uint64_t>(y)) return false;
  }
  return true;
}

mv::MoveRun run(const char* id, const mv::MoveSpec& spec) {
  return mv::run_move_scenario(*mv::find_move_scenario(id), spec, 120);
}

std::filesystem::path write_cfg(const char* name, const char* text) {
  const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
  std::ofstream(p, std::ios::binary) << text;
  return p;
}

}  // namespace

TEST(every_knob_is_a_server_cvar_with_its_kind_range_and_unit) {
  mv::MoveSpec spec = *mv::find_preset("classic_arena");
  con::Cvars v(true);
  const int n = mv::bind_knobs(v, spec);
  CHECK(n == static_cast<int>(mv::knobs().size()));
  for (const mv::Knob& k : mv::knobs()) {
    const con::Cvar* c = v.find(std::string("sv_") + k.name);
    CHECK_MSG(c != nullptr, std::string("no sv_") + k.name);
    if (c == nullptr) continue;
    CHECK((c->flags & con::kServer) != 0 && (c->flags & con::kServerInfo) != 0);
    CHECK(c->min == k.min && c->max == k.max);
    const con::CvarKind want = k.kind == mv::KnobKind::flag    ? con::CvarKind::boolean
                               : k.kind == mv::KnobKind::count ? con::CvarKind::integer
                                                               : con::CvarKind::number;
    CHECK(c->kind == want);
    if (k.unit[0] != '\0') {
      const std::string tail = std::string(", ") + k.unit;
      CHECK_MSG(c->help.size() > tail.size() &&
                    c->help.compare(c->help.size() - tail.size(), tail.size(), tail) == 0,
                c->name + "'s help does not end with its unit: " + c->help);
    }
  }
  CHECK(v.find("sv_gravity")->value == "800" && v.find("sv_airaccelerate")->value == "0.7");
}

TEST(binding_moves_no_bit_of_any_preset_and_reset_returns_to_it) {
  for (const char* const* p = mv::preset_names(); *p != nullptr; p++) {
    const mv::MoveSpec row = *mv::find_preset(*p);
    mv::MoveSpec spec = row;
    con::Cvars v(true);
    mv::bind_knobs(v, spec, "cl_", con::kArchive);
    CHECK_MSG(same_bits(spec, row), std::string("binding moved ") + *p);
    v.set("cl_gravity", "123", con::Source::local);
    CHECK(spec.gravity == 123);
    v.reset(*v.find("cl_gravity"), con::Source::local);
    CHECK_MSG(same_bits(spec, row), std::string("reset did not return ") + *p);
  }
}

TEST(a_cfg_retunes_a_run_to_the_same_digest_as_set) {
  const mv::MoveSpec row = *mv::find_preset("classic_arena");
  mv::MoveSpec by_set = row;
  std::string error;
  CHECK(mv::apply_knobs(by_set, "gravity=600,airaccelerate=10,jumphold=1,airjumps=1", error));
  const std::filesystem::path cfg =
      write_cfg("move_cvars_test.cfg",
                "// a server's row\nsv_gravity 600\nsv_airaccelerate \"10\"\nset sv_jumphold 1\n"
                "sv_airjumps 1\n");
  mv::MoveSpec by_cfg = row;
  CHECK_MSG(mv::apply_cfg(by_cfg, cfg.string(), error), error);
  CHECK(same_bits(by_cfg, by_set));
  for (const char* id : {"bhop-straight", "strafe-jump", "double-jump"}) {
    CHECK_MSG(run(id, by_cfg).digest == run(id, by_set).digest, id);
  }
  CHECK(run("bhop-straight", by_cfg).digest != run("bhop-straight", row).digest);
  std::filesystem::remove(cfg);
}

TEST(a_client_cannot_set_a_server_knob_but_follows_the_server_info) {
  mv::MoveSpec server_spec = *mv::find_preset("classic_arena");
  mv::MoveSpec client_spec = server_spec;
  con::Cvars server(true), client(false);
  mv::bind_knobs(server, server_spec);
  mv::bind_knobs(client, client_spec);
  CHECK(client.set("sv_gravity", "100", con::Source::local) == con::SetResult::server);
  CHECK(client_spec.gravity == 800);
  CHECK(server.set("sv_gravity", "650", con::Source::local) == con::SetResult::changed);
  CHECK(server.set("sv_airaccelerate", "12", con::Source::local) == con::SetResult::changed);
  const std::string info = server.info_string(con::kServerInfo);
  CHECK(info.find("\\sv_gravity\\650") != std::string::npos);
  CHECK(client.apply_info(info, con::Source::server) >= 2);
  CHECK(client_spec.gravity == 650 && client_spec.air_accelerate == 12);
  CHECK(same_bits(client_spec, server_spec));
}

TEST(a_cfg_mistake_names_its_file_and_line) {
  const std::filesystem::path cfg =
      write_cfg("move_cvars_bad.cfg", "sv_gravity 700\nsv_gravty 500\n");
  mv::MoveSpec spec = *mv::find_preset("classic_arena");
  std::string error;
  CHECK(!mv::apply_cfg(spec, cfg.string(), error));
  CHECK_MSG(error.find("move_cvars_bad.cfg:2") != std::string::npos, error);
  CHECK_MSG(error.find("sv_gravity") != std::string::npos, error);  // the nearest name
  CHECK(spec.gravity == 700);
  CHECK(!mv::apply_cfg(spec, "no/such/file.cfg", error) && !error.empty());
  std::filesystem::remove(cfg);
}
