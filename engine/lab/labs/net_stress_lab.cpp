// LAB `net-stress`: the network pushed until it breaks. For one traffic mix it raises each knob
// of the links (loss, burst, latency, jitter, and the bandwidth taken down) step by step until the
// session breaks a bound (inputs too late, snapshots lost, a reliable message lost or too late, a
// connection dropped), bisects to the breaking point, and charts each knob's latencies against
// its value; then it soaks a session for a long stretch of virtual time and charts what the
// network and the nodes hold, which must stay flat, with every reliable message whole across the
// sequence numbers' wraps. Everything runs on a virtual clock off one seed.
//
// ge:summary Lab: each link knob ramped to its breaking point, and a long soak's memory charted
// ge:keywords lab net stress ramp breaking-point bounds loss burst latency jitter bandwidth
// ge:keywords soak memory flat wraps multiplayer
// ge:skill net labs
// ge:test tests/net_stress_test.cpp
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "net/stress.hpp"

namespace {

namespace gl = ge::lab;
namespace gn = ge::net;

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    const size_t comma = s.find(',', at);
    const std::string part =
        s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!part.empty()) out.push_back(part);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return out;
}

std::string fmt(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

std::string hex(uint64_t h) {
  char b[20];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}

// A knob's value in its unit.
std::string value_text(gn::StressKnob k, double v) {
  switch (k) {
    case gn::StressKnob::loss:
      return fmt(v * 100, 1) + "%";
    case gn::StressKnob::burst:
      return fmt(v, 1) + " drops";
    case gn::StressKnob::latency:
    case gn::StressKnob::jitter:
      return fmt(v, 0) + " ms";
    case gn::StressKnob::bandwidth:
      return fmt(v, 0) + " kbit/s";
  }
  return fmt(v, 2);
}

void run(gl::Lab& lab) {
  gn::TrafficPlan base;
  base.clients = static_cast<int>(lab.arg_num("clients"));
  base.seconds = lab.arg_num("seconds");
  base.drain_seconds = 4;
  base.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  const double soak_s = lab.arg_num("soak-seconds");
  const int soak_clients = static_cast<int>(lab.arg_num("soak-clients"));
  if (!gn::conditions_preset(lab.arg("preset"), base.conditions) ||
      !gn::traffic_mix(lab.arg("mix"), base.mix) || base.clients < 1 || base.clients > 32 ||
      base.seconds < 2 || soak_clients < 1 || soak_clients > 32) {
    lab.fail("--preset, --mix, --clients, --seconds or --soak-clients is out of range",
             "Pass a preset (lan, broadband, wifi, mobile, bad, awful), a mix (racing, action, "
             "rpg, world, lockstep, download), --clients=1 to 32, --seconds=2 or more and "
             "--soak-clients=1 to 32; skill: net.");
    return;
  }
  gn::RampPlan rp;
  rp.steps = static_cast<int>(lab.arg_num("steps"));
  rp.refine = static_cast<int>(lab.arg_num("refine"));
  gl::LabTable table{"Each knob ramped from " + lab.arg("preset") + " until the " + lab.arg("mix") +
                         " mix of " + std::to_string(base.clients) + " clients breaks a bound",
                     {"knob", "held up to", "broke at", "the bound it broke", "runs", "log"},
                     {}};
  for (const std::string& name : split(lab.arg("knobs"))) {
    if (!gn::stress_knob(name, rp.knob)) {
      lab.fail("unknown knob '" + name + "'",
               "Pick from loss, burst, latency, jitter, bandwidth (engine/net/stress.cpp); "
               "skill: net.");
      continue;
    }
    const gn::Ramp r = gn::run_ramp(base, rp);
    table.rows.push_back({name, value_text(rp.knob, r.held),
                          r.found ? value_text(rp.knob, r.broke) : "never", r.found ? r.why : "-",
                          std::to_string(r.points.size()), hex(r.hash)});
    lab.metric(name + "-held", r.held, rp.knob == gn::StressKnob::loss ? "ratio" : "value");
    if (r.found && r.points.front().broke)
      lab.fail(name + ": the base conditions already break '" +
                   std::string(r.points.front().broke) + "'",
               "Pass a milder --preset or a lighter --mix, or read stress_breaks in "
               "engine/net/stress.cpp; skill: net.");
    gl::LineChart chart{{"RAMP: " + name,
                         lab.arg("mix") + " mix, " + std::to_string(base.clients) +
                             " clients, from " + lab.arg("preset"),
                         name + " (" +
                             (rp.knob == gn::StressKnob::loss        ? "share"
                              : rp.knob == gn::StressKnob::burst     ? "drops"
                              : rp.knob == gn::StressKnob::bandwidth ? "kbit/s"
                                                                     : "ms") +
                             ")",
                         "p95 latency (ms)"},
                        {},
                        {},
                        {},
                        {rp.bounds.input_p95_ms, rp.bounds.reliable_p95_ms},
                        true,
                        true,
                        false};
    gl::Trace in{"inputs p95", {}, {}}, rel{"reliable p95", {}, {}};
    for (const gn::RampPoint& p : r.points) {
      in.x.push_back(p.value);
      in.y.push_back(std::max(p.input_p95_ms, 1.0));
      rel.x.push_back(p.value);
      rel.y.push_back(std::max(p.reliable_p95_ms, 1.0));
    }
    chart.traces = {in, rel};
    if (r.found) chart.marks.push_back({r.broke, "broke: " + std::string(r.why)});
    const gl::Chart c = gl::line_chart(chart);
    const std::string svg = gl::to_svg(c.plot);
    lab.attach("ramp-" + name + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
    lab.shot({"ramp-" + name,
              c.title,
              "Each run of the ramp as a point: the knob's value across, the inputs' and the "
              "reliable messages' 95th-percentile latency up (log scale); the dashed lines are "
              "the bounds, the mark the breaking point.",
              "points that climb as the knob worsens and cross a bound, or end the session, at "
              "the mark; points crowded round the mark are the bisection.",
              {{"knob", name},
               {"preset", lab.arg("preset")},
               {"mix", lab.arg("mix")},
               {"clients", lab.arg("clients")},
               {"seed", lab.arg("seed")}}},
             gl::to_picture(c.plot));
  }
  lab.table(std::move(table));

  if (soak_s > 0) {
    gn::TrafficPlan p = base;
    p.clients = soak_clients;
    p.seconds = soak_s;
    p.mix = gn::soak_mix();
    p.sample_s = std::max(1.0, soak_s / 240);
    const gn::SoakReport s = gn::run_soak(p);
    const gn::TrafficReport& t = s.traffic;
    gl::LabTable soak{
        std::to_string(soak_clients) + " clients soaked for " + fmt(soak_s, 0) +
            " s of virtual time on " + lab.arg("preset"),
        {"held KB, first half", "second half", "flat", "packet wraps", "reliable id wraps",
         "reliable whole", "inputs mean ms", "inputs max ms", "drops", "log"},
        {}};
    soak.rows.push_back(
        {fmt(s.held_kb_start, 0), fmt(s.held_kb_end, 0), s.flat ? "yes" : "NO",
         std::to_string(s.seq_wraps), std::to_string(s.id_wraps), s.reliable_whole ? "yes" : "NO",
         fmt(t.inputs.delivered ? t.inputs.latency_sum_ms / static_cast<double>(t.inputs.delivered)
                                : 0,
             1),
         fmt(t.inputs.latency_max_ms, 0), std::to_string(t.dropped), hex(t.digest)});
    lab.table(std::move(soak));
    lab.metric("soak-held-kb", s.held_kb_end, "KB");
    if (!s.flat)
      lab.fail("the soak's memory grew from " + fmt(s.held_kb_start, 0) + " KB to " +
                   fmt(s.held_kb_end, 0) + " KB",
               "Something queues without bound: read node_held_bytes and sim_held_bytes in "
               "engine/net/traffic.cpp, chart each buffer, and find the one that grows; skill: "
               "net.");
    if (!s.reliable_whole)
      lab.fail("the soak lost or reordered a reliable message",
               "Read the reliable window and the id wraps in engine/net/connection.cpp, then run "
               "tests/net_connection_test.cpp; skill: net.");
    gl::LineChart held{
        {"SOAK: WHAT THE NETWORK AND THE NODES HOLD",
         std::to_string(soak_clients) + " clients, " + fmt(soak_s, 0) + " s", "t (s)", "held (KB)"},
        {{"held", t.t_s, t.series_held_kb}},
        {},
        {},
        {},
        false,
        false,
        true};
    const gl::Chart c = gl::line_chart(held);
    const std::string svg = gl::to_svg(c.plot);
    lab.attach("soak.svg", std::vector<uint8_t>(svg.begin(), svg.end()));
    lab.shot({"soak",
              c.title,
              "The bytes the simulated network and every node hold (their buffers' capacities), "
              "sampled through the soak.",
              "a line that rises once while the buffers find their size and is flat after; a "
              "line still rising at the end is a leak or a queue without bound.",
              {{"soak-seconds", lab.arg("soak-seconds")},
               {"soak-clients", lab.arg("soak-clients")},
               {"preset", lab.arg("preset")},
               {"seed", lab.arg("seed")}}},
             gl::to_picture(c.plot));
  }
  lab.note(
      "A bound breaks when: a client never connects or drops; the inputs' one-way p95 passes "
      "250 ms; fewer than 85% of the snapshots arrive; a reliable message is lost or out of "
      "order by the end of the drain; the reliable messages' p95 passes 1500 ms. held up to: the "
      "worst value that kept every bound; broke at: the mildest that broke one (bisected between "
      "them). runs: sessions run for the knob. A soak sums the latencies instead of keeping them; "
      "held KB: the peak over each half (past a warm-up tenth); flat: the second half's within 5% "
      "and 16 KB of the first's; wraps: times the host's packet sequence and a client's reliable "
      "ids went round 65536.");
}

const gl::LabSpec kSpec{
    "net-stress",
    gl::LabKind::sim,
    "engine",
    "engine/net (stress, traffic)",
    "net",
    "For a mix of traffic, how much loss, burst, latency, jitter or lost bandwidth does a session "
    "take before it breaks a bound, and does a long soak keep its memory flat?",
    {
        {"knobs", "loss,burst,latency,jitter,bandwidth", "the knobs to ramp, comma-separated"},
        {"preset", "broadband", "the conditions each ramp starts from"},
        {"mix", "racing", "the traffic mix (racing, action, rpg, world, lockstep, download)"},
        {"clients", "8", "clients a session (1 to 32)"},
        {"seconds", "6", "how long each ramp's session sends"},
        {"steps", "8", "a ramp's coarse steps"},
        {"refine", "4", "bisections to the breaking point"},
        {"soak-seconds", "3600", "the soak's length in virtual seconds (0: no soak)"},
        {"soak-clients", "32", "clients in the soak"},
        {"seed", "1", "the network's stream"},
    },
    "--knobs=loss,bandwidth --steps=4 --refine=2 --seconds=3 --soak-seconds=60 --soak-clients=8",
    run,
};
GE_LAB(kSpec);

}  // namespace
