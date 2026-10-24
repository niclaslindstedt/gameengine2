// engine/net/lockstep.hpp over the simulated network: four strategy peers in turns stay in
// lockstep under every condition preset; a desync planted on one step is caught on that step, in
// lockstep and in rollback; two fighting peers roll back no deeper than allowed and agree; a peer
// that waits names the seat it waits on; the config is checked; a session replays to the bit.
//
// ge:summary Test: lockstep turns and rollback: agreement per preset, a planted desync caught
// ge:keywords test net lockstep rollback desync digest stall waiting preset determinism
// ge:skill net testing
#include "net/lockstep.hpp"

#include <memory>
#include <string>

#include "check.hpp"
#include "net/lockstep_trial.hpp"
#include "net/sim_network.hpp"
#include "net/transport.hpp"

using namespace ge::net;

namespace {

LockstepTrialConfig strategy(const char* preset, double seconds = 10) {
  LockstepTrialConfig c;
  c.game = LockstepGameKind::strategy;
  c.seats = 4;
  c.seconds = seconds;
  conditions_preset(preset, c.link);
  return c;
}

LockstepTrialConfig fighting(const char* preset, double seconds = 10) {
  LockstepTrialConfig c;
  c.game = LockstepGameKind::fighting;
  c.seats = 2;
  c.seconds = seconds;
  c.lockstep.delay = 1;
  c.lockstep.max_rollback = 8;
  conditions_preset(preset, c.link);
  return c;
}

void noop_step(void*, uint32_t, const uint8_t* const*, const uint8_t*) {}
uint64_t zero_digest(void*) { return 0; }

}  // namespace

TEST(lockstep_four_peers_agree_under_every_preset) {
  for (const std::string& name : preset_names()) {
    const LockstepTrial o = run_lockstep_trial(strategy(name.c_str()));
    CHECK_MSG(o.connected == 4, (name + ": the mesh did not come up").c_str());
    CHECK_MSG(o.agree, (name + ": the peers' states differ").c_str());
    CHECK_MSG(!o.caught, (name + ": a desync was seen with none planted").c_str());
    CHECK_MSG(o.common > 300, (name + ": the session hardly moved").c_str());
    CHECK_MSG(!o.latency_ms.empty(), (name + ": no order was felt").c_str());
  }
}

TEST(lockstep_on_a_lan_never_waits_and_an_order_takes_its_delay) {
  const LockstepTrial o = run_lockstep_trial(strategy("lan"));
  CHECK(o.stall_share < 0.001);
  CHECK(o.pace > 0.999);
  // Two turns of six steps at 60 Hz, plus up to the rest of the turn the order was given in.
  for (double ms : o.latency_ms) CHECK(ms > 199 && ms < 284);
}

TEST(lockstep_a_planted_desync_is_caught_on_its_step) {
  for (const char* preset : {"lan", "wifi", "bad"}) {
    LockstepTrialConfig c = strategy(preset, 8);
    c.plant_step = 200;
    c.plant_seat = 2;
    const LockstepTrial o = run_lockstep_trial(c);
    CHECK(o.caught);
    CHECK(o.caught_step == 200);
    CHECK(o.caught_after_ms > 0 && o.caught_after_ms < 2000);
  }
}

TEST(rollback_peers_agree_and_roll_back_no_deeper_than_allowed) {
  for (const char* preset : {"lan", "wifi", "mobile", "bad"}) {
    const LockstepTrial o = run_lockstep_trial(fighting(preset));
    CHECK(o.connected == 2);
    CHECK(o.agree);
    CHECK(!o.caught);
    CHECK(o.deepest <= 8);
    if (std::string(preset) != "lan") CHECK(o.rollbacks > 0);
  }
  // On a LAN a pad is felt a step after it is pressed: the input delay, and nothing to wait for.
  const LockstepTrial lan = run_lockstep_trial(fighting("lan"));
  CHECK(lan.stall_share < 0.001);
  for (double ms : lan.latency_ms) CHECK(ms < 17);
}

TEST(rollback_a_planted_desync_is_caught_on_its_step) {
  LockstepTrialConfig c = fighting("wifi", 6);
  c.plant_step = 150;
  c.plant_seat = 0;
  const LockstepTrial o = run_lockstep_trial(c);
  CHECK(o.caught);
  CHECK(o.caught_step == 150);
}

TEST(lockstep_a_peer_that_waits_names_the_seat) {
  SimNetwork net = make_sim_network(3, LinkConditions{});
  Transport ta, tb;
  open_sim_transport(ta, net, sim_address(1, 1));
  open_sim_transport(tb, net, sim_address(2, 1));
  NodeConfig nc;
  nc.protocol = protocol_id("lockstep-test", 1);
  nc.accept = true;
  nc.max_peers = 1;
  nc.channels.count = 1;
  nc.channels.delivery[0] = Delivery::unreliable;
  Node a, b;
  nc.seed = 1;
  node_init(a, nc, ta);
  nc.seed = 2;
  node_init(b, nc, tb);
  node_connect(b, sim_address(1, 1), 0);
  LockstepGame g;
  g.step = noop_step;
  g.digest = zero_digest;
  LockstepConfig c;
  c.seats = 2;
  c.turn_steps = 2;
  c.delay = 1;
  Lockstep la, lb;
  c.seat = 0;
  CHECK(lockstep_init(la, c, g, a));
  c.seat = 1;
  CHECK(lockstep_init(lb, c, g, b));
  int64_t now = 0;
  // Only seat 0 steps: it runs the empty first turn, then waits on seat 1's input for turn 1.
  for (int t = 0; t < 30; t++) {
    now += 16667;
    sim_advance(net, now);
    node_update(a, now);
    node_update(b, now);
    lockstep_receive(la);
    lockstep_update(la);
    node_flush(a, now);
    node_flush(b, now);
  }
  CHECK(la.step == 2);
  CHECK(lockstep_waiting(la) == 2u);
  CHECK(la.stats.stalls == 28);
  // Seat 1 starts: both run on, and seat 0 waits on no one.
  for (int t = 0; t < 30; t++) {
    now += 16667;
    sim_advance(net, now);
    node_update(a, now);
    node_update(b, now);
    lockstep_receive(la);
    lockstep_receive(lb);
    lockstep_update(la);
    lockstep_update(lb);
    node_flush(a, now);
    node_flush(b, now);
  }
  CHECK(la.step > 20);
  CHECK(lb.step > 20);
}

TEST(lockstep_init_refuses_what_cannot_work) {
  SimNetwork net = make_sim_network(1, LinkConditions{});
  Transport t;
  open_sim_transport(t, net, sim_address(1, 1));
  NodeConfig nc;
  nc.channels.count = 1;
  nc.channels.delivery[0] = Delivery::reliable;
  Node n;
  node_init(n, nc, t);
  LockstepGame g;
  g.step = noop_step;
  g.digest = zero_digest;
  Lockstep ls;
  LockstepConfig c;
  const char* why = nullptr;
  CHECK(!lockstep_init(ls, c, g, n, &why) && why);  // a reliable channel
  n.config.channels.delivery[0] = Delivery::unreliable;
  CHECK(lockstep_init(ls, c, g, n, &why));
  c.mode = LockstepMode::rollback;
  c.turn_steps = 1;
  CHECK(!lockstep_init(ls, c, g, n, &why));  // no save and load
  c.turn_steps = 3;
  c.seats = 33;
  CHECK(!lockstep_init(ls, c, g, n, &why));
}

TEST(lockstep_replays_to_the_bit) {
  LockstepTrialConfig c = strategy("bad", 6);
  const LockstepTrial a = run_lockstep_trial(c), b = run_lockstep_trial(c);
  CHECK(a.hash == b.hash);
  CHECK(a.latency_ms == b.latency_ms);
  LockstepTrialConfig f = fighting("mobile", 6);
  const LockstepTrial x = run_lockstep_trial(f), y = run_lockstep_trial(f);
  CHECK(x.hash == y.hash);
  CHECK(x.depths == y.depths);
}
