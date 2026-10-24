// PRESSURE TESTS: the network pushed until it breaks, on the simulated network and its virtual
// clock, so an hour of play runs in seconds and every run is a pure function of its plan. Four
// kinds, each over the fake-traffic session (traffic.hpp):
//
// - RAMPS raise one knob of the links (loss, burst, latency, jitter, or the bandwidth taken
//   down) step by step until the session breaks a bound (`StressBounds`: inputs too late,
//   snapshots too few, a reliable message lost or too late, a connection dropped), then bisect
//   between the last step that held and the first that broke. `ge net stress` prints each knob's
//   breaking point for a mix.
// - CHAOS scripts change the links while the session runs (a link flapping, a partition and its
//   healing, the host lost, the host lost and started again, a NAT rebinding clients' ports,
//   clocks running fast and slow, a loss burst, the bandwidth collapsing); clients that lose
//   their connection try again, and every script names how it must end: every client connected,
//   or every client timed out cleanly, never stuck between, with no host slot left behind.
// - A SOAK runs a session for hours of virtual time with the latencies summed rather than kept,
//   and holds what the network and the nodes keep (MEMORY) flat, every reliable message
//   delivered in order across the packet sequence's and the message ids' wraps.
// - The FUZZER puts random and mutated datagrams on the wire beside an honest session (copies of
//   the packets in flight, bits flipped, cut short, run long, spliced, a valid header over
//   garbage), from strangers' addresses or spoofed from the peers', and runs every one through the
//   packet parser as well; the honest session must not notice.
//
// ge:summary Pressure tests: ramps to the breaking point, chaos scripts, soaks, the fuzzer
// ge:keywords net stress pressure ramp breaking-point chaos partition flap host-lost rebind
// ge:keywords clock-skew loss-burst bandwidth-collapse soak memory sequence-wrap fuzz fuzzer
// ge:keywords mutate datagram parser robustness reconnect timeout bounds
// ge:skill net
// ge:test tests/net_stress_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/traffic.hpp"

namespace ge::net {

// RAMPS ---------------------------------------------------------------------------------------

enum class StressKnob : uint8_t { loss, burst, latency, jitter, bandwidth };
const char* knob_name(StressKnob k);
bool stress_knob(std::string_view name, StressKnob& out);
const std::vector<StressKnob>& stress_knobs();

// What a session must keep to hold.
struct StressBounds {
  double input_p95_ms = 250;     // an input's one-way latency, 95th percentile
  double snapshot_share = 0.85;  // snapshots delivered of those sent
  double reliable_p95_ms = 1500;
  bool reliable_whole = true;  // every reliable message delivered, in order
  bool no_drops = true;        // every client connected, none dropped
};

// The first bound `r` breaks, or nullptr when it holds.
const char* stress_breaks(const TrafficReport& r, const TrafficPlan& plan, const StressBounds& b);

struct RampPoint {
  double value = 0;
  const char* broke = nullptr;  // null: held
  double input_p95_ms = 0, snapshot_share = 0, reliable_p95_ms = 0;
  uint64_t digest = 0;
};

struct Ramp {
  StressKnob knob = StressKnob::loss;
  bool found = false;  // some step broke
  double held = 0;     // the worst value that held
  double broke = 0;    // the mildest value that broke
  const char* why = nullptr;
  std::vector<RampPoint> points;  // in the order run
  uint64_t hash = 0;
};

struct RampPlan {
  StressKnob knob = StressKnob::loss;
  int steps = 8;   // coarse steps from the knob's start to its end
  int refine = 4;  // bisections between the last held and the first broken step
  StressBounds bounds;
};

// The knob's range: loss 0..0.6, burst 1..240 at 5% loss (geometric), latency 0..1000 ms, jitter
// 0..600 ms, bandwidth 4000 down to 16 kbit/s (geometric).
void knob_range(StressKnob k, double& from, double& to);
// Set the knob on `c`.
void apply_knob(StressKnob k, double value, LinkConditions& c);
Ramp run_ramp(const TrafficPlan& base, const RampPlan& plan);

// CHAOS ---------------------------------------------------------------------------------------

// How a script must end.
enum class ChaosEnd : uint8_t { all_connected, all_timed_out };

struct ChaosScript {
  std::string name;
  std::string what;  // the script in words
  std::vector<ChaosStep> steps;
  ChaosEnd end = ChaosEnd::all_connected;
  bool keeps_connections = false;  // nobody should even drop (the reliable streams stay whole)
};

// flap, partition, host-lost, host-restart, rebind, skew, burst, collapse.
const std::vector<std::string>& chaos_names();
// The steps are made for `base`'s clients and conditions.
bool chaos_script(std::string_view name, const TrafficPlan& base, ChaosScript& out);
// The plan a script runs on: `base` with its steps, the reconnect tries, and the time it needs.
TrafficPlan chaos_plan(const TrafficPlan& base, const ChaosScript& script);
// Null when the run ended as the script must; otherwise what went wrong.
const char* chaos_fails(const TrafficReport& r, const TrafficPlan& plan, const ChaosScript& script);

// SOAK ----------------------------------------------------------------------------------------

struct SoakReport {
  TrafficReport traffic;
  double held_kb_start = 0;  // the most held over the first half (past a warm-up tenth)
  double held_kb_end = 0;    // the most held over the second half
  bool flat = false;         // the second half's peak within 5% and 16 KB of the first's
  uint64_t seq_wraps = 0;    // times the host's packet sequence went round, the fewest of its peers
  uint64_t id_wraps = 0;     // times a client's reliable message ids went round, the fewest
  bool reliable_whole = false;
};

// A soak's mix: the racing mix with events at 40 Hz, so the reliable ids wrap within the hour.
TrafficMix soak_mix();
SoakReport run_soak(TrafficPlan plan);

// FUZZ ----------------------------------------------------------------------------------------

struct FuzzPlan {
  TrafficPlan session;          // the honest session beside the fuzz
  uint64_t datagrams = 100000;  // put on the wire over the session's sending
  bool spoof = false;           // also send mutated packets from the peers' own addresses
  uint32_t seed = 1;
};

struct FuzzReport {
  TrafficReport session;
  uint64_t sent = 0;                   // fuzzed datagrams put on the wire
  uint64_t by_kind[8] = {};            // FUZZ_KINDS' counts
  uint64_t parsed = 0, described = 0;  // started as a packet, and described whole, by the parser
  bool honest = false;                 // the honest session as it would be with no fuzz
  const char* why = nullptr;           // when it is not
  uint64_t hash = 0;
};

// random, flip, cut, long, splice, header, request, payload.
const char* fuzz_kind_name(int k);
constexpr int kFuzzKinds = 8;
FuzzReport run_fuzz(const FuzzPlan& plan);

}  // namespace ge::net
