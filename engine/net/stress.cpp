// The ramps, the chaos scripts and the soak, each a run (or a search of runs) of the fake-traffic
// session; the bounds a session must keep and the verdicts.
//
// ge:summary Pressure tests' ramps and their bisection, the chaos scripts, the soak's verdict
// ge:keywords net stress ramp bisect breaking-point bounds chaos script soak memory flat wraps
// ge:skill net
// ge:test tests/net_stress_test.cpp
#include "net/stress.hpp"

#include <algorithm>

#include "core/libm/libm.hpp"

namespace ge::net {

namespace {

double p95(std::vector<double> v) {
  if (v.empty()) return 0;
  const size_t k = (v.size() * 95 + 99) / 100 - 1;
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

double share(const StreamTally& t) {
  return t.sent ? static_cast<double>(t.delivered) / static_cast<double>(t.sent) : 1;
}

bool whole(const StreamTally& t) { return t.delivered == t.sent && t.out_of_order == 0; }

struct NamedKnob {
  const char* name;
  StressKnob knob;
};
constexpr NamedKnob kKnobs[] = {{"loss", StressKnob::loss},
                                {"burst", StressKnob::burst},
                                {"latency", StressKnob::latency},
                                {"jitter", StressKnob::jitter},
                                {"bandwidth", StressKnob::bandwidth}};

uint64_t mix(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

}  // namespace

const char* knob_name(StressKnob k) {
  for (const NamedKnob& n : kKnobs) {
    if (n.knob == k) return n.name;
  }
  return "?";
}

bool stress_knob(std::string_view name, StressKnob& out) {
  for (const NamedKnob& n : kKnobs) {
    if (name == n.name) {
      out = n.knob;
      return true;
    }
  }
  return false;
}

const std::vector<StressKnob>& stress_knobs() {
  static const std::vector<StressKnob> all = [] {
    std::vector<StressKnob> v;
    for (const NamedKnob& n : kKnobs) v.push_back(n.knob);
    return v;
  }();
  return all;
}

const char* stress_breaks(const TrafficReport& r, const TrafficPlan& plan, const StressBounds& b) {
  const int want = std::min(plan.clients, plan.max_peers);
  if (b.no_drops && r.connected < want) return "a client never connected";
  if (b.no_drops && r.dropped > 0) return "a connection dropped";
  if (p95(r.inputs.latency_ms) > b.input_p95_ms) return "inputs too late";
  if (share(r.snapshots) < b.snapshot_share) return "snapshots lost";
  if (b.reliable_whole && (!whole(r.events_up) || !whole(r.events_down)))
    return "a reliable message lost";
  std::vector<double> rel = r.events_up.latency_ms;
  rel.insert(rel.end(), r.events_down.latency_ms.begin(), r.events_down.latency_ms.end());
  if (p95(std::move(rel)) > b.reliable_p95_ms) return "reliable messages too late";
  return nullptr;
}

void knob_range(StressKnob k, double& from, double& to) {
  switch (k) {
    case StressKnob::loss:
      from = 0, to = 0.6;
      break;
    case StressKnob::burst:
      from = 1, to = 240;
      break;
    case StressKnob::latency:
      from = 0, to = 1000;
      break;
    case StressKnob::jitter:
      from = 0, to = 600;
      break;
    case StressKnob::bandwidth:
      from = 4000, to = 16;
      break;
  }
}

void apply_knob(StressKnob k, double value, LinkConditions& c) {
  switch (k) {
    case StressKnob::loss:
      c.loss = value;
      break;
    case StressKnob::burst:
      c.loss = std::max(c.loss, 0.05);
      c.burst = value;
      break;
    case StressKnob::latency:
      c.latency_ms = value;
      break;
    case StressKnob::jitter:
      c.jitter_ms = value;
      break;
    case StressKnob::bandwidth:
      c.bandwidth_kbps = value;
      break;
  }
}

Ramp run_ramp(const TrafficPlan& base, const RampPlan& plan) {
  Ramp ramp;
  ramp.knob = plan.knob;
  double from = 0, to = 0;
  knob_range(plan.knob, from, to);
  // The bandwidth is taken down and the burst raised by equal ratios; every other knob by equal
  // steps.
  const bool geometric = plan.knob == StressKnob::bandwidth || plan.knob == StressKnob::burst;
  const auto between = [&](double a, double b, double t) {
    return geometric ? a * ge::core::libm::exp(ge::core::libm::log(b / a) * t) : a + (b - a) * t;
  };
  const auto run = [&](double value) {
    TrafficPlan p = base;
    apply_knob(plan.knob, value, p.conditions);
    p.keep_latencies = true;
    const TrafficReport r = run_traffic(p);
    RampPoint pt;
    pt.value = value;
    pt.broke = stress_breaks(r, p, plan.bounds);
    pt.input_p95_ms = p95(r.inputs.latency_ms);
    pt.snapshot_share = share(r.snapshots);
    std::vector<double> rel = r.events_up.latency_ms;
    rel.insert(rel.end(), r.events_down.latency_ms.begin(), r.events_down.latency_ms.end());
    pt.reliable_p95_ms = p95(std::move(rel));
    pt.digest = r.digest;
    ramp.points.push_back(pt);
    ramp.hash = mix(ramp.hash, r.digest);
    return pt.broke;
  };
  const int steps = std::max(1, plan.steps);
  double held = from;
  for (int i = 0; i <= steps; i++) {
    const double v = between(from, to, static_cast<double>(i) / steps);
    const char* why = run(v);
    if (!why) {
      held = v;
      continue;
    }
    ramp.found = true;
    ramp.held = held;
    ramp.broke = v;
    ramp.why = why;
    if (i == 0) break;  // the base already breaks
    for (int k = 0; k < plan.refine; k++) {
      const double mid = between(ramp.held, ramp.broke, 0.5);
      const char* w = run(mid);
      if (w) {
        ramp.broke = mid;
        ramp.why = w;
      } else {
        ramp.held = mid;
      }
    }
    break;
  }
  if (!ramp.found) ramp.held = held;
  return ramp;
}

namespace {

struct NamedScript {
  const char* name;
  const char* what;
};
constexpr NamedScript kScripts[] = {
    {"flap", "every link down for 1 s in every 2, from 2 s to 16 s"},
    {"partition", "half the clients cut off from 3 s to 11 s, then healed"},
    {"host-lost", "the host's process gone at 3 s, for good"},
    {"host-restart", "the host's process gone at 3 s and started again at 5 s"},
    {"rebind", "half the clients' NATs give them a new port at 4 s"},
    {"skew", "half the clients' clocks run 10% fast, the rest 10% slow"},
    {"burst", "every link loses everything for 400 ms at 4 s"},
    {"collapse", "every link's bandwidth falls to 24 kbit/s from 4 s to 8 s"},
};

}  // namespace

const std::vector<std::string>& chaos_names() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> v;
    for (const NamedScript& s : kScripts) v.emplace_back(s.name);
    return v;
  }();
  return names;
}

bool chaos_script(std::string_view name, const TrafficPlan& base, ChaosScript& out) {
  const NamedScript* found = nullptr;
  for (const NamedScript& s : kScripts) {
    if (name == s.name) found = &s;
  }
  if (!found) return false;
  out = ChaosScript{};
  out.name = found->name;
  out.what = found->what;
  const int n = std::min(base.clients, base.max_peers);
  const int half = std::max(1, n / 2);
  LinkConditions dead = base.conditions;
  dead.loss = 1;
  dead.burst = 1;
  dead.drop_every = 0;
  const auto step = [&](double at, ChaosKind kind, int first = 0, int count = -1) {
    ChaosStep s;
    s.at_s = at;
    s.kind = kind;
    s.first = first;
    s.count = count;
    s.link = dead;
    out.steps.push_back(s);
    return &out.steps.back();
  };
  if (name == "flap") {
    for (int t = 2; t < 16; t += 2) {
      step(t, ChaosKind::link);
      step(t + 1, ChaosKind::restore);
    }
    out.keeps_connections = true;
  } else if (name == "partition") {
    step(3, ChaosKind::link, 0, half);
    step(11, ChaosKind::restore, 0, half);
  } else if (name == "host-lost") {
    step(3, ChaosKind::host_down);
    out.end = ChaosEnd::all_timed_out;
  } else if (name == "host-restart") {
    step(3, ChaosKind::host_down);
    step(5, ChaosKind::host_up);
  } else if (name == "rebind") {
    step(4, ChaosKind::rebind, 0, half);
  } else if (name == "skew") {
    step(0, ChaosKind::skew, 0, half)->rate = 1.1;
    step(0, ChaosKind::skew, half)->rate = 0.9;
    out.keeps_connections = true;
  } else if (name == "burst") {
    step(4, ChaosKind::link);
    step(4.4, ChaosKind::restore);
    out.keeps_connections = true;
  } else if (name == "collapse") {
    step(4, ChaosKind::link)->link = base.conditions;
    out.steps.back().link.bandwidth_kbps = 24;
    step(8, ChaosKind::restore);
  }
  return true;
}

TrafficPlan chaos_plan(const TrafficPlan& base, const ChaosScript& script) {
  TrafficPlan p = base;
  p.chaos = script.steps;
  p.reconnect_tries = 3;
  p.reconnect_s = 1;
  // Long enough for a lost host's clients to time out and spend every try: 3 s + 5 s silent,
  // then three tries of a second's wait and a 5 s connect timeout.
  p.seconds = std::max(p.seconds, 20.0);
  p.drain_seconds = std::max(p.drain_seconds, 8.0);
  return p;
}

const char* chaos_fails(const TrafficReport& r, const TrafficPlan& plan,
                        const ChaosScript& script) {
  const int n = std::min(plan.clients, plan.max_peers);
  if (r.ended_stuck > 0) return "a client was stuck connecting at the end";
  if (script.end == ChaosEnd::all_connected && r.ended_connected < n)
    return "a client did not reconnect";
  if (script.end == ChaosEnd::all_timed_out && r.ended_timed_out < n)
    return "a client did not time out";
  if (r.host_zombies > 0) return "the host kept a slot for a client that was gone";
  if (script.keeps_connections && r.dropped > 0) return "a connection dropped";
  if (script.keeps_connections && (!whole(r.events_up) || !whole(r.events_down)))
    return "a reliable message lost";
  return nullptr;
}

TrafficMix soak_mix() {
  TrafficMix m;
  traffic_mix("racing", m);
  m.event_hz = 40;
  return m;
}

SoakReport run_soak(TrafficPlan plan) {
  SoakReport s;
  plan.keep_latencies = false;
  s.traffic = run_traffic(plan);
  const TrafficReport& r = s.traffic;
  const size_t count = r.series_held_kb.size();
  const size_t warm = count / 10, middle = count / 2;
  for (size_t i = warm; i < count; i++) {
    double& peak = i < middle ? s.held_kb_start : s.held_kb_end;
    peak = std::max(peak, r.series_held_kb[i]);
  }
  s.flat = s.held_kb_end <= s.held_kb_start * 1.05 + 16;
  s.seq_wraps = r.host_packets_min / 65536;
  const int n = std::max(1, std::min(plan.clients, plan.max_peers));
  s.id_wraps = r.events_up.sent / static_cast<uint64_t>(n) / 65536;
  s.reliable_whole = whole(r.events_up) && whole(r.events_down) && r.dropped == 0;
  return s;
}

}  // namespace ge::net
