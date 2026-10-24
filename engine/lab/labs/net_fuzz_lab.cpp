// LAB `net-fuzz`: random and mutated datagrams put on the wire beside an honest session (copies
// of the packets in flight with bits flipped, cut short, run long or spliced, the game's own
// header over garbage, a handshake flood, payloads with guessed keys), from strangers' addresses
// and then also spoofed from the peers' own, every one run through the packet parser too. It
// answers whether the honest session notices: every client still connected, every reliable
// message whole, no host slot given away. It charts the honest session's round trip with the
// fuzz and without. Everything runs on a virtual clock off one seed.
//
// ge:summary Lab: fuzzed datagrams beside an honest session; the parser and the session unharmed
// ge:keywords lab net fuzz fuzzer mutate spoof datagram parser robustness handshake-flood
// ge:keywords multiplayer security
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

std::string hex(uint64_t h) {
  char b[20];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}

void run(gl::Lab& lab) {
  gn::FuzzPlan f;
  f.session.clients = static_cast<int>(lab.arg_num("clients"));
  f.session.seconds = lab.arg_num("seconds");
  f.session.drain_seconds = 4;
  f.session.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  f.seed = f.session.seed;
  f.datagrams = static_cast<uint64_t>(lab.arg_num("datagrams"));
  if (!gn::conditions_preset(lab.arg("preset"), f.session.conditions) || f.session.clients < 1 ||
      f.session.clients > 32 || f.session.seconds < 1) {
    lab.fail("--preset, --clients or --seconds is out of range",
             "Pass a preset (lan, broadband, wifi, mobile, bad, awful), --clients=1 to 32 and "
             "--seconds=1 or more; skill: net.");
    return;
  }
  const gn::TrafficReport clean = gn::run_traffic(f.session);
  gl::LabTable table{std::to_string(f.datagrams) + " fuzzed datagrams beside " +
                         std::to_string(f.session.clients) + " honest clients on " +
                         lab.arg("preset"),
                     {"from", "sent", "parsed", "host foreign", "strangers", "bad keys",
                      "bad cookies", "clients at end", "drops", "reliable whole", "honest", "log"},
                     {}};
  gl::LabTable kinds{"The fuzzed datagrams by kind", {}, {{}}};
  gl::LineChart rtt{{"THE HONEST SESSION'S ROUND TRIP, WITH THE FUZZ AND WITHOUT",
                     std::to_string(f.datagrams) + " datagrams over " + lab.arg("seconds") + " s",
                     "t (s)", "host's mean rtt (ms)"},
                    {{"no fuzz", clean.t_s, clean.series_rtt_ms}},
                    {},
                    {},
                    {},
                    false,
                    false,
                    false};
  for (const bool spoof : {false, true}) {
    if (spoof && lab.arg("spoof") != "1") break;
    f.spoof = spoof;
    const gn::FuzzReport r = gn::run_fuzz(f);
    const gn::TrafficReport& s = r.session;
    const bool whole = s.events_up.delivered == s.events_up.sent &&
                       s.events_down.delivered == s.events_down.sent &&
                       s.events_up.out_of_order + s.events_down.out_of_order == 0;
    table.rows.push_back(
        {spoof ? "strangers and spoofed peers" : "strangers", std::to_string(r.sent),
         std::to_string(r.parsed), std::to_string(s.host_foreign), std::to_string(s.host_strangers),
         std::to_string(s.host_bad_keys), std::to_string(s.host_bad_cookies),
         std::to_string(s.ended_connected) + "/" + std::to_string(f.session.clients),
         std::to_string(s.dropped), whole ? "yes" : "NO",
         r.honest ? "yes" : std::string("NO: ") + r.why, hex(r.hash)});
    const std::string k = spoof ? "spoofed-" : "";
    lab.metric(k + "honest", r.honest ? 1 : 0, "bool");
    lab.metric(k + "strangers", static_cast<double>(s.host_strangers), "count");
    if (!spoof) {
      for (int i = 0; i < gn::kFuzzKinds; i++) {
        kinds.columns.emplace_back(gn::fuzz_kind_name(i));
        kinds.rows[0].push_back(std::to_string(r.by_kind[i]));
      }
      if (!r.honest)
        lab.fail(std::string("the fuzz from strangers broke the honest session: ") + r.why,
                 "A datagram from an address with no peer must change nothing: read node_update's "
                 "receive path in engine/net/node.cpp, then run tests/net_node_test.cpp; skill: "
                 "net.");
    }
    rtt.traces.push_back(
        {spoof ? "fuzz, spoofed too" : "fuzz from strangers", s.t_s, s.series_rtt_ms});
  }
  lab.table(std::move(table));
  lab.table(std::move(kinds));
  lab.note(
      "Every fuzzed datagram is also run through the parser on its own (read_packet_start, the "
      "handshake or the payload header and every message, describe_packet); parsed: those that "
      "began as a packet of some type. host foreign: datagrams not of the game's protocol; "
      "strangers: of it, from an address with no peer; bad keys: a payload or disconnect with the "
      "wrong session key; bad cookies: a response with a cookie the host did not make. honest: "
      "every client connected at the end, none dropped, every reliable message whole, no slot "
      "given away. Spoofed packets come from a peer's own address with that peer's key: until "
      "payloads are authenticated (M17i) a spoofer on the path can break a connection, and this "
      "row says how often.");
  const gl::Chart c = gl::line_chart(rtt);
  const std::string svg = gl::to_svg(c.plot);
  lab.attach("rtt.svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({"rtt",
            c.title,
            "The host's mean round trip to its honest clients through the session, without the "
            "fuzz and with it.",
            "the fuzz from strangers' line on the clean line: the fuzz rides other links, and a "
            "node that drops what is not its peers' changes nothing for them.",
            {{"datagrams", lab.arg("datagrams")},
             {"clients", lab.arg("clients")},
             {"preset", lab.arg("preset")},
             {"seed", lab.arg("seed")}}},
           gl::to_picture(c.plot));
}

const gl::LabSpec kSpec{
    "net-fuzz",
    gl::LabKind::sim,
    "engine",
    "engine/net (stress_fuzz, node, packet)",
    "net",
    "Do random and mutated datagrams beside an honest session leave the parser and the session "
    "unharmed?",
    {
        {"datagrams", "1000000", "fuzzed datagrams over the session"},
        {"clients", "4", "honest clients (1 to 32)"},
        {"seconds", "10", "how long the honest session sends"},
        {"preset", "broadband", "every link's conditions"},
        {"spoof", "1", "also run the fuzz spoofed from the peers' addresses (1) or not (0)"},
        {"seed", "1", "the network's and the fuzz's stream"},
    },
    "--datagrams=20000 --seconds=3",
    run,
};
GE_LAB(kSpec);

}  // namespace
