// The lockstep trial's two games, its mesh and what it measures (net/lockstep_trial.hpp).
//
// ge:summary The lockstep trial's strategy and fighting games, the mesh, and the measures
// ge:keywords net lockstep trial strategy fighting units orders pads mesh measure
// ge:skill net
// ge:test tests/net_lockstep_test.cpp
#include "net/lockstep_trial.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include "core/prng.hpp"
#include "net/node.hpp"
#include "net/sim_network.hpp"
#include "net/transport.hpp"

namespace ge::net {

namespace {

uint64_t fnv(uint64_t h, uint64_t v) {
  for (int i = 0; i < 8; i++) h = (h ^ ((v >> (8 * i)) & 0xff)) * 0x100000001b3ull;
  return h;
}

constexpr int kUnits = 16;    // a strategy seat's units
constexpr int kOrder = 9;     // u8 unit, i16 x, i16 z, u32 the update it was given on
constexpr int kField = 4000;  // the strategy map is [-kField, kField] decimetres a side

struct Unit {
  int32_t x = 0, z = 0, tx = 0, tz = 0;
};

struct Fighter {
  int32_t x = 0, vx = 0, health = 1000;
  int32_t cooldown = 0;
  uint8_t pad = 0;  // the last pad state, for the latency of a change
};

// Both games in one: a peer's whole world, its seat and its measures.
struct World {
  LockstepGameKind kind = LockstepGameKind::strategy;
  int seats = 0, seat = 0;
  std::vector<Unit> units;        // seats x kUnits
  std::vector<Fighter> fighters;  // seats
  std::vector<std::vector<Fighter>> saved;
  uint32_t tick = 0;  // the update this peer is on, for the latency of its own inputs
  int plant_step = -1, plant_seat = -1;
  uint32_t planted_tick = 0;
  std::vector<double>* latency = nullptr;
  double tick_ms = 0;
  uint32_t pad_given = 0;  // fighting: the update this seat's pad last changed on
  uint8_t pad_now = 0;
  uint32_t logged = 0;  // steps whose latencies are counted: a rollback's rerun counts none
};

void step_strategy(World& w, uint32_t step, const uint8_t* const* inputs, const uint8_t* sizes) {
  for (int k = 0; k < w.seats; k++) {
    for (int at = 0; at + kOrder <= sizes[k]; at += kOrder) {
      const uint8_t* o = inputs[k] + at;
      int16_t x, z;
      uint32_t given;
      std::memcpy(&x, o + 1, 2);
      std::memcpy(&z, o + 3, 2);
      std::memcpy(&given, o + 5, 4);
      Unit& u = w.units[static_cast<size_t>(k) * kUnits + o[0] % kUnits];
      u.tx = x;
      u.tz = z;
      if (k == w.seat && w.latency && w.tick >= given && step >= w.logged)
        w.latency->push_back((w.tick - given) * w.tick_ms);
    }
  }
  for (Unit& u : w.units) {  // 2 decimetres a step toward the target on each axis
    u.x += std::clamp(u.tx - u.x, -2, 2);
    u.z += std::clamp(u.tz - u.z, -2, 2);
  }
  if (static_cast<int>(step) == w.plant_step && w.seat == w.plant_seat) {
    w.units[0].x += 1;
    if (!w.planted_tick) w.planted_tick = w.tick;
  }
}

void step_fighting(World& w, uint32_t step, const uint8_t* const* inputs, const uint8_t* sizes) {
  for (int k = 0; k < w.seats; k++) {
    Fighter& f = w.fighters[static_cast<size_t>(k)];
    const uint8_t pad = sizes[k] ? inputs[k][0] : 0;
    const int dir = (pad & 1) ? -1 : (pad & 2) ? 1 : 0;
    f.vx = std::clamp(f.vx + dir * 3 - f.vx / 8, -40, 40);
    f.x = std::clamp(f.x + f.vx, -3000, 3000);
    if (f.cooldown) f.cooldown--;
    if ((pad & 4) && !f.cooldown) {
      f.cooldown = 20;
      for (int j = 0; j < w.seats; j++) {
        Fighter& o = w.fighters[static_cast<size_t>(j)];
        if (j != k && o.x - f.x < 300 && f.x - o.x < 300) o.health -= 50;
      }
    }
    if (k == w.seat && w.latency && pad != f.pad && pad == w.pad_now && w.tick >= w.pad_given &&
        step >= w.logged)
      w.latency->push_back((w.tick - w.pad_given) * w.tick_ms);
    f.pad = pad;
  }
  if (static_cast<int>(step) == w.plant_step && w.seat == w.plant_seat) {
    w.fighters[0].health += 1;
    if (!w.planted_tick) w.planted_tick = w.tick;
  }
}

void game_step(void* ctx, uint32_t step, const uint8_t* const* inputs, const uint8_t* sizes) {
  World& w = *static_cast<World*>(ctx);
  if (w.kind == LockstepGameKind::strategy)
    step_strategy(w, step, inputs, sizes);
  else
    step_fighting(w, step, inputs, sizes);
  w.logged = std::max(w.logged, step + 1);
}

uint64_t game_digest(void* ctx) {
  const World& w = *static_cast<const World*>(ctx);
  uint64_t h = 0xcbf29ce484222325ull;
  for (const Unit& u : w.units)
    h = fnv(fnv(h, static_cast<uint32_t>(u.x)), static_cast<uint32_t>(u.z));
  for (const Fighter& f : w.fighters)
    h = fnv(fnv(fnv(h, static_cast<uint32_t>(f.x)), static_cast<uint32_t>(f.vx)),
            static_cast<uint32_t>(f.health));
  return h;
}

void game_save(void* ctx, int slot) {
  World& w = *static_cast<World*>(ctx);
  w.saved[static_cast<size_t>(slot)] = w.fighters;
}
void game_load(void* ctx, int slot) {
  World& w = *static_cast<World*>(ctx);
  w.fighters = w.saved[static_cast<size_t>(slot)];
}

NodeConfig node_config(int seats, uint64_t seed) {
  NodeConfig c;
  c.protocol = protocol_id("net-lockstep", 1);
  c.accept = true;
  c.max_peers = std::max(1, seats - 1);
  c.channels.count = 1;
  c.channels.delivery[0] = Delivery::unreliable;
  c.timeout_us = 5000000;
  c.seed = seed;
  return c;
}

struct Mate {
  Transport t;
  Node n;
  World w;
  Lockstep ls;
  core::Rng hand{0};
  uint32_t caught_tick = 0;
};

}  // namespace

LockstepTrial run_lockstep_trial(const LockstepTrialConfig& k) {
  LockstepTrial o;
  const int seats = std::clamp(k.seats, 1, kMaxSeats);
  const int hz = std::max(1, k.tick_hz);
  const int64_t tick_us = 1000000 / hz;
  SimNetwork net = make_sim_network(k.seed, k.link);
  std::vector<std::unique_ptr<Mate>> ps;
  for (int i = 0; i < seats; i++) {
    auto p = std::make_unique<Mate>();
    const auto id = static_cast<uint32_t>(i + 1);
    open_sim_transport(p->t, net, sim_address(id, 1));
    node_init(p->n, node_config(seats, uint64_t{k.seed} * 1000 + id), p->t);
    for (int j = 0; j < i; j++) node_connect(p->n, sim_address(static_cast<uint32_t>(j + 1), 1), 0);
    ps.push_back(std::move(p));
  }
  // The lobby's last moment: the mesh comes up before the first step.
  int64_t now = 0;
  for (int t = 0; t < 20 * hz; t++) {
    now += tick_us;
    sim_advance(net, now);
    bool all = true;
    for (auto& p : ps) {
      node_update(p->n, now);
      node_flush(p->n, now);
      all = all && node_connected_count(p->n) == seats - 1;
    }
    if (all) break;
  }
  for (auto& p : ps)
    if (node_connected_count(p->n) == seats - 1) o.connected++;

  LockstepConfig lc = k.lockstep;
  lc.seats = seats;
  lc.channel = 0;
  if (k.game == LockstepGameKind::fighting) {
    lc.mode = LockstepMode::rollback;
    lc.turn_steps = 1;
  } else {
    lc.mode = LockstepMode::lockstep;
  }
  const double tick_ms = 1000.0 / hz;
  for (int i = 0; i < seats; i++) {
    Mate& p = *ps[static_cast<size_t>(i)];
    p.w.kind = k.game;
    p.w.seats = seats;
    p.w.seat = i;
    p.w.units.resize(static_cast<size_t>(seats) * kUnits);
    for (size_t u = 0; u < p.w.units.size(); u++) {  // each seat's units in a row of their own
      p.w.units[u].x = p.w.units[u].tx = static_cast<int32_t>(u % kUnits) * 100 - 800;
      p.w.units[u].z = p.w.units[u].tz = static_cast<int32_t>(u / kUnits) * 400 - 600;
    }
    p.w.fighters.resize(k.game == LockstepGameKind::fighting ? static_cast<size_t>(seats) : 0);
    for (size_t f = 0; f < p.w.fighters.size(); f++)
      p.w.fighters[f].x = static_cast<int32_t>(f) * 600 - 300;
    p.w.saved.resize(static_cast<size_t>(lc.max_rollback) + 1);
    p.w.plant_step = k.plant_step;
    p.w.plant_seat = k.plant_seat;
    p.w.latency = &o.latency_ms;
    p.w.tick_ms = tick_ms;
    lc.seat = i;
    LockstepGame g;
    g.step = game_step;
    g.digest = game_digest;
    g.save = game_save;
    g.load = game_load;
    g.ctx = &p.w;
    lockstep_init(p.ls, lc, g, p.n);
    p.hand = core::make_rng(k.seed * 977 + static_cast<uint32_t>(i + 1));
  }

  const auto ticks = static_cast<int>(k.seconds * hz);
  for (int t = 1; t <= ticks; t++) {
    now += tick_us;
    sim_advance(net, now);
    for (auto& pp : ps) {
      Mate& p = *pp;
      p.w.tick = static_cast<uint32_t>(t);
      node_update(p.n, now);
      lockstep_receive(p.ls);
      if (k.game == LockstepGameKind::strategy) {
        if (core::chance(p.hand, 0.05)) {  // about three orders a second
          uint8_t order[kOrder];
          order[0] = static_cast<uint8_t>(core::int_in(p.hand, 0, kUnits - 1));
          const auto x = static_cast<int16_t>(core::int_in(p.hand, -kField, kField));
          const auto z = static_cast<int16_t>(core::int_in(p.hand, -kField, kField));
          const auto given = static_cast<uint32_t>(t);
          std::memcpy(order + 1, &x, 2);
          std::memcpy(order + 3, &z, 2);
          std::memcpy(order + 5, &given, 4);
          lockstep_input(p.ls, order, sizeof order);
        }
      } else if (core::chance(p.hand, 0.12)) {  // a pad that changes every eight steps or so
        uint8_t pad = static_cast<uint8_t>(core::int_in(p.hand, 0, 7));
        if (pad == p.w.pad_now) pad = static_cast<uint8_t>(pad ^ 4);
        p.w.pad_now = pad;
        p.w.pad_given = static_cast<uint32_t>(t);
        lockstep_set_input(p.ls, &pad, 1);
      }
      const uint64_t rollbacks = p.ls.stats.rollbacks, resim = p.ls.stats.resimulated;
      lockstep_update(p.ls);
      if (p.ls.stats.rollbacks != rollbacks)
        o.depths.push_back(static_cast<double>(p.ls.stats.resimulated - resim));
      if (p.ls.desync.found && !p.caught_tick) p.caught_tick = static_cast<uint32_t>(t);
      node_flush(p.n, now);
    }
    if (t % hz == 0) {
      o.t_s.push_back(static_cast<double>(t) / hz);
      o.steps.push_back(static_cast<double>(ps[0]->ls.step));
    }
  }

  const double seconds = static_cast<double>(ticks) / hz;
  uint32_t common = ~0u;
  uint32_t planted = 0;
  uint32_t first_caught = 0;
  o.pace = 1e9;
  for (auto& p : ps) {
    const LockstepStats& s = p->ls.stats;
    o.pace = std::min(o.pace, static_cast<double>(s.steps) / static_cast<double>(s.updates));
    o.stall_share += static_cast<double>(s.stalls) / static_cast<double>(s.updates) / seats;
    o.payload_kbps += static_cast<double>(s.bytes) * 8 / 1000 / seconds / seats;
    uint64_t wire = 0;
    for (const auto& peer : p->n.peers) wire += peer.conn.stats.bytes_sent;
    o.up_kbps += static_cast<double>(wire) * 8 / 1000 / seconds / seats;
    o.rollbacks += s.rollbacks;
    o.resimulated += s.resimulated;
    o.mispredicted += s.mispredicted;
    o.deepest = std::max(o.deepest, s.deepest);
    common = std::min(common, p->ls.final);
    if (p->w.planted_tick) planted = p->w.planted_tick;
    if (p->ls.desync.found) {
      if (!o.caught || p->ls.desync.step < o.caught_step) o.caught_step = p->ls.desync.step;
      o.caught = true;
      if (!first_caught || p->caught_tick < first_caught) first_caught = p->caught_tick;
    }
  }
  if (o.caught && planted && first_caught >= planted)
    o.caught_after_ms = (first_caught - planted) * tick_ms;
  uint64_t h = 0xcbf29ce484222325ull;
  o.agree = common > 0 && common != ~0u;
  if (o.agree) {
    o.common = common - 1;
    const LockstepSeat& first = ps[0]->ls.seats[static_cast<size_t>(0)];
    const uint64_t want = first.digests[o.common % first.digests.size()];
    for (auto& p : ps) {
      const LockstepSeat& me = p->ls.seats[static_cast<size_t>(p->ls.config.seat)];
      const uint64_t d = me.digests[o.common % me.digests.size()];
      o.agree = o.agree && d == want;
      h = fnv(fnv(fnv(h, p->ls.step), d), p->ls.stats.stalls);
    }
  }
  o.hash = fnv(h, o.rollbacks);
  return o;
}

}  // namespace ge::net
