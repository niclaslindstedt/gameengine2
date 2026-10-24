// THE ge COMMAND LINE: the one interface an agent makes a game on this engine through (skill:
// agent-interface). The commands, their arguments and flags are the catalog in commands.hpp, which
// `ge help` prints and `ge help --json` hands over whole; every command answers in one shape
// (outcome.hpp), as text or, with `--json`, as one JSON object. Exit codes: 0 done, 1 the work
// found a problem, 2 a usage error.
//
// In a game repo, `make ge ARGS=...` builds and runs the ge of the engine the repo pins; a ge from
// a release package runs as is.
//
// ge:summary The ge executable: parses the command line against the catalog and runs each command
// ge:keywords cli ge main command-line usage dispatch help version new find explain use add
// ge:keywords build test check precheck ci digest golden lab graph shot upgrade doctor skills net
// ge:keywords lessons json bench profile mem leaks asset blender
// ge:skill agent-interface
// ge:test tests/cli_commands_test.cpp
#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>
#include <string>

#include "cli/add.hpp"
#include "cli/asset.hpp"
#include "cli/build.hpp"
#include "cli/ci.hpp"
#include "cli/commands.hpp"
#include "cli/cvar.hpp"
#include "cli/doctor.hpp"
#include "cli/embedded.hpp"
#include "cli/goals.hpp"
#include "cli/golden.hpp"
#include "cli/graph.hpp"
#include "cli/index.hpp"
#include "cli/json.hpp"
#include "cli/lint.hpp"
#include "cli/net.hpp"
#include "cli/outcome.hpp"
#include "cli/perf.hpp"
#include "cli/play.hpp"
#include "cli/publish.hpp"
#include "cli/scaffold.hpp"
#include "cli/skills.hpp"
#include "cli/sweep.hpp"
#include "cli/upgrade.hpp"

namespace {

using namespace ge::cli;

Outcome usage_error(const std::string& what, const std::string& next) {
  Outcome o;
  o.findings.push_back({Severity::error, what, "the command line", next});
  o.exit = 2;
  return o;
}

fs::path repo_of(const Args& a) {
  return has_flag(a, "repo") ? fs::path(flag_value(a, "repo")) : fs::current_path();
}

BuildOptions build_options(const Args& a, const char* preset = "debug") {
  BuildOptions b;
  b.repo = fs::absolute(repo_of(a));
  b.preset = flag_value(a, "preset", preset);
  b.engine = flag_value(a, "engine");
  return b;
}

Outcome cmd_help(const Args& a) {
  const CommandSpec* one = nullptr;
  if (!a.words.empty()) {
    std::string name;
    for (const std::string& w : a.words) name += (name.empty() ? "" : " ") + w;
    one = find_command(name);
    if (!one) return usage_error("no command \"" + name + "\"", "run `ge help` for the commands");
  }
  Outcome o;
  o.text = help_text(one);
  o.result = help_json(one);
  return o;
}

Outcome cmd_new(const Args& a) {
  NewOptions n{a.words[0], flag_value(a, "name"), flag_value(a, "engine-sha256")};
  if (const std::string game = flag_value(a, "game"); !game.empty()) n.game = game;
  const NewResult r = new_game(n);
  Outcome o;
  o.findings = r.findings;
  o.written = r.written;
  o.result = json_obj({{"dir", json_str(a.words[0])},
                       {"game", json_str(n.game)},
                       {"engine", json_str(engine_version())}});
  o.summary = "scaffolded " + a.words[0] + " against engine " + std::string(engine_version()) +
              "; its AGENTS.md is the router";
  o.next = "cd " + a.words[0] + " && git init && ge check";
  return o;
}

Outcome cmd_lessons(const Args& a) {
  const fs::path repo = repo_of(a);
  Outcome o;
  if (a.words.empty()) {
    std::vector<std::string> rows;
    for (const auto& [name, n] : lesson_inventory(repo)) {
      rows.push_back(json_obj({{"skill", json_str(name)}, {"lessons", json_num(n)}}));
      o.text += name + "  " + std::to_string(n) + (n == 1 ? " lesson\n" : " lessons\n");
    }
    o.result = json_obj({{"skills", json_arr(rows)}});
    o.next = "ge lessons <skill>";
    return o;
  }
  const auto inv = lesson_inventory(repo);
  const std::string& skill = a.words[0];
  if (std::none_of(inv.begin(), inv.end(), [&](const auto& s) { return s.first == skill; })) {
    std::string all;
    for (const auto& [name, n] : inv) all += (all.empty() ? "" : ", ") + name;
    return usage_error("no skill \"" + skill + "\"", "name one of: " + all);
  }
  std::vector<std::string> rows;
  for (const Lesson& l : lessons_of(repo, skill, flag_value(a, "scope"))) {
    rows.push_back(json_obj({{"file", json_str(l.file)},
                             {"title", json_str(l.title)},
                             {"date", json_str(l.date)},
                             {"scope", json_strs(l.scope)},
                             {"concepts", json_strs(l.concepts)},
                             {"body", json_str(l.body)}}));
  }
  std::ostringstream text;
  print_lessons(repo, skill, flag_value(a, "scope"), text);
  o.text = text.str();
  o.result = json_obj({{"skill", json_str(skill)}, {"lessons", json_arr(rows)}});
  return o;
}

Outcome run(const Args& a) {
  const std::string name(a.command->name);
  if (name == "help") return cmd_help(a);
  if (name == "version") {
    Outcome o;
    o.text = std::string(engine_version()) + "\n";
    o.result = json_obj({{"version", json_str(engine_version())}});
    return o;
  }
  if (name == "find") {
    const std::string limit = flag_value(a, "limit", "15");
    if (limit.find_first_not_of("0123456789") != std::string::npos || limit.empty()) {
      return usage_error("--limit takes a whole number", "rerun with --limit=15");
    }
    return run_find(repo_of(a), flag_value(a, "engine"), a.words, std::stoi(limit));
  }
  if (name == "explain") return run_explain(repo_of(a), flag_value(a, "engine"), a.words[0]);
  if (name == "lessons") return cmd_lessons(a);
  if (name == "goals free") {
    const std::string base = flag_value(a, "base");
    return run_goals_free(fs::absolute(repo_of(a)), a.words.empty() ? "" : a.words[0],
                          base.empty() ? "origin/main" : base, has_flag(a, "fetch"));
  }
  if (name == "goals claim") {
    const std::string base = flag_value(a, "base");
    std::string title;
    for (const std::string& w : a.words) title += (title.empty() ? "" : " ") + w;
    return run_goals_claim(fs::absolute(repo_of(a)), title, flag_value(a, "under"),
                           base.empty() ? "origin/main" : base);
  }
  if (name == "new") return cmd_new(a);
  if (name == "use") return run_use(repo_of(a), a.words[0]);
  if (name == "upgrade") {
    const UpgradeResult r = upgrade(repo_of(a), flag_value(a, "engine-sha256"));
    Outcome o;
    o.findings = r.findings;
    o.written = r.written;
    o.result = json_obj({{"from", json_str(r.from)}, {"to", json_str(r.to)}});
    o.summary = r.from == r.to ? "the repo already pins engine " + r.to + "; its skills match it"
                               : "moved the pin from engine " + r.from + " to " + r.to +
                                     " and synced its skills";
    o.next = "read the release notes (" + std::string(kEngineRepo) +
             "/releases), then ge check "
             "and every lab with --compare=before (skill: engine, its Upgrade steps)";
    return o;
  }
  if (name.starts_with("add ")) {
    return run_add(repo_of(a), name.substr(4), a.words[0], flag_value(a, "kind"),
                   has_flag(a, "dry-run"));
  }
  if (name == "build") return run_build(build_options(a));
  if (name == "test") {
    const std::string base = has_flag(a, "base") ? flag_value(a, "base") : "origin/main";
    return run_test(build_options(a), flag_value(a, "filter"), has_flag(a, "affected") ? base : "",
                    has_flag(a, "list"));
  }
  if (name == "check") return run_check(build_options(a));
  if (name == "lab sweep") {
    SweepOptions sweep;
    sweep.id = a.words[0];
    sweep.set = flag_value(a, "set");
    sweep.flag = flag_value(a, "flag");
    sweep.in = flag_value(a, "in");
    sweep.metrics = flag_value(a, "metrics");
    sweep.passthrough = a.passthrough;
    return run_lab_sweep(build_options(a), sweep);
  }
  if (name == "lab publish") {
    PublishOptions p;
    p.ids = a.words;
    p.before = flag_value(a, "before");
    p.out = flag_value(a, "out");
    if (const std::string w = flag_value(a, "width"); !w.empty()) {
      if (w.find_first_not_of("0123456789") != std::string::npos || w.size() > 5 ||
          std::stoi(w) < 16)
        return usage_error("--width takes a whole number of pixels, 16 or more",
                           "rerun with --width=720");
      p.width = std::stoi(w);
    }
    return run_lab_publish(fs::absolute(repo_of(a)), p);
  }
  if (name == "lab diff") {
    LabDiffOptions diff;
    diff.id = a.words[0];
    if (const std::string against = flag_value(a, "against"); !against.empty()) {
      diff.against = against;
    }
    diff.run = has_flag(a, "run");
    diff.bands = flag_value(a, "bands");
    diff.psnr = flag_value(a, "psnr");
    diff.passthrough = a.passthrough;
    return run_lab_diff(build_options(a), diff);
  }
  if (name == "precheck") {
    const std::string preset = flag_value(a, "preset");
    return run_precheck(build_options(a), preset.empty()
                                              ? std::vector<std::string>{"debug", "release"}
                                              : std::vector<std::string>{preset});
  }
  if (name == "ci") {
    if (has_flag(a, "wait")) {
      if (a.words.empty())
        return usage_error("--wait needs a pull request's number", "ge ci <pr> --wait");
      const std::string m = flag_value(a, "wait");
      int minutes = 60;
      if (!m.empty()) {
        const auto [p, ec] = std::from_chars(m.data(), m.data() + m.size(), minutes);
        if (ec != std::errc() || p != m.data() + m.size() || minutes < 1)
          return usage_error("--wait=" + m + " is not a whole number of minutes",
                             "ge ci <pr> --wait=60");
      }
      return run_ci_wait(fs::absolute(repo_of(a)), a.words[0], has_flag(a, "merge"), minutes);
    }
    return run_ci(fs::absolute(repo_of(a)), a.words.empty() ? "" : a.words[0], flag_value(a, "log"),
                  has_flag(a, "merge"));
  }
  if (name == "lint") {
    LintOptions l;
    l.files = a.words;
    l.base = flag_value(a, "base", "origin/main");
    l.all = has_flag(a, "all");
    return run_lint(build_options(a), l);
  }
  if (name == "digest") {
    DigestOptions d;
    d.labs = a.words;
    d.write = has_flag(a, "write");
    d.diff = has_flag(a, "diff");
    d.affected = has_flag(a, "affected");
    d.list = has_flag(a, "list");
    d.base = flag_value(a, "base", "origin/main");
    return run_digest(build_options(a), d);
  }
  if (name == "golden diff") {
    return run_golden_diff(fs::absolute(repo_of(a)), a.words, flag_value(a, "base", "HEAD"));
  }
  if (name == "golden write") return run_golden_write(build_options(a, "release"), a.words[0]);
  if (name == "lab") {
    LabOptions lab;
    lab.id = a.words.empty() ? "" : a.words[0];
    lab.smoke = has_flag(a, "smoke");
    lab.tag = flag_value(a, "tag");
    lab.compare = flag_value(a, "compare");
    lab.passthrough = a.passthrough;
    return run_lab(build_options(a), lab);
  }
  if (name == "sim") {
    std::vector<std::string> options = a.passthrough;
    if (has_flag(a, "human")) options.emplace_back("--human");
    return run_sim(build_options(a), flag_value(a, "seeds"), options);
  }
  if (name == "replay") return run_replay(build_options(a), a.words[0]);
  if (name == "state") return run_state(build_options(a), a.words[0], flag_value(a, "step"));
  if (name == "diverge") return run_diverge(build_options(a), a.words[0], a.words[1]);
  if (name.rfind("cvar ", 0) == 0 || name.rfind("cfg ", 0) == 0) {
    ConsoleAsk ask;
    ask.op = name.substr(name.find(' ') + 1);
    ask.words = a.words;
    ask.server = has_flag(a, "server");
    ask.cmd = flag_value(a, "cmd");
    ask.out = flag_value(a, "out");
    return name[1] == 'v' ? run_cvar(build_options(a), ask) : run_cfg(build_options(a), ask);
  }
  if (name == "why") {
    WhyOptions w;
    w.log = a.words[0];
    w.agent = flag_value(a, "agent");
    w.step = flag_value(a, "step");
    w.around = flag_value(a, "around");
    w.decision = flag_value(a, "decision");
    w.close = flag_value(a, "close");
    w.flips = has_flag(a, "flips");
    w.timeline = has_flag(a, "timeline");
    return run_why(build_options(a), w);
  }
  if (name == "shot" || name == "record") {
    std::vector<std::string> args;
    for (const auto& [k, v] : a.flags) {
      if (k != "preset" && k != "engine" && k != "repo" && k != "json")
        args.push_back(std::string("--").append(k).append("=").append(v));
    }
    if (name == "shot") {
      return run_shot(build_options(a), a.words.empty() ? "" : a.words[0], args, a.passthrough);
    }
    // A CPU view draws every frame on the CPU: the release build draws them many times faster.
    return run_record(build_options(a, "release"), a.words.empty() ? "" : a.words[0], args,
                      a.passthrough);
  }
  if (name == "bench" || name == "profile" || name == "mem" || name == "leaks") {
    PerfOptions p;
    p.seeds = flag_value(a, "seeds");
    p.repeats = flag_value(a, "repeats");
    p.steps = flag_value(a, "steps");
    p.tag = flag_value(a, "tag");
    p.compare = flag_value(a, "compare");
    p.render = has_flag(a, "render");
    p.valgrind = has_flag(a, "valgrind");
    p.options = a.passthrough;
    if (name == "leaks") return run_leaks(build_options(a), p);
    const BuildOptions b = build_options(a, "release");
    if (name == "bench") return run_bench(b, p);
    if (name == "profile") return run_profile(b, p);
    return run_mem(b, p);
  }
  if (name.starts_with("net ")) return run_net(a);
  if (name == "asset list") return run_asset_list(build_options(a));
  if (name == "asset check") return run_asset_check(build_options(a));
  if (name == "asset build") {
    AssetOptions as;
    as.names = a.words;
    as.force = has_flag(a, "force");
    as.blender = flag_value(a, "blender");
    return run_asset_build(build_options(a), as);
  }
  if (name == "graph") {
    GraphOptions g;
    g.files = a.words;
    g.kind = flag_value(a, "kind", "auto");
    const std::string cols = flag_value(a, "columns");
    for (size_t start = 0; start < cols.size();) {
      const size_t comma = cols.find(',', start);
      const std::string one =
          cols.substr(start, comma == std::string::npos ? comma : comma - start);
      if (!one.empty()) g.columns.push_back(one);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    g.x = flag_value(a, "x");
    g.out = flag_value(a, "out");
    g.title = flag_value(a, "title");
    g.log_y = has_flag(a, "log");
    return run_graph(g);
  }
  if (name == "doctor") {
    Outcome o;
    o.findings = doctor(repo_of(a));
    o.summary = "ok against engine " + std::string(engine_version());
    o.next = "ge check";
    return o;
  }
  if (name == "skills sync") {
    const SyncResult r = sync_skills(repo_of(a), has_flag(a, "force"));
    Outcome o;
    o.findings = r.findings;
    o.written = r.written;
    o.summary = "the engine's skills match engine " + std::string(engine_version());
    return o;
  }
  return usage_error("ge " + name + " has no handler", "report it: this ge is broken");
}

// A ge of another release run in a repo that pins this engine hands the command to the ge the pin
// builds (build/<preset>/ge), so the rules and the skills are always the pinned release's. The
// pinned ge's version matches the pin, so it never hands on again. -1: run here.
int hand_to_pinned(const Args& a, int argc, char** argv) {
  if (a.command &&
      (a.command->name == "new" || a.command->name == "help" || a.command->name == "upgrade")) {
    return -1;
  }
  const fs::path repo = repo_of(a);
  const auto pin = read_pin(repo);
  if (!pin || pin->version == engine_version()) return -1;
  for (const char* preset : {"debug", "release"}) {
    fs::path exe = repo / "build" / preset / "ge";
#ifdef _WIN32
    exe += ".exe";
#endif
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) continue;
    std::string line = "\"" + exe.string() + "\"";
    for (int i = 1; i < argc; i++) line += " \"" + std::string(argv[i]) + "\"";
    const int code = run_passthrough(line);
    return code < 0 ? 2 : code;
  }
  return -1;
}

}  // namespace

int main(int argc, char** argv) {
  const Args a = parse_args(argc, argv);
  if (const int handed = hand_to_pinned(a, argc, argv); handed >= 0) return handed;
  const bool json = has_flag(a, "json");
  if (!a.command) {
    if (a.words.empty()) {
      Args help = a;
      help.command = find_command("help");
      return emit("ge help", cmd_help(help), json, std::cout);
    }
    return emit(
        "ge",
        usage_error("unknown command \"" + a.words[0] + "\"", "run `ge help` for the commands"),
        json, std::cout);
  }
  const std::string command = "ge " + std::string(a.command->name);
  if (has_flag(a, "help")) {
    Outcome o;
    o.text = help_text(a.command);
    o.result = help_json(a.command);
    return emit(command, o, json, std::cout);
  }
  if (const auto bad = check_args(a)) {
    return emit(command,
                usage_error(*bad, "run `ge help " + std::string(a.command->name) +
                                      "` for its arguments and flags"),
                json, std::cout);
  }
  if (a.command->name == "play") {
    // The protocol owns stdout once the session starts: nothing of ge's is printed after it.
    bool played = false;
    Outcome o = run_play(build_options(a), flag_value(a, "seed"), a.passthrough, played);
    if (played) return o.exit;
    return emit(command, o, json, std::cout);
  }
  return emit(command, run(a), json, std::cout);
}
