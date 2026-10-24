// LAB `net-chaos`: a session's links changed while it runs, one script at a time: a link
// flapping, a partition and its healing, the host lost for good, the host lost and started again,
// a NAT rebinding clients' ports, clocks running fast and slow, a loss burst, the bandwidth
// collapsing. Clients that lose their connection try again; each script names how it must end
// (every client connected, or every client timed out cleanly) and the lab fails one that ends
// any other way, or leaves the host a slot for a client that is gone. It charts the clients
// connected through each script. Everything runs on a virtual clock off one seed.
//
// ge:summary Lab: chaos scripts over a session: drops, reconnects, clean timeouts, charted
// ge:keywords lab net chaos partition flap host-lost host-restart rebind clock-skew burst
// ge:keywords collapse reconnect timeout zombie multiplayer
// ge:skill net labs
// ge:test tests/net_stress_test.cpp
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

void run(gl::Lab& lab) {
  gn::TrafficPlan base;
  base.clients = static_cast<int>(lab.arg_num("clients"));
  base.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  if (!gn::conditions_preset(lab.arg("preset"), base.conditions) ||
      !gn::traffic_mix(lab.arg("mix"), base.mix) || base.clients < 2 || base.clients > 32) {
    lab.fail("--preset, --mix or --clients is out of range",
             "Pass a preset (lan, broadband, wifi, mobile, bad, awful), a mix (racing, action, "
             "rpg, world, lockstep, download) and --clients=2 to 32; skill: net.");
    return;
  }
  gl::LabTable table{
      std::to_string(base.clients) + " clients of the " + lab.arg("mix") + " mix on " +
          lab.arg("preset") + ", each script in turn",
      {"script", "what it does", "must end", "connected", "timed out", "stuck", "drops",
       "reconnects", "back after s", "host zombies", "strangers", "ok", "log"},
      {}};
  gl::LineChart chart{{"CLIENTS CONNECTED THROUGH EACH SCRIPT",
                       std::to_string(base.clients) + " clients on " + lab.arg("preset"), "t (s)",
                       "clients connected"},
                      {},
                      {},
                      {},
                      {},
                      false,
                      false,
                      false};
  for (const std::string& name : split(lab.arg("scripts"))) {
    gn::ChaosScript script;
    if (!gn::chaos_script(name, base, script)) {
      lab.fail("unknown script '" + name + "'",
               "Pick from flap, partition, host-lost, host-restart, rebind, skew, burst, collapse "
               "(engine/net/stress.cpp); skill: net.");
      continue;
    }
    const gn::TrafficPlan plan = gn::chaos_plan(base, script);
    const gn::TrafficReport r = gn::run_traffic(plan);
    const char* why = gn::chaos_fails(r, plan, script);
    table.rows.push_back(
        {name, script.what,
         script.end == gn::ChaosEnd::all_connected
             ? (script.keeps_connections ? "connected, no drop" : "all reconnected")
             : "all timed out",
         std::to_string(r.ended_connected), std::to_string(r.ended_timed_out),
         std::to_string(r.ended_stuck), std::to_string(r.dropped), std::to_string(r.reconnects),
         r.reconnects ? fmt(r.recover_s, 1) : "-", std::to_string(r.host_zombies),
         std::to_string(r.host_strangers), why ? "NO" : "yes", hex(r.digest)});
    lab.metric(name + "-reconnects", r.reconnects, "count");
    lab.metric(name + "-recover-s", r.recover_s, "s");
    if (why)
      lab.fail(name + ": " + why,
               "Read the script in chaos_script and the reconnect and end-state code in "
               "engine/net/traffic.cpp, then node.cpp's timeouts; skill: net.");
    chart.traces.push_back({name, r.t_s, r.series_connected});
  }
  lab.table(std::move(table));
  lab.note(
      "Each script runs 20 s of sending and 8 s of drain; a client that loses its connection "
      "waits a second and tries again, three times. connected / timed out / stuck: how the "
      "clients ended (stuck: still connecting or waiting to try, which no script allows). drops: "
      "connections lost; reconnects: made again; back after: the longest from a client losing "
      "its connection to having one again. host zombies: the host's connected peers at the end "
      "with no client behind them. strangers: datagrams the host had no peer for (a rebound "
      "client's, until it reconnects).");
  if (chart.traces.empty()) return;
  const gl::Chart c = gl::line_chart(chart);
  const std::string svg = gl::to_svg(c.plot);
  lab.attach("connected.svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({"connected",
            c.title,
            "How many clients are connected at each moment, one line a script.",
            "lines that dip when a script cuts clients off and climb back once it heals (a "
            "timeout's 5 s and a retry's second later); host-lost's line falls to zero and stays "
            "there; flap, skew and burst stay flat at every client.",
            {{"scripts", lab.arg("scripts")},
             {"clients", lab.arg("clients")},
             {"preset", lab.arg("preset")},
             {"seed", lab.arg("seed")}}},
           gl::to_picture(c.plot));
}

const gl::LabSpec kSpec{
    "net-chaos",
    gl::LabKind::sim,
    "engine",
    "engine/net (stress, traffic)",
    "net",
    "When the links change under a running session (a flap, a partition, the host lost, a NAT "
    "rebinding, clock skew, a burst, a collapse), does every client end connected again or "
    "cleanly timed out?",
    {
        {"scripts", "flap,partition,host-lost,host-restart,rebind,skew,burst,collapse",
         "the chaos scripts, comma-separated"},
        {"preset", "broadband", "every link's conditions"},
        {"mix", "racing", "the traffic mix"},
        {"clients", "8", "clients a session (2 to 32)"},
        {"seed", "1", "the network's stream"},
    },
    "--clients=4 --scripts=flap,partition,host-lost,rebind",
    run,
};
GE_LAB(kSpec);

}  // namespace
