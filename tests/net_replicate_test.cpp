// engine/net/replicate.hpp over the simulated network: a server and its clients on a toy world of
// movers. With no loss a prediction never needs correcting; under loss it is corrected and still
// lands on the server's state; deltas are smaller than whole snapshots; interest hides the far;
// a byte budget starves some rows but every one arrives; a rewind is what the client drew; the
// whole exchange replays to the bit.
//
// ge:summary Test: replication's prediction, corrections, deltas, interest, budget and rewind
// ge:keywords test net replicate snapshot delta prediction reconcile interest budget rewind
// ge:skill net testing
#include "net/replicate.hpp"

#include <memory>
#include <vector>

#include "check.hpp"
#include "core/prng.hpp"

using namespace ge::net;

namespace {

constexpr int64_t kTickUs = 16667;  // 60 Hz

// The toy world: a mover's x and z, its heading (an angle) and its health, which only the game
// sets.
enum : uint8_t { kX, kZ, kHeading, kHealth, kFields };

void step_mover(void*, double* v, const uint8_t* input) {
  const auto dx = static_cast<int8_t>(input[0]), dz = static_cast<int8_t>(input[1]);
  v[kX] += dx * 0.05;
  v[kZ] += dz * 0.05;
  v[kHeading] += static_cast<int8_t>(input[2]) * 0.01;
}

Schema mover_schema() {
  Schema s;
  s.fields = kFields;
  s.spec[kX] = {-1024, 1024, 22, false};
  s.spec[kZ] = {-1024, 1024, 22, false};
  s.spec[kHeading] = {0, 6.283185307179586, 12, true};
  s.spec[kHealth] = {0, 100, 8, false};
  s.input_size = 3;
  s.step = step_mover;
  return s;
}

NodeConfig node_config(bool host, uint64_t seed) {
  NodeConfig c;
  c.protocol = protocol_id("replicate-test", 1);
  c.accept = host;
  c.channels.count = 2;
  c.channels.delivery[0] = Delivery::unreliable;
  c.channels.delivery[1] = Delivery::unreliable;
  c.seed = seed;
  return c;
}

struct End {
  Transport t;
  Node n;
};

struct World {
  SimNetwork net;
  End server_end;
  std::vector<std::unique_ptr<End>> client_ends;
  ReplicateServer server;
  std::vector<std::unique_ptr<ReplicateClient>> clients;
  std::vector<ge::core::Rng> hands;
  int64_t now = 0;
  bool moving = true;
  double spread = 0;  // where each new mover is put: client k at x = k * spread
};

std::unique_ptr<World> make_world(int clients, const LinkConditions& link,
                                  const ReplicateConfig& config, uint32_t seed = 1) {
  auto w = std::make_unique<World>();
  w->net = make_sim_network(seed, link);
  open_sim_transport(w->server_end.t, w->net, sim_address(0, 1));
  node_init(w->server_end.n, node_config(true, 100), w->server_end.t);
  replicate_server_init(w->server, mover_schema(), config, w->server_end.n);
  for (int k = 0; k < clients; k++) {
    auto e = std::make_unique<End>();
    const auto id = static_cast<uint32_t>(k + 1);
    open_sim_transport(e->t, w->net, sim_address(id, 1));
    node_init(e->n, node_config(false, 200 + id), e->t);
    const int server_peer = node_connect(e->n, sim_address(0, 1), 0);
    auto c = std::make_unique<ReplicateClient>();
    replicate_client_init(*c, mover_schema(), config, e->n, server_peer);
    w->client_ends.push_back(std::move(e));
    w->clients.push_back(std::move(c));
    w->hands.push_back(ge::core::make_rng(seed * 1000 + id));
  }
  return w;
}

void tick(World& w) {
  w.now += kTickUs;
  sim_advance(w.net, w.now);
  node_update(w.server_end.n, w.now);
  for (const Event& e : w.server_end.n.events) {
    if (e.type != EventType::connected) continue;
    double v[kFields] = {e.peer * w.spread, 0, 0, 0};
    replicate_spawn(w.server, e.peer, v);
  }
  replicate_server_receive(w.server);
  replicate_server_tick(w.server);
  node_flush(w.server_end.n, w.now);
  for (size_t k = 0; k < w.clients.size(); k++) {
    End& e = *w.client_ends[k];
    node_update(e.n, w.now);
    replicate_client_receive(*w.clients[k]);
    uint8_t input[3] = {};
    if (w.moving) {
      input[0] = static_cast<uint8_t>(ge::core::int_in(w.hands[k], -20, 20));
      input[1] = static_cast<uint8_t>(ge::core::int_in(w.hands[k], -20, 20));
      input[2] = static_cast<uint8_t>(ge::core::int_in(w.hands[k], -10, 10));
    }
    replicate_client_input(*w.clients[k], input);
    node_flush(e.n, w.now);
  }
}

void run(World& w, int ticks) {
  for (int i = 0; i < ticks; i++) tick(w);
}

// Stop moving and let every input and snapshot land.
void settle(World& w) {
  w.moving = false;
  run(w, 120);
}

double diff(const FieldSpec& f, double a, double b) {
  double d = a - b;
  if (f.angle) {
    const double range = f.max - f.min;
    while (d > range / 2) d -= range;
    while (d < -range / 2) d += range;
  }
  return d < 0 ? -d : d;
}

// The client's drawn own entity against the server's: the largest gap over the position fields.
double own_gap(const World& w, size_t k) {
  const ReplicateClient& c = *w.clients[k];
  const double* s = replicate_get(w.server, c.own);
  if (!s) return 1e9;
  double v[kMaxFields];
  replicate_client_predicted(c, v);
  const Schema schema = mover_schema();
  double worst = 0;
  for (int f = 0; f < kHealth; f++) {
    const double d = diff(schema.spec[f], v[f], s[f]);
    if (d > worst) worst = d;
  }
  return worst;
}

}  // namespace

TEST(replicate_quantize_round_trips_and_wraps_angles) {
  const FieldSpec x{-10, 10, 16, false};
  CHECK(quantize(x, -10) == 0);
  CHECK(quantize(x, 10) == 65535);
  CHECK(quantize(x, 99) == 65535);  // clamped
  CHECK(diff(x, dequantize(x, quantize(x, 3.25)), 3.25) <= 20.0 / 65535);
  const FieldSpec a{0, 360, 8, true};
  CHECK(quantize(a, 360) == 0);  // the end is the start
  CHECK(quantize(a, -90) == quantize(a, 270));
  CHECK(quantize(a, 720 + 45) == quantize(a, 45));
  // A value already on the grid stays put: the server's state is quantized every tick.
  const double on = dequantize(x, 12345);
  CHECK(quantize(x, on) == 12345);
}

TEST(replicate_with_no_loss_a_prediction_is_never_corrected) {
  LinkConditions link;
  link.latency_ms = 40;
  ReplicateConfig config;
  auto w = make_world(4, link, config);
  run(*w, 600);
  for (const auto& c : w->clients) {
    CHECK(c->own != kNoEntity);
    CHECK(c->stats.snapshots > 250);
    CHECK(c->stats.corrections == 0);
    CHECK(c->stats.dropped == 0);
  }
  CHECK(w->server.stats.repeated == 0);
  settle(*w);
  for (size_t k = 0; k < w->clients.size(); k++) CHECK(own_gap(*w, k) == 0);
}

TEST(replicate_under_loss_a_prediction_is_corrected_and_lands) {
  LinkConditions link;
  link.latency_ms = 50;
  link.jitter_ms = 10;
  link.loss = 0.1;
  link.burst = 2;
  ReplicateConfig config;
  auto w = make_world(4, link, config, 7);
  run(*w, 900);
  uint64_t corrections = 0;
  for (const auto& c : w->clients) {
    CHECK(c->own != kNoEntity);
    corrections += c->stats.corrections;
  }
  CHECK(corrections > 0);  // inputs lost past the redundancy, or late, were repeated
  settle(*w);
  // The correction is smoothed away: what is drawn ends on the server's state.
  for (size_t k = 0; k < w->clients.size(); k++) CHECK(own_gap(*w, k) < 1e-6);
}

TEST(replicate_deltas_are_smaller_than_whole_snapshots) {
  LinkConditions link;
  link.latency_ms = 20;
  ReplicateConfig config;
  auto w = make_world(32, link, config);
  w->spread = 3;
  run(*w, 120);
  const ReplicateStats before = w->server.stats;
  CHECK(before.full >= 32);
  // Moving: every row changes its position and heading but never its health.
  run(*w, 120);
  const ReplicateStats moving = w->server.stats;
  CHECK(moving.full == before.full);  // every client acks: no whole snapshot after the first
  const double whole = (32 * (18.0 + 64) / 8) + 19;  // every row new
  const double per_moving = static_cast<double>(moving.bytes - before.bytes) /
                            static_cast<double>(moving.snapshots - before.snapshots);
  CHECK(per_moving < whole);
  // Still: nothing changes, so a snapshot is its header and its count.
  settle(*w);
  const ReplicateStats still0 = w->server.stats;
  run(*w, 60);
  const ReplicateStats still = w->server.stats;
  const double per_still = static_cast<double>(still.bytes - still0.bytes) /
                           static_cast<double>(still.snapshots - still0.snapshots);
  CHECK(per_still <= 19);  // the 16-byte header and the 3-byte count
  for (const auto& c : w->clients) CHECK(c->stats.dropped == 0);
}

TEST(replicate_interest_hides_the_far) {
  LinkConditions link;
  link.latency_ms = 20;
  ReplicateConfig config;
  config.interest_x = kX;
  config.interest_z = kZ;
  config.interest_radius = 50;
  auto w = make_world(3, link, config);
  w->spread = 40;  // peer 0 at 0, 1 at 40, 2 at 80
  w->moving = false;
  run(*w, 120);
  // Each client is the server's peer slot in connect order; find each one's entity.
  std::vector<uint16_t> ids;
  for (const auto& c : w->clients) ids.push_back(c->own);
  for (uint16_t id : ids) CHECK(id != kNoEntity);
  double v[kMaxFields];
  // The first sees the second (40 away) but not the third (80).
  CHECK(replicate_client_sample(*w->clients[0], ids[1], v));
  CHECK(!replicate_client_sample(*w->clients[0], ids[2], v));
  // The middle one sees both.
  CHECK(replicate_client_sample(*w->clients[1], ids[0], v));
  CHECK(replicate_client_sample(*w->clients[1], ids[2], v));
  // Move the third next to the first: it comes into sight, and the second's row for the first
  // stays.
  double near[kFields] = {10, 0, 0, 0};
  replicate_set(w->server, ids[2], near);
  run(*w, 60);
  CHECK(replicate_client_sample(*w->clients[0], ids[2], v));
  CHECK(diff(mover_schema().spec[kX], v[kX], 10) < 1e-3);
  // And out of sight again: it is gone from the client's snapshots.
  double far[kFields] = {500, 0, 0, 0};
  replicate_set(w->server, ids[2], far);
  run(*w, 60);
  CHECK(!replicate_client_sample(*w->clients[0], ids[2], v));
}

TEST(replicate_a_budget_starves_rows_but_every_one_arrives) {
  LinkConditions link;
  link.latency_ms = 20;
  ReplicateConfig config;
  config.max_entities = 64;
  config.snapshot_budget = 120;
  auto w = make_world(2, link, config);
  w->moving = false;
  run(*w, 30);
  // Thirty movers the game moves, every one every tick.
  std::vector<uint16_t> movers;
  for (int i = 0; i < 30; i++) {
    double v[kFields] = {i * 2.0, 0, 0, 0};
    movers.push_back(replicate_spawn(w->server, -1, v));
  }
  for (int t = 0; t < 240; t++) {
    for (size_t i = 0; i < movers.size(); i++) {
      double v[kFields];
      const double* now = replicate_get(w->server, movers[i]);
      for (int f = 0; f < kFields; f++) v[f] = now[f];
      v[kZ] += 0.25;
      replicate_set(w->server, movers[i], v);
    }
    tick(*w);
  }
  CHECK(w->server.stats.starved > 0);
  CHECK(w->server.stats.bytes <= w->server.stats.snapshots * config.snapshot_budget);
  // Every mover reached both clients, and none is far behind the server.
  for (const auto& c : w->clients) {
    for (uint16_t id : movers) {
      double v[kMaxFields];
      CHECK(replicate_client_sample(*c, id, v));
      CHECK(replicate_get(w->server, id)[kZ] - v[kZ] < 0.25 * 60);
    }
  }
}

TEST(replicate_a_rewind_is_what_the_client_drew) {
  LinkConditions link;
  link.latency_ms = 60;
  ReplicateConfig config;
  auto w = make_world(1, link, config);
  w->moving = false;
  run(*w, 30);
  double start[kFields] = {0, 0, 0, 0};
  const uint16_t target = replicate_spawn(w->server, -1, start);
  int checked = 0;
  for (int t = 0; t < 120; t++) {
    double v[kFields];
    const double* now = replicate_get(w->server, target);
    for (int f = 0; f < kFields; f++) v[f] = now[f];
    v[kX] += 0.5;  // a steady line, so drawing between snapshots is exact
    replicate_set(w->server, target, v);
    tick(*w);
    const ReplicateClient& c = *w->clients[0];
    const uint32_t at = replicate_client_render_tick(c);
    double drawn[kMaxFields], judged[kMaxFields];
    if (t < 30 || !replicate_client_sample(c, target, drawn)) continue;
    // The server judges a hit at the tick the client said it drew, once its input arrives.
    CHECK(replicate_rewind(w->server, target, at, judged));
    CHECK(diff(mover_schema().spec[kX], drawn[kX], judged[kX]) < 1e-3);
    checked++;
  }
  CHECK(checked > 60);
  double out[kMaxFields];
  CHECK(!replicate_rewind(w->server, target, (w->server.tick + 1) << 8, out));   // the future
  CHECK(!replicate_rewind(w->server, target, (w->server.tick - 40) << 8, out));  // out of the ring
}

TEST(replicate_replays_to_the_bit) {
  LinkConditions link;
  link.latency_ms = 45;
  link.jitter_ms = 15;
  link.loss = 0.08;
  link.burst = 3;
  link.reorder = 0.05;
  link.reorder_ms = 30;
  ReplicateConfig config;
  config.snapshot_budget = 300;
  auto a = make_world(8, link, config, 11);
  auto b = make_world(8, link, config, 11);
  a->spread = b->spread = 5;
  run(*a, 600);
  run(*b, 600);
  CHECK(a->server.stats.bytes == b->server.stats.bytes);
  CHECK(a->server.stats.repeated == b->server.stats.repeated);
  CHECK(a->server.stats.starved == b->server.stats.starved);
  for (size_t k = 0; k < a->clients.size(); k++) {
    const ReplicateClient& ca = *a->clients[k];
    const ReplicateClient& cb = *b->clients[k];
    CHECK(ca.stats.corrections == cb.stats.corrections);
    CHECK(ca.stats.dropped == cb.stats.dropped);
    double va[kMaxFields], vb[kMaxFields];
    replicate_client_predicted(ca, va);
    replicate_client_predicted(cb, vb);
    for (int f = 0; f < kFields; f++) CHECK_BITS(va[f], vb[f]);
  }
}

TEST(replicate_a_client_asks_its_rates_and_the_server_caps_them) {
  LinkConditions link;
  link.latency_ms = 30;
  ReplicateConfig config;
  config.update_rate = 20;  // a snapshot every 3 ticks at 60 Hz
  config.cmd_rate = 20;     // an input message every 3 ticks
  config.rate = 4000;       // 200 bytes a snapshot
  auto w = make_world(8, link, config);
  w->spread = 3;
  run(*w, 300);
  const ReplicateStats a = w->server.stats;
  run(*w, 300);
  const ReplicateStats b = w->server.stats;
  // 300 ticks at 20 a second is 100 snapshots a client.
  CHECK(b.snapshots - a.snapshots == 800u);
  CHECK(b.bytes - a.bytes <= (b.snapshots - a.snapshots) * 200);
  CHECK(replicate_send_interval(w->server, 0) == 3);
  CHECK(replicate_snapshot_budget(w->server, 0) == 200);
  for (const auto& c : w->clients) {
    CHECK(c->stats.corrections == 0);  // three inputs a message, none lost, none repeated
    CHECK(c->stats.dropped == 0);
  }
  CHECK(w->server.stats.repeated == 0);
  // The server's caps win over a greedy ask.
  w->server.config.max_update_rate = 10;
  w->server.config.max_rate = 1000;
  CHECK(replicate_send_interval(w->server, 0) == 6);
  CHECK(replicate_snapshot_budget(w->server, 0) == 100);
}
