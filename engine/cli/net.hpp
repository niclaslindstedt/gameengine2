// `ge net`: the network's tools (docs/networking.md § The tools), over the engine's own wire
// format (engine/net). Each answers from a capture (a .pcap a lab attaches, `ge net sim` writes
// or a live node saves: net/capture.hpp) or from a session it runs itself:
//
//   ge net inspect <pcap>   every datagram decoded to one line: time, addresses, type,
//                           sequence, acks, each message's channel, id and size
//   ge net stats <pcap>     per connection, from the capturing node's side: packets and
//                           kilobits each way, round trip, loss each way, duplicates, reorders,
//                           resends; the local node's kbps, round trip and loss over time as a
//                           series file and its charts (`ge graph`)
//   ge net sim              fake-traffic clients and a host over chosen conditions (presets, a
//                           traffic mix, --link overrides): the same numbers the net-link lab
//                           prints, a digest a run, and a capture with --capture
//   ge net watch            a RUNNING game's or server's datagrams, from its live tap
//                           (net/tap.hpp) on a loopback port: per connection every second,
//                           then saved as a capture and measured as ge net stats measures it
//   ge net sweep            a replicated world (net/replicate_trial.hpp) run once a value of
//                           one knob (a rate, the interpolation, a link condition): bandwidth
//                           each way, view delay, corrections, rewind gaps; the cheapest value
//                           within reach of the best named
//   ge net stress           each link knob ramped until the session breaks a bound, bisected to
//                           its breaking point; --chaos, --soak and --fuzz add the other
//                           pressure tests (net/stress.hpp)
//
// No game and no build are needed: everything runs inside ge.
//
// ge:summary ge net inspect, stats, sim and sweep: captures read, traffic run, rates swept
// ge:keywords cli ge net network inspect stats sim pcap capture rtt loss bandwidth kbps
// ge:keywords fake-traffic conditions preset mix link wireshark multiplayer sweep rate
// ge:keywords update-rate cmd-rate replicate experiment knee watch tap live attach running
// ge:keywords stress ramp breaking-point chaos soak fuzz pressure
// ge:skill agent-interface net
// ge:test tests/cli_net_test.cpp
#pragma once

#include <string>
#include <vector>

#include "cli/commands.hpp"
#include "cli/outcome.hpp"
#include "net/conditions.hpp"

namespace ge::cli {

struct NetInspectOptions {
  std::string file;
  std::string peer;  // only datagrams to or from this address ("" for all)
  std::string type;  // only this packet type ("" for all)
  long long skip = 0;
  long long limit = 200;  // 0: every one
};

struct NetStatsOptions {
  std::string file;
  double bucket_ms = 250;
  std::string out;  // the series and charts' directory (default: <stem>-net/ beside the file)
  bool charts = true;
};

struct NetSimOptions {
  std::vector<std::string> presets;  // empty: every preset
  std::string mix = "racing";
  std::string link;        // overrides on every preset: "latency=40,jitter=10,loss=2%"
  bool stream = false;     // every link a stream (TCP): --transport=tcp
  double budget_kbps = 0;  // every connection's send budget
  int clients = 8;
  int max_peers = 32;
  double seconds = 10;
  double drain = 4;
  double tick_hz = 60;
  unsigned seed = 1;
  std::string capture;  // a .pcap path: the host's capture (one a preset when several)
};

struct NetWatchOptions {
  int port = 27950;                          // where the tap sends (net/tap.hpp: kTapPort)
  double seconds = 10;                       // how long to watch; 0: until --frames
  double every = 1;                          // a live line a connection this often, seconds
  long long frames = 0;                      // stop after this many datagrams (0: no limit)
  std::string save = "runs/net-watch.pcap";  // what was seen, read back by ge net stats
};

struct NetSweepOptions {
  std::string knob;                  // update-rate, cmd-rate, rate, interp, backup, tick-hz, or
                                     // a --link key (loss, latency, jitter, drop-every, ...)
  std::vector<std::string> values;   // the knob's values, one trial each
  std::vector<std::string> presets;  // empty: wifi
  std::string link;                  // overrides on every preset
  int clients = 16, npcs = 32;
  double seconds = 10;
  int tick_hz = 60, update_rate = 0, cmd_rate = 0, interp = 6, backup = 4;
  double rate = 0;
  unsigned seed = 1;
};

struct NetStressOptions {
  std::vector<std::string> knobs;  // empty: every knob
  std::string preset = "broadband";
  std::string mix = "racing";
  std::string link;  // overrides on the preset
  int clients = 8;
  double seconds = 6;
  int steps = 8, refine = 4;
  std::vector<std::string> chaos;  // scripts to run too ("all": every one)
  double soak = 0;                 // virtual seconds of soak (0: none)
  int soak_clients = 32;
  double fuzz = 0;  // fuzzed datagrams (0: none)
  unsigned seed = 1;
};

Outcome run_net_inspect(const NetInspectOptions& opts);
Outcome run_net_stress(const NetStressOptions& opts);
// "latency=40,loss=2%" onto `c`; the first bad key or value, or empty.
std::string apply_link(const std::string& spec, net::LinkConditions& c);
Outcome run_net_stats(const NetStatsOptions& opts);
Outcome run_net_sim(const NetSimOptions& opts);
Outcome run_net_sweep(const NetSweepOptions& opts);
// Listens for a live tap; a line a connection every `every` seconds on stderr, then the whole
// watch as ge net stats reads it.
Outcome run_net_watch(const NetWatchOptions& opts);
// A parsed `ge net <inspect|stats|sim>` line's flags read into the options, and run.
Outcome run_net(const Args& a);

}  // namespace ge::cli
