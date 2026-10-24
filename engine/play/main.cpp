// The `play` command line (main.hpp).
//
// ge:summary play_main: parses the play command line and prints each command's JSON answer
// ge:keywords play main command-line describe sim replay state diverge bench shot png json why
// ge:skill agent-interface
// ge:test tests/play_session_test.cpp
#include "play/main.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/num_text.hpp"
#include "lab/png.hpp"
#include "play/bench.hpp"
#include "play/console_cmds.hpp"
#include "play/human.hpp"
#include "play/record.hpp"
#include "play/session.hpp"
#include "play/why.hpp"
#include "prof/memory.hpp"

namespace ge::play {

namespace {

namespace fs = std::filesystem;

// The most flips `why --flips` lists; the rest are counted.
constexpr size_t kMostFlips = 200;

struct Line {
  std::string op;
  std::vector<std::string> words;
  std::map<std::string, std::string> flags;
};

int problem(std::ostream& out, int code, std::string_view what, std::string_view where,
            std::string_view next) {
  out << "{\"error\":" << quote(what) << ",\"where\":" << quote(where)
      << ",\"next\":" << quote(next) << "}\n";
  return code;
}

bool read_log(const std::string& path, InputLog& log, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot read " + path;
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  auto parsed = parse_inputs(ss.str(), error);
  if (!parsed) {
    error = path + ": " + error;
    return false;
  }
  log = std::move(*parsed);
  return true;
}

std::string views_json(const GameSpec& spec) {
  std::string out;
  for (const View& v : spec.views) {
    out += std::string(out.empty() ? "" : ",") + "{\"name\":" + quote(v.name) +
           ",\"help\":" + quote(v.help) + "}";
  }
  return out;
}

// The renderer's views and modes (`--gpu`), or null when the executable has none.
std::string gpu_json() {
  const RendererSpec* r = registered_renderer();
  if (!r) return "null";
  auto list = [](std::span<const View> vs) {
    std::string out;
    for (const View& v : vs) {
      out += std::string(out.empty() ? "" : ",") + "{\"name\":" + quote(v.name) +
             ",\"help\":" + quote(v.help) + "}";
    }
    return out;
  };
  return "{\"views\":[" + list(r->views) + "],\"modes\":[" + list(r->modes) +
         "],\"width\":" + std::to_string(r->width) + ",\"height\":" + std::to_string(r->height) +
         "}";
}

std::string describe(const GameSpec& spec) {
  using core::num::to_string;
  std::string axes;
  for (const Axis& a : spec.axes) {
    const char* kind = a.kind == AxisKind::signed_unit ? "signed"
                       : a.kind == AxisKind::lever     ? "lever"
                       : a.kind == AxisKind::toggle    ? "toggle"
                                                       : "press";
    axes += std::string(axes.empty() ? "" : ",") + "{\"name\":" + quote(a.name) +
            ",\"kind\":" + quote(kind) + ",\"help\":" + quote(a.help) + "}";
  }
  std::string options;
  for (const Option& o : spec.options) {
    options += std::string(options.empty() ? "" : ",") + "{\"name\":" + quote(o.name) +
               ",\"default\":" + quote(o.fallback) + ",\"help\":" + quote(o.help) + "}";
  }
  return "{\"game\":" + quote(spec.name) + ",\"summary\":" + quote(spec.summary) +
         ",\"hz\":" + to_string(spec.hz) + ",\"most_seconds\":" + to_string(spec.most_seconds) +
         ",\"axes\":[" + axes + "],\"options\":[" + options + "],\"views\":[" + views_json(spec) +
         "],\"gpu\":" + gpu_json() + "}";
}

std::string played_json(const Played& p) {
  return "\"steps\":" + std::to_string(p.steps) + ",\"over\":" + (p.over ? "true" : "false") +
         ",\"digest\":" + quote(hex64(p.digest)) + ",\"report\":" + p.report;
}

// The options among the flags: every flag the command does not take itself.
std::map<std::string, std::string> options_of(const Line& l,
                                              std::initializer_list<const char*> own) {
  std::map<std::string, std::string> out;
  for (const auto& [k, v] : l.flags) {
    bool mine = false;
    for (const char* o : own) mine = mine || k == o;
    if (!mine) out[k] = v;
  }
  return out;
}

bool number_flag(const Line& l, const std::string& name, double fallback, double& out) {
  const auto it = l.flags.find(name);
  if (it == l.flags.end()) {
    out = fallback;
    return true;
  }
  char* end = nullptr;
  out = std::strtod(it->second.c_str(), &end);
  return !it->second.empty() && end == it->second.c_str() + it->second.size();
}

int protocol(const GameSpec& spec, const Line& l, std::istream& in, std::ostream& out) {
  double seed = 0;
  if (!number_flag(l, "seed", 1, seed)) {
    return problem(out, 2, "--seed is not a number", "the command line", "pass --seed=38");
  }
  Setup setup;
  std::string error;
  if (!make_setup(spec, seed, options_of(l, {"seed", "inputs", "stdio"}), setup, error)) {
    return problem(out, 2, error, "the command line", "play describe lists the options");
  }
  Session s(spec, setup);
  if (!s.ok()) return problem(out, 1, s.error(), "the setup", "fix the options and run again");
  out << s.hello() << "\n" << std::flush;
  std::string line;
  while (!s.ended() && std::getline(in, line)) {
    if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
    out << s.handle(line) << "\n" << std::flush;
  }
  if (const auto it = l.flags.find("inputs"); it != l.flags.end()) {
    std::ofstream f(it->second, std::ios::binary);
    f << to_text(s.inputs());
    if (!f) return problem(out, 1, "cannot write " + it->second, it->second, "check the path");
  }
  return 0;
}

// --seeds=A,B as numbers (1 when absent); false with `bad` the word that is not one.
bool seeds_of(const Line& l, std::vector<double>& seeds, std::string& bad) {
  const auto it = l.flags.find("seeds");
  std::stringstream list(it == l.flags.end() ? "1" : it->second);
  std::string word;
  while (std::getline(list, word, ',')) {
    char* end = nullptr;
    const double v = std::strtod(word.c_str(), &end);
    if (word.empty() || end != word.c_str() + word.size()) {
      bad = word;
      return false;
    }
    seeds.push_back(v);
  }
  return true;
}

int sim(const GameSpec& spec, const Line& l, std::ostream& out) {
  std::vector<double> seeds;
  std::string bad;
  if (!seeds_of(l, seeds, bad)) {
    return problem(out, 2, "--seeds holds `" + bad + "`, not a number", "the command line",
                   "pass --seeds=38,7");
  }
  const auto out_it = l.flags.find("out");
  const fs::path dir = out_it == l.flags.end() ? fs::path("runs") : fs::path(out_it->second);
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string runs;
  for (const double seed : seeds) {
    Setup setup;
    std::string error;
    if (!make_setup(spec, seed, options_of(l, {"seeds", "out", "human"}), setup, error)) {
      return problem(out, 2, error, "the command line", "play describe lists the options");
    }
    const auto p = pilot_run(spec, setup, error);
    if (!p) return problem(out, 1, error, "seed " + core::num::to_string(seed), "fix the game");
    const fs::path file =
        dir / (std::string(spec.name) + "-" + core::num::to_string(seed) + ".inputs");
    std::ofstream f(file, std::ios::binary);
    f << to_text(p->log);
    if (!f) return problem(out, 1, "cannot write " + file.string(), file.string(), "check --out");
    std::string human;
    if (l.flags.count("human")) {
      const auto w = why_replay(spec, p->log, 0, error);
      if (!w) return problem(out, 1, error, file.string(), "--human needs a game whose Run traces");
      human = ",\"human\":" + human_json(human_report(spec, p->log, w->why));
    }
    runs += std::string(runs.empty() ? "" : ",") + "{\"seed\":" + core::num::to_string(seed) +
            ",\"inputs\":" + quote(file.generic_string()) + "," + played_json(*p) + human + "}";
  }
  out << "{\"game\":" << quote(spec.name) << ",\"runs\":[" << runs << "]}\n";
  return 0;
}

int replay_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() != 1) {
    return problem(out, 2, "replay takes one input log", "the command line",
                   "play replay runs/x.inputs");
  }
  InputLog log;
  std::string error;
  if (!read_log(l.words[0], log, error))
    return problem(out, 1, error, l.words[0], "pass an input log");
  const auto p = replay(spec, log, error);
  if (!p) return problem(out, 1, error, l.words[0], "fix the log's header or the options");
  const bool matches = !log.digest || *log.digest == p->digest;
  out << "{\"inputs\":" << quote(l.words[0])
      << ",\"recorded\":" << (log.digest ? quote(hex64(*log.digest)) : "null")
      << ",\"matches\":" << (matches ? "true" : "false") << "," << played_json(*p) << "}\n";
  return matches ? 0 : 1;
}

int state_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() != 1) {
    return problem(out, 2, "state takes one input log", "the command line",
                   "play state runs/x.inputs --step=600");
  }
  InputLog log;
  std::string error;
  if (!read_log(l.words[0], log, error))
    return problem(out, 1, error, l.words[0], "pass an input log");
  double step = 0;
  if (!number_flag(l, "step", static_cast<double>(log.steps()), step)) {
    return problem(out, 2, "--step is not a number", "the command line", "pass --step=600");
  }
  const auto st = state_at(spec, log, static_cast<int64_t>(step), error);
  if (!st) return problem(out, 1, error, l.words[0], "pass a --step inside the log");
  out << "{\"inputs\":" << quote(l.words[0]) << ",\"step\":" << static_cast<int64_t>(step)
      << ",\"state\":" << *st << "}\n";
  return 0;
}

int diverge_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() != 2) {
    return problem(out, 2, "diverge takes two input logs", "the command line",
                   "play diverge runs/a.inputs runs/b.inputs");
  }
  InputLog a, b;
  std::string error;
  if (!read_log(l.words[0], a, error))
    return problem(out, 1, error, l.words[0], "pass an input log");
  if (!read_log(l.words[1], b, error))
    return problem(out, 1, error, l.words[1], "pass an input log");
  const auto d = diverge(spec, a, b, error);
  if (!d) return problem(out, 1, error, "the logs", "fix the logs' headers");
  std::string fields;
  for (const FieldDiff& f : d->fields) {
    fields += std::string(fields.empty() ? "" : ",") + "{\"key\":" + quote(f.key) +
              ",\"a\":" + quote(f.a) + ",\"b\":" + quote(f.b) + "}";
  }
  out << "{\"same\":" << (d->same ? "true" : "false")
      << ",\"setup_differs\":" << (d->setup_differs ? "true" : "false")
      << ",\"input_step\":" << d->input_step << ",\"state_step\":" << d->state_step
      << ",\"fields\":[" << fields << "],\"more_fields\":" << d->more_fields
      << ",\"steps_a\":" << d->steps_a << ",\"steps_b\":" << d->steps_b
      << ",\"digest_a\":" << quote(hex64(d->digest_a))
      << ",\"digest_b\":" << quote(hex64(d->digest_b)) << "}\n";
  return d->same ? 0 : 1;
}

// --size=WIDTHxHEIGHT into `w` and `h` (left alone without the flag); false when it does not read.
bool size_flag(const Line& l, int& w, int& h) {
  const auto it = l.flags.find("size");
  if (it == l.flags.end()) return true;
  const size_t x = it->second.find('x');
  char* e1 = nullptr;
  char* e2 = nullptr;
  const std::string ws = it->second.substr(0, x),
                    hs = x == std::string::npos ? "" : it->second.substr(x + 1);
  w = static_cast<int>(std::strtol(ws.c_str(), &e1, 10));
  h = static_cast<int>(std::strtol(hs.c_str(), &e2, 10));
  return !ws.empty() && !hs.empty() && !*e1 && !*e2 && w > 0 && h > 0 && w <= 7680 && h <= 4320;
}

// The lens the flags ask for: --view, and --gpu with --mode and --size.
LensAsk lens_ask_of(const Line& l) {
  LensAsk ask;
  if (const auto it = l.flags.find("view"); it != l.flags.end()) ask.view = it->second;
  if (const auto it = l.flags.find("gpu"); it != l.flags.end()) ask.gpu = it->second != "0";
  if (const auto it = l.flags.find("mode"); it != l.flags.end()) ask.mode = it->second;
  return ask;
}

// A lens's name as a file names it: "map", or "chase-depth" with --gpu (the defaults filled).
std::string lens_file_name(const GameSpec& spec, const LensAsk& ask) {
  if (!ask.gpu) {
    return ask.view.empty() && !spec.views.empty() ? spec.views[0].name : ask.view;
  }
  const RendererSpec* r = registered_renderer();
  std::string view = ask.view, mode = ask.mode;
  if (r && view.empty() && !r->views.empty()) view = r->views[0].name;
  if (r && mode.empty() && !r->modes.empty()) mode = r->modes[0].name;
  return view + "-" + mode;
}

// `why`: a log replayed with the decision trace on; one agent's decisions round a step, with
// --flips the changes of choice over the whole run, with --timeline its series file in --out.
int why_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() != 1) {
    return problem(out, 2, "why takes one input log", "the command line",
                   "play why runs/x.inputs --agent=0 --step=600");
  }
  InputLog log;
  std::string error;
  if (!read_log(l.words[0], log, error))
    return problem(out, 1, error, l.words[0], "pass an input log");
  double agent = 0, step = 0, around = 0, close = 0;
  const double last = static_cast<double>(log.steps() > 0 ? log.steps() - 1 : 0);
  if (!number_flag(l, "agent", 0, agent) || !number_flag(l, "step", last, step) ||
      !number_flag(l, "around", 0, around) || !number_flag(l, "close", 0, close) || agent < 0 ||
      around < 0) {
    return problem(out, 2, "--agent, --step, --around and --close take numbers, none negative",
                   "the command line", "pass --agent=0 --step=600 --around=12");
  }
  const auto w = why_replay(spec, log, static_cast<int32_t>(agent), error);
  if (!w) return problem(out, 1, error, l.words[0], "trace the game's Run (play/game.hpp)");
  const auto it = l.flags.find("decision");
  const std::string name = it == l.flags.end() ? "" : it->second;
  const auto from = static_cast<int64_t>(step - around);
  const auto to = static_cast<int64_t>(step + around);
  std::string decisions, text;
  for (const ai::Decision* d : decisions_between(w->why, from, to, name)) {
    decisions += std::string(decisions.empty() ? "" : ",") + ai::decision_json(w->why, *d);
    text += ai::decision_text(w->why, *d);
  }
  int64_t agree = 0, first_disagree = -1;
  for (size_t i = 0; i < w->agrees.size(); i++) {
    agree += w->agrees[i];
    if (!w->agrees[i] && first_disagree < 0) first_disagree = static_cast<int64_t>(i);
  }
  std::ostringstream o;
  o << "{\"inputs\":" << quote(l.words[0]) << ",\"agent\":" << static_cast<int32_t>(agent)
    << ",\"steps\":" << w->steps << ",\"digest\":" << quote(hex64(w->digest))
    << ",\"recorded\":" << w->why.decisions().size()
    << ",\"pilot_asked\":" << (w->asked_pilot ? "true" : "false") << ",\"pilot_agrees\":" << agree
    << ",\"first_disagree\":" << first_disagree << ",\"from\":" << from << ",\"to\":" << to
    << ",\"decisions\":[" << decisions << "]";
  if (l.flags.count("flips")) {
    const std::vector<Flip> flips = why_flips(w->why, static_cast<int32_t>(agent), close);
    std::string rows;
    for (size_t i = 0; i < flips.size() && i < kMostFlips; i++) {
      const Flip& f = flips[i];
      text +=
          "step " + std::to_string(f.step) + " " + f.decision + ": " +
          (f.from == std::string_view(f.to) ? std::string("close call on ") + f.to
                                            : std::string(*f.from ? f.from : "-") + " -> " + f.to) +
          (std::isfinite(f.margin) ? " by " + core::num::to_string(f.margin) : "") + "\n";
      rows += std::string(rows.empty() ? "" : ",") + "{\"step\":" + std::to_string(f.step) +
              ",\"decision\":" + quote(f.decision) + ",\"from\":" + quote(f.from) +
              ",\"to\":" + quote(f.to) + ",\"margin\":" +
              (std::isfinite(f.margin) ? core::num::to_string(f.margin) : std::string("null")) +
              "}";
    }
    o << ",\"flips\":[" << rows
      << "],\"more_flips\":" << (flips.size() > kMostFlips ? flips.size() - kMostFlips : 0);
  }
  if (l.flags.count("timeline")) {
    const auto out_it = l.flags.find("out");
    const fs::path dir = out_it == l.flags.end() ? fs::path("runs") : fs::path(out_it->second);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / (fs::path(l.words[0]).stem().string() + "-why-" +
                                 std::to_string(static_cast<int32_t>(agent)) + ".tsv");
    std::ofstream f(file, std::ios::binary);
    f << lab::write_series(why_timeline(w->why, static_cast<int32_t>(agent), spec.hz));
    if (!f) return problem(out, 1, "cannot write " + file.string(), file.string(), "check --out");
    o << ",\"timeline\":" << quote(file.generic_string());
  }
  out << o.str() << ",\"text\":" << quote(text) << "}\n";
  return 0;
}

// `shot`: a log played to --step (its end by default), or the pilot's run of --seed, and the
// view drawn, framed and written as a PNG.
int shot_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() > 1) {
    return problem(out, 2, "shot takes at most one input log", "the command line",
                   "play shot runs/x.inputs --step=600 --view=map");
  }
  InputLog log;
  std::string error;
  if (l.words.size() == 1) {
    if (!read_log(l.words[0], log, error))
      return problem(out, 1, error, l.words[0], "pass an input log");
  } else {
    double seed = 0;
    if (!number_flag(l, "seed", 1, seed)) {
      return problem(out, 2, "--seed is not a number", "the command line", "pass --seed=38");
    }
    Setup setup;
    if (!make_setup(spec, seed,
                    options_of(l, {"seed", "step", "view", "out", "gpu", "mode", "size"}), setup,
                    error)) {
      return problem(out, 2, error, "the command line", "play describe lists the options");
    }
    auto p = pilot_run(spec, setup, error);
    if (!p) return problem(out, 1, error, "seed " + core::num::to_string(seed), "fix the game");
    log = std::move(p->log);
  }
  double step = 0;
  if (!number_flag(l, "step", static_cast<double>(log.steps()), step)) {
    return problem(out, 2, "--step is not a number", "the command line", "pass --step=600");
  }
  LensAsk ask = lens_ask_of(l);
  if (!size_flag(l, ask.width, ask.height)) {
    return problem(out, 2, "--size is not WIDTHxHEIGHT (at most 7680x4320)", "the command line",
                   "pass --size=1280x720");
  }
  const auto still = shot_at(spec, log, static_cast<int64_t>(step), ask, error);
  if (!still) {
    return problem(out, 1, error, l.words.empty() ? "the pilot's run" : l.words[0],
                   ask.gpu ? "play describe lists the gpu views and modes; pass a --step inside "
                             "the log"
                           : "play describe lists the views; pass a --step inside the log");
  }
  const auto out_it = l.flags.find("out");
  const fs::path dir = out_it == l.flags.end() ? fs::path("runs") : fs::path(out_it->second);
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path file =
      dir / (std::string(spec.name) + "-" + core::num::to_string(log.seed) + "-" +
             lens_file_name(spec, ask) + "-" + std::to_string(still->step) + ".png");
  if (!lab::write_png(file.string(), still->picture)) {
    return problem(out, 1, "cannot write " + file.generic_string(), file.generic_string(),
                   "check --out");
  }
  out << "{\"png\":" << quote(file.generic_string()) << ",\"view\":" << quote(still->view)
      << ",\"step\":" << still->step << ",\"digest\":" << quote(hex64(still->digest))
      << ",\"caption\":" << quote(still->caption) << ",\"width\":" << still->picture.width
      << ",\"height\":" << still->picture.height << ",\"gpu\":" << (ask.gpu ? "true" : "false")
      << ",\"views\":[" << lens_views_json(spec, ask.gpu) << "]}\n";
  return 0;
}

// A step flag: a step, or seconds on the run's clock with an `s` ("12.5s").
bool step_flag(const Line& l, const std::string& name, double hz, int64_t fallback, int64_t& out) {
  const auto it = l.flags.find(name);
  if (it == l.flags.end()) {
    out = fallback;
    return true;
  }
  std::string text = it->second;
  const bool seconds = !text.empty() && text.back() == 's';
  if (seconds) text.pop_back();
  char* end = nullptr;
  const double v = std::strtod(text.c_str(), &end);
  if (text.empty() || end != text.c_str() + text.size() || v < 0) return false;
  out = static_cast<int64_t>(std::llround(seconds ? v * hz : v));
  return true;
}

// `record`: a log (or the pilot's run of --seed) played back and a view drawn at a frame rate,
// encoded as a video by ffmpeg (or PNG frames), with a contact sheet beside it (record.hpp).
int record_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  if (l.words.size() > 1) {
    return problem(out, 2, "record takes at most one input log", "the command line",
                   "play record runs/x.inputs --view=map --fps=30");
  }
  static const std::initializer_list<const char*> kOwn = {
      "seed", "view",    "from",           "to",     "fps",     "speed", "size", "format", "codec",
      "crf",  "bitrate", "encoder-preset", "thumbs", "columns", "band",  "out",  "gpu",    "mode",
      "sound"};
  InputLog log;
  std::string error;
  if (l.words.size() == 1) {
    if (!read_log(l.words[0], log, error))
      return problem(out, 1, error, l.words[0], "pass an input log");
  } else {
    double seed = 0;
    if (!number_flag(l, "seed", 1, seed)) {
      return problem(out, 2, "--seed is not a number", "the command line", "pass --seed=38");
    }
    Setup setup;
    if (!make_setup(spec, seed, options_of(l, kOwn), setup, error)) {
      return problem(out, 2, error, "the command line", "play describe lists the options");
    }
    auto p = pilot_run(spec, setup, error);
    if (!p) return problem(out, 1, error, "seed " + core::num::to_string(seed), "fix the game");
    log = std::move(p->log);
  }
  RecordOptions o;
  double crf = -1, thumbs = 24, columns = 6;
  if (!step_flag(l, "from", spec.hz, 0, o.from) || !step_flag(l, "to", spec.hz, -1, o.to)) {
    return problem(out, 2, "--from and --to take a step or seconds (\"12.5s\")", "the command line",
                   "pass --from=10s --to=40s");
  }
  if (!number_flag(l, "fps", 30, o.fps) || !number_flag(l, "speed", 1, o.speed) ||
      !number_flag(l, "crf", -1, crf) || !number_flag(l, "thumbs", 24, thumbs) ||
      !number_flag(l, "columns", 6, columns) || !(o.fps > 0) || !(o.speed > 0)) {
    return problem(out, 2, "--fps, --speed, --crf, --thumbs or --columns is not a number over 0",
                   "the command line", "pass --fps=30 --speed=1");
  }
  o.encoding.crf = static_cast<int>(crf);
  o.thumbs = static_cast<int>(thumbs);
  o.columns = static_cast<int>(columns);
  if (!size_flag(l, o.width, o.height)) {
    return problem(out, 2, "--size is not WIDTHxHEIGHT (at most 7680x4320)", "the command line",
                   "pass --size=1280x720");
  }
  const LensAsk ask = lens_ask_of(l);
  o.gpu = ask.gpu;
  o.mode = ask.mode;
  auto text_flag = [&](const char* name, std::string& into) {
    if (const auto it = l.flags.find(name); it != l.flags.end()) into = it->second;
  };
  text_flag("view", o.view);
  text_flag("format", o.encoding.format);
  text_flag("codec", o.encoding.codec);
  text_flag("bitrate", o.encoding.bitrate);
  text_flag("encoder-preset", o.encoding.preset);
  if (const auto it = l.flags.find("band"); it != l.flags.end()) o.band = it->second != "0";
  const auto sound_it = l.flags.find("sound");
  if (sound_it == l.flags.end() || sound_it->second != "0") o.sound = registered_sound();
  if (const std::string why = encoding_problem(o.encoding); !why.empty()) {
    return problem(out, 2, why, "the command line", "pass --format=mp4 (or webm, gif, mkv, png)");
  }
  const auto out_it = l.flags.find("out");
  const fs::path dir = out_it == l.flags.end() ? fs::path("runs") : fs::path(out_it->second);
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string base = std::string(spec.name) + "-" + core::num::to_string(log.seed) + "-" +
                           lens_file_name(spec, ask);
  const std::string ext(extension_of(o.encoding));
  o.video = (dir / (ext.empty() ? base + "-frames" : base + "." + ext)).generic_string();
  o.log = (dir / (base + "-encode.log")).generic_string();
  o.sheet = o.thumbs > 0 ? (dir / (base + "-sheet.png")).generic_string() : "";
  fs::remove(o.log, ec);
  const auto rec = record(spec, log, o, error);
  if (!rec) {
    return problem(
        out, 1, error, l.words.empty() ? "the pilot's run" : l.words[0],
        error.find("ffmpeg is not") != std::string::npos
            ? "install ffmpeg, or pass --format=png for the frames and the sheet alone"
        : error.find("ffmpeg") != std::string::npos
            ? "read " + o.log + ", then fix --codec, --crf, --bitrate or --encoder-preset"
        : o.gpu ? "play describe lists the gpu views and modes; pass --from and --to inside the log"
                : "play describe lists the views; pass --from and --to inside the log");
  }
  out << "{\"" << (ext.empty() ? "frames_dir" : "video") << "\":" << quote(o.video)
      << ",\"sheet\":" << quote(o.sheet) << ",\"format\":" << quote(o.encoding.format)
      << ",\"view\":" << quote(rec->view) << ",\"width\":" << rec->width
      << ",\"height\":" << rec->height << ",\"frames\":" << rec->frames
      << ",\"fps\":" << core::num::to_string(o.fps)
      << ",\"speed\":" << core::num::to_string(o.speed)
      << ",\"seconds\":" << core::num::to_fixed(rec->seconds, 2) << ",\"from\":" << rec->from
      << ",\"to\":" << rec->to << ",\"digest\":" << quote(hex64(rec->digest))
      << ",\"encoder\":" << quote(rec->encoder) << ",\"samples\":" << rec->samples
      << ",\"bytes\":" << rec->bytes << ",\"log\":" << quote(rec->encoder.empty() ? "" : o.log)
      << ",\"gpu\":" << (o.gpu ? "true" : "false") << ",\"views\":[" << lens_views_json(spec, o.gpu)
      << "]}\n";
  return 0;
}

std::string fixed(double v, int places) { return core::num::to_fixed(v, places); }

std::string bench_json(const GameSpec& spec, double seed, const BenchRun& b,
                       const std::string& series, const std::string& spans) {
  std::vector<double> us(b.step_us);
  double total = 0;
  for (const double v : us) total += v;
  const double median = quantile(us, 0.5);
  std::string zones;
  for (const ZoneCost& z : b.zones) {
    zones += std::string(zones.empty() ? "" : ",") + "{\"name\":" + quote(z.name) +
             ",\"calls_per_step\":" + fixed(z.calls_per_step, 3) +
             ",\"excl_us_per_step\":" + fixed(z.excl_us_per_step, 3) +
             ",\"incl_us_per_step\":" + fixed(z.incl_us_per_step, 3) +
             ",\"share\":" + fixed(z.share, 4) + ",\"allocs\":" + std::to_string(z.allocs) + "}";
  }
  std::string slow;
  for (const SlowStep& s : b.slowest) {
    slow += std::string(slow.empty() ? "" : ",") + "{\"step\":" + std::to_string(s.step) +
            ",\"us\":" + fixed(s.us, 3) + ",\"zone\":" + quote(s.zone) + "}";
  }
  std::string open_zones;
  for (const OpenZone& z : b.open_zones) {
    open_zones += std::string(open_zones.empty() ? "" : ",") + "{\"name\":" + quote(z.name) +
                  ",\"calls\":" + std::to_string(z.calls) + ",\"excl_ms\":" + fixed(z.excl_ms, 3) +
                  ",\"incl_ms\":" + fixed(z.incl_ms, 3) + "}";
  }
  int64_t allocs = 0;
  for (const int64_t a : b.step_allocs) allocs += a;
  const double budget = 1e6 / spec.hz;
  return "{\"seed\":" + core::num::to_string(seed) + ",\"steps\":" + std::to_string(b.steps) +
         ",\"digest\":" + quote(hex64(b.digest)) +
         ",\"same_digest\":" + (b.same_digest ? "true" : "false") +
         ",\"open_ms\":" + fixed(b.open_ms, 3) +
         ",\"open_allocs\":" + std::to_string(b.open_allocs) + ",\"open_zones\":[" + open_zones +
         "],\"step_us\":{\"median\":" + fixed(median, 3) +
         ",\"mean\":" + fixed(us.empty() ? 0 : total / static_cast<double>(us.size()), 3) +
         ",\"p95\":" + fixed(quantile(us, 0.95), 3) + ",\"p99\":" + fixed(quantile(us, 0.99), 3) +
         ",\"max\":" + fixed(quantile(us, 1.0), 3) + ",\"total_ms\":" + fixed(total / 1e3, 3) +
         "},\"budget_us\":" + fixed(budget, 3) + ",\"budget_share\":" + fixed(median / budget, 4) +
         ",\"steps_per_s\":" +
         fixed(total > 0 ? static_cast<double>(b.steps) * 1e6 / total : 0, 1) + ",\"zones\":[" +
         zones + "],\"untracked_share\":" + fixed(b.untracked_share, 4) +
         ",\"dropped_spans\":" + std::to_string(b.dropped_spans) + ",\"slowest\":[" + slow +
         "],\"allocs\":{\"total\":" + std::to_string(allocs) +
         ",\"warmup_steps\":" + std::to_string(b.warmup_steps) +
         ",\"steps_allocating\":" + std::to_string(b.steps_allocating) +
         ",\"first_allocating\":" + std::to_string(b.first_allocating) +
         ",\"zone\":" + quote(b.alloc_zone) + "},\"series\":" + quote(series) +
         ",\"spans\":" + quote(spans) + "}";
}

// `bench`: the pilot plays each seed (or the logs named are read), then each log is benched.
int bench_cmd(const GameSpec& spec, const Line& l, std::ostream& out) {
  BenchOptions o;
  double v = 0;
  if (!number_flag(l, "repeats", o.repeats, v) || v < 1 || v > 1000) {
    return problem(out, 2, "--repeats must be 1..1000", "the command line", "pass --repeats=5");
  }
  o.repeats = static_cast<int>(v);
  if (!number_flag(l, "steps", -1, v)) {
    return problem(out, 2, "--steps is not a number", "the command line", "pass --steps=1200");
  }
  o.most_steps = static_cast<int64_t>(v);
  const auto out_it = l.flags.find("out");
  const fs::path dir = out_it == l.flags.end() ? fs::path("runs") : fs::path(out_it->second);
  std::error_code ec;
  fs::create_directories(dir, ec);
  // What to bench: the logs named, or the pilot's run of each seed.
  std::vector<InputLog> logs;
  std::string error;
  if (!l.words.empty()) {
    for (const std::string& path : l.words) {
      InputLog log;
      if (!read_log(path, log, error)) return problem(out, 1, error, path, "pass an input log");
      logs.push_back(std::move(log));
    }
  } else {
    std::vector<double> seeds;
    std::string bad;
    if (!seeds_of(l, seeds, bad)) {
      return problem(out, 2, "--seeds holds `" + bad + "`, not a number", "the command line",
                     "pass --seeds=38,7");
    }
    for (const double seed : seeds) {
      Setup setup;
      if (!make_setup(spec, seed, options_of(l, {"seeds", "out", "repeats", "steps"}), setup,
                      error)) {
        return problem(out, 2, error, "the command line", "play describe lists the options");
      }
      auto p = pilot_run(spec, setup, error);
      if (!p) return problem(out, 1, error, "seed " + core::num::to_string(seed), "fix the game");
      logs.push_back(std::move(p->log));
    }
  }
  std::string runs;
  for (const InputLog& log : logs) {
    const auto b = bench(spec, log, o, error);
    if (!b) return problem(out, 1, error, "seed " + core::num::to_string(log.seed), "fix the game");
    const std::string stem = std::string(spec.name) + "-" + core::num::to_string(log.seed);
    const fs::path series = dir / (stem + ".bench.tsv");
    const fs::path spans = dir / (stem + ".spans.tsv");
    std::ofstream(series, std::ios::binary) << b->series;
    std::ofstream(spans, std::ios::binary) << b->spans;
    runs += std::string(runs.empty() ? "" : ",") +
            bench_json(spec, log.seed, *b, series.generic_string(), spans.generic_string());
  }
  const prof::Allocations a = prof::allocations();
  out << "{\"game\":" << quote(spec.name) << ",\"hz\":" << core::num::to_string(spec.hz)
      << ",\"repeats\":" << o.repeats << ",\"counting\":" << (a.counting ? "true" : "false")
      << ",\"peak_rss_bytes\":" << prof::peak_rss_bytes() << ",\"runs\":[" << runs << "]}\n";
  return 0;
}
}  // namespace

int play_main(int argc, char** argv, std::istream& in, std::ostream& out) {
  prof::count_allocations();
  const GameSpec* spec = registered_game();
  if (!spec) {
    return problem(out, 1, "no game is registered in this executable", "the play target",
                   "add a file with GE_GAME(spec) to it (skill: play-test)");
  }
  Line l;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a.rfind("--", 0) == 0) {
      const size_t eq = a.find('=');
      l.flags[a.substr(2, eq == std::string::npos ? std::string::npos : eq - 2)] =
          eq == std::string::npos ? "" : a.substr(eq + 1);
    } else if (l.op.empty()) {
      l.op = a;
    } else {
      l.words.push_back(a);
    }
  }
  if (l.op.empty()) return protocol(*spec, l, in, out);
  if (l.op == "describe") {
    out << describe(*spec) << "\n";
    return 0;
  }
  if (l.op == "sim") return sim(*spec, l, out);
  if (l.op == "replay") return replay_cmd(*spec, l, out);
  if (l.op == "state") return state_cmd(*spec, l, out);
  if (l.op == "diverge") return diverge_cmd(*spec, l, out);
  if (l.op == "bench") return bench_cmd(*spec, l, out);
  if (l.op == "shot") return shot_cmd(*spec, l, out);
  if (l.op == "why") return why_cmd(*spec, l, out);
  if (l.op == "record") return record_cmd(*spec, l, out);
  if (l.op == "cvar") return cvar_cmd(*spec, l.words, l.flags, out);
  if (l.op == "cfg") return cfg_cmd(*spec, l.words, l.flags, out);
  return problem(out, 2, "unknown command `" + l.op + "`", "the command line",
                 "run describe, sim, replay, state, diverge, bench, shot, record, why, cvar or "
                 "cfg, or no command for the protocol");
}

int play_main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  return play_main(argc, argv, std::cin, std::cout);
}

}  // namespace ge::play
