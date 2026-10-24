// `ge net stress` (net.hpp): the pressure tests of net/stress.hpp from the command line: each
// knob's breaking point for a mix, and the chaos scripts, a soak and the fuzzer when asked, as
// text, findings and one JSON result.
//
// ge:summary ge net stress: each knob's breaking point, chaos scripts, a soak and the fuzzer
// ge:keywords cli net stress ramp breaking-point chaos soak fuzz pressure run_net_stress
// ge:skill agent-interface net
// ge:test tests/cli_net_test.cpp
#include <algorithm>
#include <cmath>

#include "cli/json.hpp"
#include "cli/net.hpp"
#include "core/num_text.hpp"
#include "net/stress.hpp"

namespace ge::cli {

namespace {

std::string fixed(double v, int digits) {
  return std::isfinite(v) ? core::num::to_fixed(v, digits) : std::string("-");
}

std::string hex16(uint64_t v) {
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

std::string value_text(net::StressKnob k, double v) {
  switch (k) {
    case net::StressKnob::loss:
      return fixed(v * 100, 1) + "%";
    case net::StressKnob::burst:
      return fixed(v, 1) + " drops";
    case net::StressKnob::latency:
    case net::StressKnob::jitter:
      return fixed(v, 0) + " ms";
    case net::StressKnob::bandwidth:
      return fixed(v, 0) + " kbit/s";
  }
  return fixed(v, 2);
}

Outcome failed(const std::string& what, const std::string& next) {
  Outcome o;
  o.findings.push_back({Severity::error, what, "the command line", next});
  o.exit = 2;
  return o;
}

}  // namespace

Outcome run_net_stress(const NetStressOptions& opts) {
  net::TrafficPlan base;
  if (!net::conditions_preset(opts.preset, base.conditions) && opts.preset != "none") {
    std::string all = "none";
    for (const std::string& p : net::preset_names()) all += "|" + p;
    return failed("no preset \"" + opts.preset + "\"", "rerun with --preset=" + all);
  }
  if (const std::string bad = apply_link(opts.link, base.conditions); !bad.empty())
    return failed("--link: " + bad, "rerun with --link=latency=40,jitter=10,loss=2%");
  if (!net::traffic_mix(opts.mix, base.mix)) {
    std::string all;
    for (const std::string& m : net::mix_names()) all += (all.empty() ? "" : "|") + m;
    return failed("no traffic mix \"" + opts.mix + "\"", "rerun with --mix=" + all);
  }
  if (opts.clients < 1 || opts.clients > 32 || !(opts.seconds > 0) || opts.steps < 1 ||
      opts.refine < 0 || opts.soak_clients < 1 || opts.soak_clients > 32)
    return failed("the session is out of range",
                  "use --clients=1..32, --seconds above 0, --steps=1 or more, --refine=0 or more "
                  "and --soak-clients=1..32");
  base.clients = opts.clients;
  base.seconds = opts.seconds;
  base.drain_seconds = 4;
  base.seed = opts.seed;

  std::vector<net::StressKnob> knobs;
  for (const std::string& name : opts.knobs) {
    net::StressKnob k;
    if (!net::stress_knob(name, k))
      return failed("no knob \"" + name + "\"",
                    "rerun with --knobs= any of loss,burst,latency,jitter,bandwidth");
    knobs.push_back(k);
  }
  if (opts.knobs.empty()) knobs = net::stress_knobs();
  std::vector<std::string> scripts = opts.chaos;
  if (scripts.size() == 1 && scripts[0] == "all") scripts = net::chaos_names();

  Outcome o;
  std::vector<std::vector<std::string>> rows{
      {"knob", "held up to", "broke at", "the bound it broke", "runs", "hash"}};
  std::vector<std::string> ramps;
  net::RampPlan rp;
  rp.steps = opts.steps;
  rp.refine = opts.refine;
  for (net::StressKnob k : knobs) {
    rp.knob = k;
    const net::Ramp r = net::run_ramp(base, rp);
    rows.push_back({net::knob_name(k), value_text(k, r.held),
                    r.found ? value_text(k, r.broke) : "never", r.found ? r.why : "-",
                    std::to_string(r.points.size()), hex16(r.hash)});
    if (r.found && r.points.front().broke) {
      o.findings.push_back(
          {Severity::warn,
           std::string(net::knob_name(k)) + ": the starting conditions already break \"" +
               r.points.front().broke + "\"",
           "engine/net/stress.cpp",
           "start from a milder --preset or a lighter --mix, or read stress_breaks (skill: net)"});
    }
    ramps.push_back(json_obj({{"knob", json_str(net::knob_name(k))},
                              {"found", json_bool(r.found)},
                              {"held", core::num::to_string(r.held)},
                              {"broke", r.found ? core::num::to_string(r.broke) : "null"},
                              {"why", r.found ? json_str(r.why) : "null"},
                              {"runs", json_num(static_cast<long long>(r.points.size()))},
                              {"hash", json_str(hex16(r.hash))}}));
  }
  o.text = "  " + std::to_string(opts.clients) + " clients, " + net::describe_mix(base.mix) +
           ", from " + net::describe_conditions(base.conditions) + ", seed " +
           std::to_string(opts.seed) + "\n" + table(rows) +
           "  a bound breaks when a client drops, the inputs' p95 passes 250 ms, fewer than 85% "
           "of snapshots arrive, or a reliable message is lost or its p95 passes 1500 ms\n";

  std::vector<std::string> chaos;
  if (!scripts.empty()) {
    std::vector<std::vector<std::string>> crows{{"script", "ends", "connected", "timed out",
                                                 "stuck", "drops", "reconnects", "back after s",
                                                 "ok"}};
    for (const std::string& name : scripts) {
      net::ChaosScript script;
      if (!net::chaos_script(name, base, script)) {
        std::string all;
        for (const std::string& s : net::chaos_names()) all += (all.empty() ? "" : ",") + s;
        return failed("no chaos script \"" + name + "\"",
                      "rerun with --chaos=all or any of " + all);
      }
      const net::TrafficPlan plan = net::chaos_plan(base, script);
      const net::TrafficReport r = net::run_traffic(plan);
      const char* why = net::chaos_fails(r, plan, script);
      crows.push_back({name, script.end == net::ChaosEnd::all_connected ? "connected" : "timed out",
                       std::to_string(r.ended_connected), std::to_string(r.ended_timed_out),
                       std::to_string(r.ended_stuck), std::to_string(r.dropped),
                       std::to_string(r.reconnects), r.reconnects ? fixed(r.recover_s, 1) : "-",
                       why ? "NO" : "yes"});
      if (why) {
        o.findings.push_back({Severity::error, name + ": " + why, "engine/net/traffic.cpp",
                              "make lab LAB=net-chaos ARGS=--scripts=" + name +
                                  " charts it; read the reconnects in traffic.cpp and node.cpp's "
                                  "timeouts (skill: net)"});
      }
      chaos.push_back(json_obj({{"script", json_str(name)},
                                {"ok", json_bool(!why)},
                                {"connected", json_num(r.ended_connected)},
                                {"timed_out", json_num(r.ended_timed_out)},
                                {"stuck", json_num(r.ended_stuck)},
                                {"reconnects", json_num(r.reconnects)},
                                {"recover_s", core::num::to_string(r.recover_s)},
                                {"digest", json_str(hex16(r.digest))}}));
    }
    o.text += "  chaos, each script in turn:\n" + table(crows);
  }

  std::string soak = "null";
  if (opts.soak > 0) {
    net::TrafficPlan p = base;
    p.clients = opts.soak_clients;
    p.seconds = opts.soak;
    p.mix = net::soak_mix();
    p.sample_s = std::max(1.0, opts.soak / 240);
    const net::SoakReport s = net::run_soak(p);
    o.text += "  soak: " + std::to_string(opts.soak_clients) + " clients for " +
              fixed(opts.soak, 0) + " s: held " + fixed(s.held_kb_start, 0) + " KB then " +
              fixed(s.held_kb_end, 0) + " KB (" + (s.flat ? "flat" : "GROWING") + "), " +
              std::to_string(s.seq_wraps) + " packet wraps, " + std::to_string(s.id_wraps) +
              " reliable id wraps, reliable " + (s.reliable_whole ? "whole" : "BROKEN") + "\n";
    if (!s.flat)
      o.findings.push_back({Severity::error, "the soak's memory grew", "engine/net/traffic.cpp",
                            "make lab LAB=net-stress charts it; find the buffer that grows "
                            "(skill: net)"});
    if (!s.reliable_whole)
      o.findings.push_back({Severity::error, "the soak lost a reliable message or a connection",
                            "engine/net/connection.cpp",
                            "run tests/net_connection_test.cpp and read the id wraps (skill: "
                            "net)"});
    soak = json_obj({{"seconds", core::num::to_string(opts.soak)},
                     {"held_kb_start", core::num::to_string(s.held_kb_start)},
                     {"held_kb_end", core::num::to_string(s.held_kb_end)},
                     {"flat", json_bool(s.flat)},
                     {"seq_wraps", json_num(static_cast<long long>(s.seq_wraps))},
                     {"id_wraps", json_num(static_cast<long long>(s.id_wraps))},
                     {"reliable_whole", json_bool(s.reliable_whole)}});
  }

  std::string fuzz = "null";
  if (opts.fuzz > 0) {
    net::FuzzPlan f;
    f.session = base;
    f.session.seconds = std::max(opts.seconds, 10.0);
    f.datagrams = static_cast<uint64_t>(opts.fuzz);
    f.seed = opts.seed;
    const net::FuzzReport r = net::run_fuzz(f);
    o.text += "  fuzz: " + std::to_string(r.sent) + " datagrams, " + std::to_string(r.parsed) +
              " began as a packet; the honest session " +
              (r.honest ? std::string("never noticed") : std::string("broke: ") + r.why) + "\n";
    if (!r.honest)
      o.findings.push_back({Severity::error, std::string("the fuzz broke the session: ") + r.why,
                            "engine/net/node.cpp",
                            "make lab LAB=net-fuzz reproduces it; read node_update's receive path "
                            "(skill: net)"});
    fuzz = json_obj({{"sent", json_num(static_cast<long long>(r.sent))},
                     {"parsed", json_num(static_cast<long long>(r.parsed))},
                     {"honest", json_bool(r.honest)},
                     {"hash", json_str(hex16(r.hash))}});
  }

  o.result = json_obj({{"mix", json_str(opts.mix)},
                       {"preset", json_str(opts.preset)},
                       {"clients", json_num(opts.clients)},
                       {"ramps", json_arr(ramps)},
                       {"chaos", json_arr(chaos)},
                       {"soak", soak},
                       {"fuzz", fuzz}});
  o.summary = std::to_string(knobs.size()) + " knobs ramped" +
              (scripts.empty() ? "" : ", " + std::to_string(scripts.size()) + " chaos scripts");
  o.next =
      "make lab LAB=net-stress charts each ramp; ge net sim --presets=none --link=<the "
      "breaking value> reruns one";
  return o;
}

}  // namespace ge::cli
