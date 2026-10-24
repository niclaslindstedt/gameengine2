// FAKE TRAFFIC: a whole session run on the simulated network with no game behind it: a host and
// up to 32 clients connect, every client sends inputs (sequenced, 60 Hz) and events (reliable),
// the host sends each client snapshots (unreliable, 20 Hz) and events back, for a while, then
// everyone stops sending new messages and the links drain. Every message carries its stream's
// count and the time it was sent, so the report says exactly what arrived, how late, in what
// order and twice or not, beside what each link measured (round trip, loss, bandwidth) and what
// the wire did (`SimStats`). The `net-link` lab sweeps it over the condition presets, the tests
// hold it to its guarantees, and `ge net sim` (M17b) runs it from the command line.
//
// The run is a pure function of its plan: the same plan is the same report, bit for bit, and the
// `digest` says so in one number.
//
// CHAOS (stress.hpp names the scripts): a plan may carry a script of changes made to the session
// as it runs (a link's conditions changed and restored, the host lost and started again, a
// client's port rebound as a NAT does, a client's clock run fast or slow), and clients that
// lose their connection may try again (`reconnect_tries`). The report then says how each client
// ended: connected, timed out cleanly, or stuck between the two. A `hook` runs every tick with
// the network, for a caller that puts its own datagrams on the wire (the fuzzer).
//
// ge:summary Fake traffic: a host and clients over the simulated network, measured end to end
// ge:keywords net traffic fake-traffic generator load session simulate bots latency loss
// ge:keywords reliable-delivery in-order goodput bandwidth report digest stress mix
// ge:keywords traffic-mix action rpg world lockstep racing download bulk budget
// ge:skill net
// ge:test tests/net_traffic_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/capture.hpp"
#include "net/conditions.hpp"
#include "net/connection.hpp"
#include "net/node.hpp"
#include "net/sim_network.hpp"

namespace ge::net {

// The traffic's channels: 0 sequenced (inputs up), 1 unreliable (snapshots down), 2 reliable
// (events both ways), and with a bulk in the mix 3 reliable (big messages down, sent in pieces,
// at a lower priority than the rest).
constexpr int kTrafficInputs = 0;
constexpr int kTrafficSnapshots = 1;
constexpr int kTrafficEvents = 2;
constexpr int kTrafficBulk = 3;

struct TrafficMix {
  double input_hz = 60;  // each client to the host
  size_t input_bytes = 24;
  double snapshot_hz = 20;  // the host to each client
  size_t snapshot_bytes = 320;
  double event_hz = 4;  // each way, on every link
  size_t event_bytes = 48;
  double bulk_hz = 0;  // the host to each client: big messages (a map, a save), up to 64 KB
  size_t bulk_bytes = 0;
};

// One change a chaos script makes, at `at_s` into the run, to clients [first, first + count)
// (count -1: first to the last).
enum class ChaosKind : uint8_t {
  link,       // the conditions between the host and each client become `link`, both ways
  restore,    // back to the plan's conditions
  host_down,  // the host's process is gone: its socket closed, it reads and sends nothing
  host_up,    // a new host process on the same address, knowing nobody
  rebind,     // each client's socket moves to a new port, as a NAT rebinding does
  skew,       // each client's clock runs `rate` times as fast as the network's
};
const char* chaos_kind_name(ChaosKind k);

struct ChaosStep {
  double at_s = 0;
  ChaosKind kind = ChaosKind::link;
  int first = 0, count = -1;
  LinkConditions link;
  double rate = 1;
};

using TrafficHook = void (*)(void* ctx, SimNetwork& net, int64_t now_us, int64_t tick);

struct TrafficPlan {
  int clients = 8;
  int max_peers = 32;        // the host's slots; clients past it are denied
  double seconds = 10;       // sending
  double drain_seconds = 4;  // then nothing new, while the reliable messages finish
  double tick_hz = 60;       // how often every node updates and flushes
  uint32_t seed = 1;
  LinkConditions conditions;  // every link, both ways
  TrafficMix mix;
  double budget_kbps = 0;        // every connection's send budget (connection.hpp); 0: none
  bool capture = false;          // capture the host's transport
  double sample_s = 0.25;        // the series' step
  std::vector<ChaosStep> chaos;  // in order of at_s
  int reconnect_tries = 0;       // a client that loses its connection connects again, this often
  double reconnect_s = 1;        // after this long
  bool keep_latencies = true;    // every delivery's latency kept (a long soak keeps the sums only)
  TrafficHook hook = nullptr;    // every tick, after the network's clock moves
  void* hook_ctx = nullptr;
};

// One kind of message, summed over its streams.
struct StreamTally {
  uint64_t sent = 0, refused = 0, delivered = 0, out_of_order = 0, duplicated = 0;
  std::vector<double> latency_ms;  // each delivery, one way (TrafficPlan::keep_latencies)
  double latency_sum_ms = 0, latency_max_ms = 0;
};

struct TrafficReport {
  int connected = 0;  // clients that reached connected
  int denied = 0;     // clients refused (the host was full)
  int dropped = 0;    // connections lost before the end
  StreamTally inputs, snapshots, events_up, events_down, bulk;
  uint64_t bulk_broken = 0;  // big messages delivered but not whole (never, when all is well)
  // What the host's links measured at the end of the sending, averaged over its peers.
  double rtt_ms = 0, rtt_var_ms = 0, jitter_ms = 0, loss = 0;
  double loss_total = 0;  // the host's packets lost over the whole sending, of those settled
  double host_send_kbps = 0, host_recv_kbps = 0;  // mean over the sending, the host's total
  uint64_t resends = 0, dropped_unsent = 0, duplicates = 0, stale = 0;
  SimStats wire;
  std::vector<double> t_s, series_rtt_ms, series_loss;  // the host's mean over its peers
  std::vector<double> series_connected;                 // clients connected at each sample
  std::vector<double> series_held_kb;  // what the network and every node hold (MEMORY)
  // How the clients ended (CHAOS): connected, timed out (gave up, or never tried again), or
  // stuck (still connecting, or waiting to try again, when the run stopped).
  int ended_connected = 0, ended_timed_out = 0, ended_stuck = 0;
  int reconnects = 0;    // connections made again after one was lost
  double recover_s = 0;  // the longest a client went from losing its connection to regaining it
  int host_zombies = 0;  // the host's connected peers at the end with no client behind them
  uint64_t host_packets_min = 0;  // the fewest packets the host sent one of its peers
  uint64_t host_strangers = 0, host_bad_keys = 0, host_bad_cookies = 0, host_foreign = 0;
  Capture capture;  // empty unless the plan asked for it
  uint64_t digest = 0;
};

TrafficReport run_traffic(const TrafficPlan& plan);
// The protocol id the traffic's nodes speak.
uint32_t traffic_protocol();
// MEMORY: the bytes a network and a node hold now, by their buffers' capacities: the same each
// sample in a steady session, growing when something leaks or queues without bound.
size_t sim_held_bytes(const SimNetwork& net);
size_t node_held_bytes(const Node& node);

// Named mixes, one a shape of game (docs/networking.md § The shapes), for the labs and `ge net
// sim`: racing (the default numbers above), action (fast inputs, big snapshots), rpg, world (few
// inputs, a busy world), lockstep (inputs and turns, no snapshots), download (racing while each
// client is sent a 64 KB message every two seconds).
const std::vector<std::string>& mix_names();
bool traffic_mix(std::string_view name, TrafficMix& out);
// "inputs 60 Hz × 24 B, snapshots 20 Hz × 320 B, events 4 Hz × 48 B".
std::string describe_mix(const TrafficMix& m);

}  // namespace ge::net
