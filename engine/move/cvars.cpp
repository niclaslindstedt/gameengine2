// The knobs bound as cvars: one spec per row of the knob table.
//
// ge:summary bind_knobs and apply_cfg: every movement knob a cvar bound to its MoveSpec field
// ge:keywords move cvars bind_knobs apply_cfg console
// ge:skill move console
// ge:test tests/move_cvars_test.cpp
#include "move/cvars.hpp"

#include <filesystem>
#include <string>

#include "console/console.hpp"
#include "move/knobs.hpp"

namespace ge::move {

int bind_knobs(console::Cvars& cvars, MoveSpec& spec, std::string_view prefix, uint32_t flags) {
  int n = 0;
  for (const Knob& k : knobs()) {
    const std::string name = std::string(prefix) + k.name;
    const std::string fallback = console::number_text(knob_value(spec, k));
    std::string help = k.help;
    if (k.unit[0] != '\0') help += std::string(", ") + k.unit;
    console::CvarSpec cs{.name = name, .fallback = fallback, .flags = flags, .help = help};
    cs.min = k.min;
    cs.max = k.max;
    switch (k.kind) {
      case KnobKind::number:
        cs.kind = console::CvarKind::number;
        cvars.bind(cs, number_field(spec, k));
        break;
      case KnobKind::flag:
        cs.kind = console::CvarKind::boolean;
        cvars.bind(cs, flag_field(spec, k));
        break;
      case KnobKind::count:
        cs.kind = console::CvarKind::integer;
        cvars.bind(cs, count_field(spec, k));
        break;
    }
    n++;
  }
  return n;
}

bool apply_cfg(MoveSpec& spec, std::string_view path, std::string& error, std::string_view prefix) {
  console::Console c(true);
  c.set_sink([](std::string_view) {});
  bind_knobs(c.cvars(), spec, prefix);
  const std::filesystem::path file(path);
  c.set_search_path({file.has_parent_path() ? file.parent_path() : std::filesystem::path(".")});
  c.exec_file(file.filename().string());
  for (int i = 0; i < console::kMaxFrames && c.pending(); i++) c.run();
  error.clear();
  for (const console::Problem& p : c.problems()) {
    if (!error.empty()) error += "\n";
    if (!p.where.file.empty()) error += p.where.file + ":" + std::to_string(p.where.line) + ": ";
    error += p.what;
    if (!p.next.empty()) error += " (next: " + p.next + ")";
  }
  return error.empty();
}

}  // namespace ge::move
