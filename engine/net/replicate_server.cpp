// Replication's server half: the entities, the inputs applied, the history, the snapshots built
// per client against what it acked (net/replicate.hpp).
//
// ge:summary Replication's server: entities, inputs applied a tick, history, per-client deltas
// ge:keywords net replicate server snapshot delta interest priority budget history quantize
// ge:skill net
// ge:test tests/net_replicate_test.cpp
#include <algorithm>
#include <cmath>
#include <cstring>

#include "net/replicate.hpp"
#include "net/replicate_wire.hpp"
#include "net/wire.hpp"

namespace ge::net {

namespace {

using namespace replicate_wire;

constexpr size_t kMaxQueued = 32;  // inputs a client may have waiting; older ones are dropped

double levels(const FieldSpec& f) {
  return f.bits >= 32 ? 4294967295.0 : static_cast<double>((uint64_t{1} << f.bits) - 1);
}

int index_of(const ReplicateServer& s, uint16_t id) {
  for (size_t i = 0; i < s.ids.size(); i++) {
    if (s.ids[i] == id) return static_cast<int>(i);
  }
  return -1;
}

void normalize(const Schema& schema, double* v) {
  for (int f = 0; f < schema.fields; f++)
    v[f] = dequantize(schema.spec[f], quantize(schema.spec[f], v[f]));
}

void quantize_row(const Schema& schema, const double* v, uint32_t* q) {
  for (int f = 0; f < schema.fields; f++) q[f] = quantize(schema.spec[f], v[f]);
}

Snap* find_sent(ReplicateClientSlot& c, uint32_t tick) {
  Snap& s = c.sent[tick % c.sent.size()];
  return s.valid && s.tick == tick ? &s : nullptr;
}

const Row* find_row(const Snap& s, uint16_t id) {
  const auto it = std::lower_bound(s.rows.begin(), s.rows.end(), id,
                                   [](const Row& r, uint16_t want) { return r.id < want; });
  return it != s.rows.end() && it->id == id ? &*it : nullptr;
}

bool visible(const ReplicateServer& s, int client, int index) {
  const ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  if (s.ids[static_cast<size_t>(index)] == c.entity) return true;
  const ReplicateConfig& k = s.config;
  if (k.visible && !k.visible(k.visible_ctx, client, index)) return false;
  if (k.interest_radius <= 0 || k.interest_x < 0 || k.interest_z < 0) return true;
  const int own = c.entity == kNoEntity ? -1 : index_of(s, c.entity);
  if (own < 0) return true;
  const int n = s.schema.fields;
  const double* a = &s.state[static_cast<size_t>(own) * static_cast<size_t>(n)];
  const double* b = &s.state[static_cast<size_t>(index) * static_cast<size_t>(n)];
  const double dx = a[k.interest_x] - b[k.interest_x], dz = a[k.interest_z] - b[k.interest_z];
  return dx * dx + dz * dz <= k.interest_radius * k.interest_radius;
}

int total_bits(const Schema& schema) {
  int n = 0;
  for (int f = 0; f < schema.fields; f++) n += schema.spec[f].bits;
  return n;
}

// THE PREDICTED WORLD: the other players near this client's entity, each with the last input the
// server applied for it, after the bits; the snapshot's kind says a trailer is there.
size_t write_trailer(const ReplicateServer& s, int client, uint8_t* buffer, size_t size) {
  const ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  const ReplicateConfig& k = s.config;
  const int own = c.entity == kNoEntity ? -1 : index_of(s, c.entity);
  const auto n = static_cast<size_t>(s.schema.input_size);
  const size_t start = size;
  if (own >= 0 && k.interest_x >= 0 && k.interest_z >= 0) {
    const int nf = s.schema.fields;
    const double* a = &s.state[static_cast<size_t>(own) * static_cast<size_t>(nf)];
    const double reach = 2 * k.predict_radius;
    for (size_t p = 0; p < s.clients.size(); p++) {
      const ReplicateClientSlot& o = s.clients[p];
      if (static_cast<int>(p) == client || o.entity == kNoEntity || !o.applied) continue;
      const int j = index_of(s, o.entity);
      if (j < 0) continue;
      const double* b = &s.state[static_cast<size_t>(j) * static_cast<size_t>(nf)];
      const double dx = a[k.interest_x] - b[k.interest_x], dz = a[k.interest_z] - b[k.interest_z];
      if (dx * dx + dz * dz > reach * reach) continue;
      if (size + 2 + n + 2 > kMaxMessage) break;
      buffer[size] = static_cast<uint8_t>(o.entity & 0xff);
      buffer[size + 1] = static_cast<uint8_t>(o.entity >> 8);
      std::memcpy(buffer + size + 2, o.last, n);
      size += 2 + n;
    }
  }
  const size_t len = size - start;
  buffer[size] = static_cast<uint8_t>(len & 0xff);
  buffer[size + 1] = static_cast<uint8_t>(len >> 8);
  buffer[0] = kSnapshotInputs;
  return size + 2;
}

void send_snapshot(ReplicateServer& s, int client, uint8_t* buffer, size_t budget) {
  ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  const Schema& schema = s.schema;
  const int nf = schema.fields;
  const Snap* base = c.has_ack ? find_sent(c, c.acked) : nullptr;
  Snap& out = c.sent[s.tick % c.sent.size()];
  out.tick = s.tick;
  out.valid = true;
  out.rows.clear();

  Writer head = make_writer(buffer, kSnapshotHeader);
  write_u8(head, kSnapshot);
  write_u32(head, s.tick);
  write_u32(head, base ? base->tick : 0);
  write_u32(head, c.applied);
  write_u16(head, c.entity);
  write_u8(head, static_cast<uint8_t>(static_cast<int8_t>(replicate_slack(c))));
  uint8_t* body = buffer + kSnapshotHeader;
  std::memset(body, 0, budget - kSnapshotHeader);  // the bits' last byte, and the count's pad
  // The entry count goes first as a varint of at most 3 bytes; the bits follow it.
  constexpr size_t kCountBytes = 3;
  BitWriter w{body + kCountBytes, budget - kSnapshotHeader - kCountBytes};
  uint32_t entries = 0;
  const auto room = [&](size_t bits) { return w.bits + bits <= w.capacity * 8; };

  // Gone first: rows of the baseline whose entity is dead or out of sight now.
  if (base) {
    for (const Row& r : base->rows) {
      const int i = index_of(s, r.id);
      if (i >= 0 && visible(s, client, i)) continue;
      if (!room(18)) {
        out.rows.push_back(r);  // still the client's until a later snapshot says otherwise
        continue;
      }
      put_bits(w, r.id, 16);
      put_bits(w, kGone, 2);
      entries++;
    }
  }
  // Then the visible ones, the stalest first: a row's priority is how many ticks the client's copy
  // (the baseline's) is behind, so a row starved in one chain of baselines is not starved for
  // having gone out in another; a new row comes before every update, the own entity before all.
  std::vector<int>& order = s.order;
  order.clear();
  for (size_t i = 0; i < s.ids.size(); i++) {
    if (s.ids[i] == kNoEntity || !visible(s, client, static_cast<int>(i))) continue;
    const Row* was = base ? find_row(*base, s.ids[i]) : nullptr;
    double p = was ? static_cast<double>(s.tick - was->fresh) : 1e6;
    if (s.ids[i] == c.entity) p += 1e9;
    c.priority[i] = p;
    order.push_back(static_cast<int>(i));
  }
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return c.priority[static_cast<size_t>(a)] > c.priority[static_cast<size_t>(b)];
  });
  const int all_bits = total_bits(schema);
  for (int i : order) {
    Row now;
    now.id = s.ids[static_cast<size_t>(i)];
    now.fresh = s.tick;
    quantize_row(schema, &s.state[static_cast<size_t>(i) * static_cast<size_t>(nf)], now.q);
    const Row* was = base ? find_row(*base, now.id) : nullptr;
    if (was) {
      uint32_t mask = 0;
      int bits = 0;
      for (int f = 0; f < nf; f++) {
        if (now.q[f] != was->q[f]) {
          mask |= 1u << f;
          bits += schema.spec[f].bits;
        }
      }
      if (!mask) {
        out.rows.push_back(now);
        continue;
      }
      if (!room(size_t{18} + static_cast<size_t>(nf) + static_cast<size_t>(bits))) {
        out.rows.push_back(*was);  // left as the client has it, growing staler
        s.stats.starved++;
        continue;
      }
      put_bits(w, now.id, 16);
      put_bits(w, kUpdate, 2);
      put_bits(w, mask, nf);
      for (int f = 0; f < nf; f++) {
        if (mask & (1u << f)) put_bits(w, now.q[f], schema.spec[f].bits);
      }
    } else {
      if (!room(size_t{18} + static_cast<size_t>(all_bits))) {
        s.stats.starved++;
        continue;
      }
      put_bits(w, now.id, 16);
      put_bits(w, kNew, 2);
      for (int f = 0; f < nf; f++) put_bits(w, now.q[f], schema.spec[f].bits);
    }
    entries++;
    out.rows.push_back(now);
  }
  std::sort(out.rows.begin(), out.rows.end(),
            [](const Row& a, const Row& b) { return a.id < b.id; });
  // The count, padded to its 3 bytes (a varint read stops at its last byte; the rest is skipped).
  Writer cw = make_writer(body, kCountBytes);
  write_varint(cw, entries);
  size_t size = kSnapshotHeader + kCountBytes + bytes_of(w);
  if (s.config.predict_radius > 0 && s.schema.interact)
    size = write_trailer(s, client, buffer, size);
  node_send(*s.node, client, s.config.snapshot_channel, buffer, size);
  s.stats.snapshots++;
  s.stats.bytes += size;
  if (!base) s.stats.full++;
}

void on_input(ReplicateServer& s, int client, const uint8_t* data, size_t size) {
  ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  Reader r = make_reader(data, size);
  if (read_u8(r) != kInput) return;
  const uint32_t ack = read_u32(r), view = read_u32(r), rate = read_u32(r);
  const uint16_t update_rate = read_u16(r);
  const uint32_t newest = read_u32(r);
  const uint8_t count = read_u8(r);
  const auto n = static_cast<size_t>(s.schema.input_size);
  if (r.bad || remaining(r) != count * n || count == 0 || newest < count) return;
  c.active = true;
  if (ack && (!c.has_ack || ack > c.acked) && ack <= s.tick) {
    c.acked = ack;
    c.has_ack = true;
  }
  c.view_tick = view;
  c.rate = rate;
  c.update_rate = update_rate;
  const uint8_t* inputs = data + r.at;
  for (uint32_t k = 0; k < count; k++) {
    const uint32_t seq = newest - count + 1 + k;
    const uint32_t expect = c.queue_seq.empty() ? c.next_seq : c.queue_seq.back() + 1;
    if (seq < expect) continue;  // had it
    if (seq > expect) {
      // More were lost than the redundancy covers. The ticks that went without already stepped
      // the last input again; the rest are taken to be the one before them (a held stick, most
      // likely), so the steps still number the client's own and a correction is only the
      // difference in what was held.
      const uint32_t lost = seq - expect, covered = std::min(lost, c.repeats);
      c.repeats -= covered;
      if (lost - covered > kMaxQueued) {
        c.queue_seq.clear();
        c.inputs.clear();
        c.next_seq = seq;
      } else {
        for (uint32_t k2 = expect + covered; k2 < seq; k2++) {
          const uint8_t* before = c.queue_seq.empty() ? c.last : &c.inputs[c.inputs.size() - n];
          c.inputs.insert(c.inputs.end(), before, before + n);
          c.queue_seq.push_back(k2);
        }
        // Those the repeats stood in for are skipped: the queue goes on after them.
        if (covered && c.queue_seq.empty()) c.next_seq = expect + covered;
      }
    }
    c.queue_seq.push_back(seq);
    c.inputs.insert(c.inputs.end(), inputs + k * n, inputs + (k + 1) * n);
  }
  while (c.queue_seq.size() > kMaxQueued) {
    s.stats.trimmed++;
    c.queue_seq.erase(c.queue_seq.begin());
    c.inputs.erase(c.inputs.begin(), c.inputs.begin() + static_cast<std::ptrdiff_t>(n));
  }
}

void send_event(ReplicateServer& s, int client, uint8_t kind, const uint8_t* payload, int size) {
  const ReplicateConfig& k = s.config;
  if (k.event_channel < 0 || client < 0 || client >= kMaxPeers ||
      client >= static_cast<int>(s.node->peers.size()) || !peer_connected(*s.node, client))
    return;
  size = std::clamp(size, 0, kMaxEventPayload);
  uint8_t out[1 + 1 + 4 + 1 + kMaxEventPayload];
  Writer w = make_writer(out, sizeof out);
  write_u8(w, kEvent);
  write_u8(w, kind);
  write_u32(w, s.clients[static_cast<size_t>(client)].applied);
  write_u8(w, static_cast<uint8_t>(size));
  write_bytes(w, payload, static_cast<size_t>(size));
  node_send(*s.node, client, k.event_channel, out, w.size);
  s.stats.events++;
}

// THE PREDICTED WORLD's other half: every owned entity resolved against every other after the
// steps, in slot order then index order, each pair once; a contact beginning is an event to each
// side's client.
void interact_all(ReplicateServer& s) {
  const int nf = s.schema.fields;
  const size_t ents = s.ids.size();
  for (size_t k = 0; k < s.clients.size(); k++) {
    ReplicateClientSlot& c = s.clients[k];
    const int i = c.entity == kNoEntity ? -1 : index_of(s, c.entity);
    if (i < 0) continue;
    double* a = &s.state[static_cast<size_t>(i) * static_cast<size_t>(nf)];
    for (size_t j = 0; j < ents; j++) {
      if (static_cast<int>(j) == i || s.ids[j] == kNoEntity) continue;
      const int m = s.owner[j];
      const bool other_player =
          m >= 0 && m < kMaxPeers && s.clients[static_cast<size_t>(m)].entity == s.ids[j];
      if (other_player && static_cast<size_t>(m) < k) continue;  // that slot resolved the pair
      double* b = &s.state[j * static_cast<size_t>(nf)];
      const bool touched = s.schema.interact(s.schema.ctx, a, b);
      if (touched) {
        normalize(s.schema, a);
        normalize(s.schema, b);
      }
      const bool began = touched && !c.touching[j];
      c.touching[j] = touched ? 1 : 0;
      if (other_player) {
        s.clients[static_cast<size_t>(m)].touching[static_cast<size_t>(i)] = touched ? 1 : 0;
      }
      if (!began) continue;
      s.stats.contacts++;
      if (!s.config.contact_events) continue;
      const uint8_t pj[2] = {static_cast<uint8_t>(s.ids[j] & 0xff),
                             static_cast<uint8_t>(s.ids[j] >> 8)};
      send_event(s, static_cast<int>(k), kContactEvent, pj, 2);
      if (other_player) {
        const uint8_t pi[2] = {static_cast<uint8_t>(c.entity & 0xff),
                               static_cast<uint8_t>(c.entity >> 8)};
        send_event(s, m, kContactEvent, pi, 2);
      }
    }
  }
}

}  // namespace

void replicate_server_event(ReplicateServer& s, int client, uint8_t kind, const uint8_t* payload,
                            int size) {
  send_event(s, client, kind, payload, size);
}

uint32_t quantize(const FieldSpec& f, double v) {
  const double range = f.max - f.min;
  if (f.angle) {
    const double steps = levels(f) + 1.0;
    double t = (v - f.min) / range;
    t -= std::floor(t);
    double q = std::floor(t * steps + 0.5);
    if (q >= steps) q -= steps;
    return static_cast<uint32_t>(q);
  }
  double t = (v - f.min) / range;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return static_cast<uint32_t>(std::floor(t * levels(f) + 0.5));
}

double dequantize(const FieldSpec& f, uint32_t q) {
  const double range = f.max - f.min;
  if (f.angle) return f.min + static_cast<double>(q) * range / (levels(f) + 1.0);
  return f.min + static_cast<double>(q) * range / levels(f);
}

void replicate_server_init(ReplicateServer& s, const Schema& schema, const ReplicateConfig& config,
                           Node& node) {
  s = ReplicateServer{};
  s.schema = schema;
  s.config = config;
  if (s.config.ring < 2) s.config.ring = 2;
  if (s.config.send_every < 1) s.config.send_every = 1;
  if (s.config.tick_hz < 1) s.config.tick_hz = 1;
  s.slack_window =
      std::max(1, static_cast<int>(std::lround(s.config.slack_window_s * s.config.tick_hz)));
  s.node = &node;
  const auto n = static_cast<size_t>(config.max_entities);
  const auto ring = static_cast<size_t>(s.config.ring);
  s.ids.assign(n, kNoEntity);
  s.owner.assign(n, -1);
  s.state.assign(n * static_cast<size_t>(schema.fields), 0.0);
  s.history.assign(ring * n * static_cast<size_t>(schema.fields), 0);
  s.history_tick.assign(ring, 0);
  s.history_alive.assign(ring * n, 0);
  s.order.reserve(n);
  s.clients.resize(kMaxPeers);
  for (ReplicateClientSlot& c : s.clients) {
    c.sent.resize(ring);
    for (Snap& snap : c.sent) snap.rows.reserve(n);
    c.priority.assign(n, 0.0);
    c.touching.assign(n, 0);
    c.inputs.reserve(kMaxQueued * static_cast<size_t>(kMaxInputSize) * 2);
    c.queue_seq.reserve(kMaxQueued * 2);
  }
}

uint16_t replicate_spawn(ReplicateServer& s, int owner, const double* values) {
  for (size_t i = 0; i < s.ids.size(); i++) {
    if (s.ids[i] != kNoEntity) continue;
    // Ids are not reused until the counter wraps, so a client never mistakes a new entity for
    // a gone one it still holds.
    uint16_t id = s.next_id;
    while (id == kNoEntity || index_of(s, id) >= 0) id++;
    s.next_id = static_cast<uint16_t>(id + 1);
    s.ids[i] = id;
    s.owner[i] = owner;
    double* v = &s.state[i * static_cast<size_t>(s.schema.fields)];
    std::memcpy(v, values, sizeof(double) * static_cast<size_t>(s.schema.fields));
    normalize(s.schema, v);
    if (owner >= 0 && owner < kMaxPeers) {
      ReplicateClientSlot& c = s.clients[static_cast<size_t>(owner)];
      c.entity = id;
    }
    return id;
  }
  return kNoEntity;
}

void replicate_set(ReplicateServer& s, uint16_t id, const double* values) {
  const int i = index_of(s, id);
  if (i < 0) return;
  double* v = &s.state[static_cast<size_t>(i) * static_cast<size_t>(s.schema.fields)];
  std::memcpy(v, values, sizeof(double) * static_cast<size_t>(s.schema.fields));
  normalize(s.schema, v);
}

const double* replicate_get(const ReplicateServer& s, uint16_t id) {
  const int i = index_of(s, id);
  return i < 0 ? nullptr : &s.state[static_cast<size_t>(i) * static_cast<size_t>(s.schema.fields)];
}

void replicate_despawn(ReplicateServer& s, uint16_t id) {
  const int i = index_of(s, id);
  if (i < 0) return;
  const int owner = s.owner[static_cast<size_t>(i)];
  if (owner >= 0 && owner < kMaxPeers && s.clients[static_cast<size_t>(owner)].entity == id)
    s.clients[static_cast<size_t>(owner)].entity = kNoEntity;
  s.ids[static_cast<size_t>(i)] = kNoEntity;
  s.owner[static_cast<size_t>(i)] = -1;
}

void replicate_server_receive(ReplicateServer& s) {
  for (const Event& e : s.node->events) {
    if (e.peer < 0 || e.peer >= kMaxPeers) continue;
    ReplicateClientSlot& c = s.clients[static_cast<size_t>(e.peer)];
    if (e.type == EventType::disconnected) {
      // The slot is free for the next to take it; its entity is the game's to despawn. Its
      // buffers are kept, emptied, so no tick allocates.
      std::vector<Snap> sent = std::move(c.sent);
      std::vector<double> priority = std::move(c.priority);
      std::vector<uint8_t> inputs = std::move(c.inputs);
      std::vector<uint32_t> queue = std::move(c.queue_seq);
      std::vector<uint8_t> touching = std::move(c.touching);
      c = ReplicateClientSlot{};
      std::fill(touching.begin(), touching.end(), uint8_t{0});
      c.touching = std::move(touching);
      for (Snap& snap : sent) snap.valid = false;
      std::fill(priority.begin(), priority.end(), 0.0);
      inputs.clear();
      queue.clear();
      c.sent = std::move(sent);
      c.priority = std::move(priority);
      c.inputs = std::move(inputs);
      c.queue_seq = std::move(queue);
    } else if (e.type == EventType::message && e.channel == s.config.input_channel) {
      on_input(s, e.peer, event_data(*s.node, e), e.size);
    }
  }
}

void replicate_server_tick(ReplicateServer& s) {
  s.tick++;
  const int nf = s.schema.fields;
  const auto n = static_cast<size_t>(s.schema.input_size);
  for (size_t k = 0; k < s.clients.size(); k++) {
    ReplicateClientSlot& c = s.clients[k];
    const int i = c.entity == kNoEntity ? -1 : index_of(s, c.entity);
    if (i < 0 || !s.schema.step) continue;
    // TIME SYNC: the inputs left waiting behind the one applied now (-1: none came in time), the
    // fewest over a window, which the client's clock is steered by.
    const int sample = c.queue_seq.empty() ? (c.applied ? -1 : kNoSlack)
                                           : static_cast<int>(c.queue_seq.size()) - 1;
    c.slack_now = std::min(c.slack_now, sample);
    if (++c.slack_ticks >= s.slack_window) {
      c.slack_before = c.slack_now;
      c.slack_now = kNoSlack;
      c.slack_ticks = 0;
    }
    if (!c.queue_seq.empty()) {
      std::memcpy(c.last, c.inputs.data(), n);
      c.applied = c.queue_seq.front();
      c.queue_seq.erase(c.queue_seq.begin());
      c.inputs.erase(c.inputs.begin(), c.inputs.begin() + static_cast<std::ptrdiff_t>(n));
      c.next_seq = c.applied + 1;
      s.stats.inputs++;
    } else if (c.applied) {
      // None arrived in time: the last one again. It is not counted as the next one, so the queue
      // grows by one (a buffer for this link's jitter) and the client sees a one-step correction.
      c.repeats++;
      s.stats.repeated++;
    } else {
      continue;  // nothing from it yet: it stands still
    }
    double* v = &s.state[static_cast<size_t>(i) * static_cast<size_t>(nf)];
    s.schema.step(s.schema.ctx, v, c.last);
    normalize(s.schema, v);
  }
  if (s.schema.interact) interact_all(s);
  // The history, for rewinding.
  const auto ring = static_cast<size_t>(s.config.ring);
  const size_t slot = s.tick % ring;
  const size_t ents = s.ids.size();
  s.history_tick[slot] = s.tick;
  for (size_t i = 0; i < ents; i++) {
    s.history_alive[slot * ents + i] = s.ids[i] != kNoEntity ? 1 : 0;
    if (s.ids[i] != kNoEntity)
      quantize_row(s.schema, &s.state[i * static_cast<size_t>(nf)],
                   &s.history[(slot * ents + i) * static_cast<size_t>(nf)]);
  }
  uint8_t buffer[kMaxMessage];
  for (int p = 0; p < kMaxPeers && p < static_cast<int>(s.node->peers.size()); p++) {
    if (!peer_connected(*s.node, p)) continue;
    ReplicateClientSlot& c = s.clients[static_cast<size_t>(p)];
    if (s.tick - c.last_sent < static_cast<uint32_t>(replicate_send_interval(s, p))) continue;
    c.last_sent = s.tick;
    send_snapshot(s, p, buffer, replicate_snapshot_budget(s, p));
  }
}

int replicate_slack(const ReplicateClientSlot& c) {
  const int v = std::min(c.slack_now, c.slack_before);
  return v == kNoSlack ? 0 : std::clamp(v, -128, 127);
}

int replicate_send_interval(const ReplicateServer& s, int client) {
  const ReplicateConfig& k = s.config;
  const ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  int asked = c.update_rate;
  if (k.max_update_rate > 0 && (asked <= 0 || asked > k.max_update_rate)) asked = k.max_update_rate;
  int interval = k.send_every;
  if (asked > 0) interval = std::max(interval, (k.tick_hz + asked - 1) / asked);
  return interval;
}

size_t replicate_snapshot_budget(const ReplicateServer& s, int client) {
  const ReplicateConfig& k = s.config;
  const ReplicateClientSlot& c = s.clients[static_cast<size_t>(client)];
  size_t budget = k.snapshot_budget ? std::min(k.snapshot_budget, kMaxMessage) : kMaxMessage;
  uint32_t rate = c.rate;
  if (k.max_rate > 0 && (rate == 0 || rate > k.max_rate)) rate = k.max_rate;
  if (rate > 0 && k.tick_hz > 0) {
    const auto per = static_cast<size_t>(uint64_t{rate} *
                                         static_cast<uint64_t>(replicate_send_interval(s, client)) /
                                         static_cast<uint64_t>(k.tick_hz));
    budget = std::min(budget, std::max(per, kSnapshotFloor));
  }
  return budget;
}

bool replicate_rewind(const ReplicateServer& s, uint16_t id, uint32_t tick_x256, double* out) {
  const int i = index_of(s, id);
  if (i < 0) return false;
  const uint32_t t0 = tick_x256 >> 8, t1 = t0 + ((tick_x256 & 255) ? 1 : 0);
  const auto ring = static_cast<size_t>(s.config.ring);
  const size_t ents = s.ids.size();
  const int nf = s.schema.fields;
  const size_t a = t0 % ring, b = t1 % ring;
  if (s.history_tick[a] != t0 || s.history_tick[b] != t1 || t1 > s.tick) return false;
  if (!s.history_alive[a * ents + static_cast<size_t>(i)] ||
      !s.history_alive[b * ents + static_cast<size_t>(i)])
    return false;
  const double frac = static_cast<double>(tick_x256 & 255) / 256.0;
  for (int f = 0; f < nf; f++) {
    const FieldSpec& spec = s.schema.spec[f];
    const double va = dequantize(spec, s.history[(a * ents + static_cast<size_t>(i)) * nf + f]);
    double vb = dequantize(spec, s.history[(b * ents + static_cast<size_t>(i)) * nf + f]);
    if (spec.angle) {
      const double range = spec.max - spec.min;
      if (vb - va > range / 2) vb -= range;
      if (va - vb > range / 2) vb += range;
    }
    out[f] = va + (vb - va) * frac;
  }
  return true;
}

}  // namespace ge::net
