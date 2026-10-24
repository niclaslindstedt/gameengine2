// Replication's client half: snapshots decoded against their baseline, the prediction replayed
// and smoothed, the others interpolated (net/replicate.hpp).
//
// ge:summary Replication's client: deltas decoded, prediction reconciled, interpolation, acks
// ge:keywords net replicate client decode delta predict reconcile replay smoothing interpolate
// ge:skill net
// ge:test tests/net_replicate_test.cpp
#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/libm/libm.hpp"
#include "net/replicate.hpp"
#include "net/replicate_wire.hpp"
#include "net/wire.hpp"

namespace ge::net {

namespace {

using namespace replicate_wire;

constexpr uint32_t kInputRing = 256;  // inputs kept for replaying: seconds at any tick rate

const Snap* find_snap(const ReplicateClient& c, uint32_t tick) {
  const Snap& s = c.ring[tick % c.ring.size()];
  return s.valid && s.tick == tick ? &s : nullptr;
}

const Row* find_row(const Snap& s, uint16_t id) {
  const auto it = std::lower_bound(s.rows.begin(), s.rows.end(), id,
                                   [](const Row& r, uint16_t want) { return r.id < want; });
  return it != s.rows.end() && it->id == id ? &*it : nullptr;
}

double wrap_diff(const FieldSpec& f, double d) {
  if (!f.angle) return d;
  const double range = f.max - f.min;
  while (d > range / 2) d -= range;
  while (d < -range / 2) d += range;
  return d;
}

void step_own(ReplicateClient& c, double* v, const uint8_t* input) {
  c.schema.step(c.schema.ctx, v, input);
  for (int f = 0; f < c.schema.fields; f++)
    v[f] = dequantize(c.schema.spec[f], quantize(c.schema.spec[f], v[f]));
}

Predicted* find_predicted(ReplicateClient& c, uint16_t id) {
  for (Predicted& p : c.world) {
    if (p.id == id) return &p;
  }
  return nullptr;
}
const Predicted* find_predicted(const ReplicateClient& c, uint16_t id) {
  for (const Predicted& p : c.world) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

// The curve through the snapshots at the render tick (M17o), whatever the predicted world holds.
bool sample_curve(const ReplicateClient& c, uint16_t id, double* out) {
  const uint32_t t = replicate_client_render_tick(c);
  // The snapshots holding the entity around the render tick: a at or before it, b after, and the
  // one either side of them for the curve's slopes (p before a, n after b).
  struct Hit {
    const Snap* s = nullptr;
    const Row* r = nullptr;
  } p, a, b, n;
  for (const Snap& s : c.ring) {
    if (!s.valid) continue;
    const Row* r = find_row(s, id);
    if (!r) continue;
    if ((s.tick << 8) <= t) {
      if (!a.s || s.tick > a.s->tick) {
        p = a;
        a = {&s, r};
      } else if (!p.s || s.tick > p.s->tick) {
        p = {&s, r};
      }
    } else if (!b.s || s.tick < b.s->tick) {
      n = b;
      b = {&s, r};
    } else if (!n.s || s.tick < n.s->tick) {
      n = {&s, r};
    }
  }
  if (!a.s && !b.s) return false;
  const int nf = c.schema.fields;
  if (!a.s) {
    for (int f = 0; f < nf; f++) out[f] = dequantize(c.schema.spec[f], b.r->q[f]);
    return true;  // nothing before it yet: held where it is
  }
  const auto value = [&](const Hit& h, int f, double from) {
    const FieldSpec& spec = c.schema.spec[f];
    return from + wrap_diff(spec, dequantize(spec, h.r->q[f]) - from);
  };
  // Carried on from a at the velocity from p to a, at most extrapolate_s past it.
  const auto carried = [&](int f, uint32_t at) {
    const FieldSpec& spec = c.schema.spec[f];
    const double va = dequantize(spec, a.r->q[f]);
    if (!p.s || c.extrapolate_ticks <= 0 || !spec.extrapolate) return va;
    const double ahead =
        std::min(static_cast<double>(at - (a.s->tick << 8)) / 256, c.extrapolate_ticks);
    const double vp = value(p, f, va);
    return va + (va - vp) / static_cast<double>(a.s->tick - p.s->tick) * ahead;
  };
  if (!b.s) {
    for (int f = 0; f < nf; f++) out[f] = carried(f, t);
    return true;
  }
  const double span = static_cast<double>(b.s->tick - a.s->tick);
  const double u = static_cast<double>(t - (a.s->tick << 8)) / (span * 256);
  // A late b: blended in from where a carried it, over extrapolate_blend_s from its arrival.
  double blend = 1;
  if (b.s->late_x256 > (a.s->tick << 8) && c.extrapolate_ticks > 0 && c.blend_ticks > 0) {
    const auto since = static_cast<double>(int64_t{t} - int64_t{b.s->late_x256});
    blend = std::clamp(since / (256.0 * c.blend_ticks), 0.0, 1.0);
  }
  for (int f = 0; f < nf; f++) {
    const FieldSpec& spec = c.schema.spec[f];
    const double va = dequantize(spec, a.r->q[f]);
    const double vb = value(b, f, va);
    double v = va + (vb - va) * u;
    if (c.config.cubic) {
      // Hermite through a and b, each slope from its neighbours (Catmull-Rom on uneven ticks):
      // the velocity is continuous from one span to the next, so the path does not kink.
      const double ma = p.s ? (vb - value(p, f, va)) / static_cast<double>(b.s->tick - p.s->tick)
                            : (vb - va) / span;
      const double mb = n.s ? (value(n, f, va) - va) / static_cast<double>(n.s->tick - a.s->tick)
                            : (vb - va) / span;
      const double u2 = u * u, u3 = u2 * u;
      v = (2 * u3 - 3 * u2 + 1) * va + (u3 - 2 * u2 + u) * span * ma + (-2 * u3 + 3 * u2) * vb +
          (u3 - u2) * span * mb;
    }
    out[f] = blend < 1 ? carried(f, t) + (v - carried(f, t)) * blend : v;
  }
  return true;
}

// A critically damped spring over one tick: e(1) = (e + j) d, e'(1) = (e' - j w) d, j = e' + e w.
void spring(const ReplicateClient& c, double* e, double* ev) {
  for (int f = 0; f < c.schema.fields; f++) {
    const double j = ev[f] + e[f] * c.spring_w;
    e[f] = (e[f] + j) * c.spring_decay;
    ev[f] = (ev[f] - j * c.spring_w) * c.spring_decay;
  }
}

// Where entity `p` is drawn now (its state or the curve, plus its error).
void drawn_of(const ReplicateClient& c, const Predicted& p, double* out) {
  const int nf = c.schema.fields;
  if (p.active || !sample_curve(c, p.id, out)) std::memcpy(out, p.state, sizeof(double) * nf);
  for (int f = 0; f < nf; f++) out[f] += p.error[f];
}

// Entity `p` leaves the predicted world: drawn on the curve again, its drawn place kept as an
// error the spring takes out; freed when the curve does not have it.
void fade(ReplicateClient& c, Predicted& p) {
  double was[kMaxFields], curve[kMaxFields];
  drawn_of(c, p, was);
  p.active = false;
  if (!sample_curve(c, p.id, curve)) {
    p = Predicted{};
    return;
  }
  for (int f = 0; f < c.schema.fields; f++)
    p.error[f] = wrap_diff(c.schema.spec[f], was[f] - curve[f]);
}

// One step of the predicted world beside the own entity `v`: each other player by its last input,
// then each contact resolved. `live`: the first time this input is stepped (contacts are events).
void step_world(ReplicateClient& c, double* v, bool live) {
  if (!c.schema.interact) return;
  for (Predicted& p : c.world) {
    if (p.active && p.stepped) step_own(c, p.state, p.input);
  }
  for (Predicted& p : c.world) {
    if (!p.active) continue;
    const bool touched = c.schema.interact(c.schema.ctx, v, p.state);
    if (touched) {
      for (int f = 0; f < c.schema.fields; f++) {
        v[f] = dequantize(c.schema.spec[f], quantize(c.schema.spec[f], v[f]));
        p.state[f] = dequantize(c.schema.spec[f], quantize(c.schema.spec[f], p.state[f]));
      }
    }
    if (live && touched && !p.touching) {
      c.stats.contacts++;
      if (c.config.contact_events) {
        const uint8_t id[2] = {static_cast<uint8_t>(p.id & 0xff), static_cast<uint8_t>(p.id >> 8)};
        replicate_client_predict_event(c, kContactEvent, id, 2);
      }
    }
    p.touching = touched;
  }
}

// THE PREDICTED WORLD on a snapshot: the entities near the own one chosen, each started from the
// snapshot (another player with its last input), the drawn place of each kept continuous.
void choose_world(ReplicateClient& c, const Snap& snap, const double* own) {
  const ReplicateConfig& k = c.config;
  const int nf = c.schema.fields;
  if (k.predict_radius <= 0 || !c.schema.interact || k.interest_x < 0 || k.interest_z < 0) return;
  // Too far ahead of the server for a stale input to guess well: every other fades to the curve,
  // until the replay is back to three quarters of the horizon (a jittery link flickering across
  // it would swap the drawn place back and forth).
  const uint32_t ahead = c.seq > c.applied ? c.seq - c.applied : 0;
  if (c.predict_max > 0) {
    if (ahead > static_cast<uint32_t>(c.predict_max)) c.beyond = true;
    if (ahead * 4 <= static_cast<uint32_t>(c.predict_max) * 3) c.beyond = false;
  }
  if (c.beyond) {
    for (Predicted& p : c.world) {
      if (p.active) fade(c, p);
    }
    return;
  }
  const double r2 = k.predict_radius * k.predict_radius, keep2 = r2 * 2.25;
  // The candidates by distance, then id; those already predicted are kept out to 1.5 radii.
  struct Pick {
    double d2;
    uint16_t id;
    const Row* row;
  };
  Pick picks[64];
  int count = 0;
  for (const Row& row : snap.rows) {
    if (row.id == c.own) continue;
    const double dx =
        dequantize(c.schema.spec[k.interest_x], row.q[k.interest_x]) - own[k.interest_x];
    const double dz =
        dequantize(c.schema.spec[k.interest_z], row.q[k.interest_z]) - own[k.interest_z];
    const double d2 = dx * dx + dz * dz;
    const Predicted* was = find_predicted(c, row.id);
    if (d2 > (was && was->active ? keep2 : r2)) continue;
    Pick p{d2, row.id, &row};
    int at = count < 64 ? count++ : 63;
    if (at == 63 && count == 64 && d2 >= picks[63].d2) continue;
    while (at > 0 &&
           (picks[at - 1].d2 > d2 || (picks[at - 1].d2 == d2 && picks[at - 1].id > row.id))) {
      picks[at] = picks[at - 1];
      at--;
    }
    picks[at] = p;
  }
  count = std::min(count, static_cast<int>(c.world.size()));
  // Those no longer chosen fade back to the curve.
  for (Predicted& p : c.world) {
    if (!p.active) continue;
    bool kept = false;
    for (int i = 0; i < count; i++) kept = kept || picks[i].id == p.id;
    if (!kept) fade(c, p);
  }
  for (int i = 0; i < count; i++) {
    Predicted* p = find_predicted(c, picks[i].id);
    double was[kMaxFields];
    bool continuing = false;
    if (p) {
      continuing = p->active;
      drawn_of(c, *p, was);
    } else {
      for (Predicted& q : c.world) {
        if (q.id == kNoEntity) {
          p = &q;
          break;
        }
      }
      if (!p) {
        // Every slot busy: the fading one nearest done gives way.
        for (Predicted& q : c.world) {
          if (!q.active) {
            p = &q;
            break;
          }
        }
      }
      if (!p) break;
      *p = Predicted{};
      p->id = picks[i].id;
      if (!sample_curve(c, p->id, was))
        for (int f = 0; f < nf; f++) was[f] = dequantize(c.schema.spec[f], picks[i].row->q[f]);
    }
    p->continuing = continuing;
    if (continuing) std::memcpy(p->prior, p->state, sizeof(double) * nf);
    p->active = true;
    p->dist2 = picks[i].d2;
    for (int f = 0; f < nf; f++) {
      p->state[f] = dequantize(c.schema.spec[f], picks[i].row->q[f]);
      p->error[f] = was[f];  // the drawn place, until the replay says where the prediction is
    }
    if (!continuing) std::fill(p->error_v, p->error_v + nf, 0.0);
    p->stepped = false;
    for (size_t o = 0; o < c.others_id.size(); o++) {
      if (c.others_id[o] != p->id) continue;
      p->stepped = true;
      std::memcpy(p->input, &c.others_input[o * static_cast<size_t>(c.schema.input_size)],
                  static_cast<size_t>(c.schema.input_size));
    }
  }
}

void reconcile(ReplicateClient& c, const Snap& snap) {
  const Row* own = c.own == kNoEntity ? nullptr : find_row(snap, c.own);
  if (!own || !c.schema.step) return;
  const int nf = c.schema.fields;
  double v[kMaxFields];
  for (int f = 0; f < nf; f++) v[f] = dequantize(c.schema.spec[f], own->q[f]);
  const auto n = static_cast<size_t>(c.schema.input_size);
  choose_world(c, snap, v);
  // The replay's cost: each unapplied input steps the own entity and every other player predicted
  // beside it. Past the budget the farthest are left out of this replay (and fade).
  const uint32_t unapplied = c.seq > c.applied ? std::min(c.seq - c.applied, kInputRing - 1) : 0;
  const auto cost = [&] {
    uint64_t per = 1;
    for (const Predicted& p : c.world) per += p.active ? 1 : 0;
    return per * unapplied;
  };
  if (c.config.resim_budget > 0) {
    while (cost() > static_cast<uint64_t>(c.config.resim_budget)) {
      Predicted* far = nullptr;
      for (Predicted& p : c.world) {
        if (p.active && (!far || p.dist2 > far->dist2)) far = &p;
      }
      if (!far) break;
      // Its drawn place (in `error` until the replay) becomes a fade from there to the curve.
      double was[kMaxFields], curve[kMaxFields];
      std::memcpy(was, far->error, sizeof was);
      far->active = false;
      if (!sample_curve(c, far->id, curve)) {
        *far = Predicted{};
      } else {
        for (int f = 0; f < nf; f++) far->error[f] = wrap_diff(c.schema.spec[f], was[f] - curve[f]);
      }
      c.stats.resim_capped++;
    }
  }
  if (c.config.predict_radius > 0) {
    c.stats.resims++;
    c.stats.resim_steps += cost();
    c.stats.resim_max = std::max<uint64_t>(c.stats.resim_max, cost());
  }
  // Every input the server has not applied yet, replayed over its word.
  for (uint32_t s = c.applied + 1; s <= c.seq && c.seq - s < kInputRing; s++) {
    step_own(c, v, &c.history[(s % kInputRing) * n]);
    step_world(c, v, false);
  }
  // The others' drawn places stay where they were: the difference to the new prediction is
  // their error, drawn out by the spring.
  c.last_world_error = 0;
  for (Predicted& p : c.world) {
    if (!p.active) continue;
    double moved = 0;
    for (int f = 0; f < nf; f++) {
      const double before = p.error[f];  // the drawn place
      p.error[f] = wrap_diff(c.schema.spec[f], before - p.state[f]);
      const double d = wrap_diff(c.schema.spec[f], p.prior[f] - p.state[f]);
      if (p.continuing) moved = std::max(moved, d < 0 ? -d : d);
    }
    if (moved > 0) {
      c.stats.predicted_corrections++;
      c.last_world_error = std::max(c.last_world_error, moved);
    }
  }
  if (c.predicting) {
    double worst = 0;
    for (int f = 0; f < nf; f++) {
      const double d = wrap_diff(c.schema.spec[f], c.predicted[f] - v[f]);
      c.error[f] += d;
      const double a = d < 0 ? -d : d;
      if (a > worst) worst = a;
    }
    if (worst > 0) {
      c.stats.corrections++;
      c.last_error = worst;
    }
    if (c.config.snap_distance > 0 && worst >= c.config.snap_distance) {
      c.stats.snaps++;  // a teleport, a respawn: shown at once, nothing left to draw out
      for (int f = 0; f < nf; f++) c.error[f] = c.error_v[f] = 0;
    }
  }
  std::memcpy(c.predicted, v, sizeof(double) * static_cast<size_t>(nf));
  c.predicting = true;
}

// TIME SYNC on a snapshot's arrival: the clock's speed from the slack the server saw, and the
// reckoned server tick, the arrivals' spacing and their jitter from when it came.
void sync_clock(ReplicateClient& c, uint32_t tick, int slack) {
  const ReplicateConfig& k = c.config;
  c.slack = slack;
  // Running short starves the server (a step repeated, a correction), so the clock speeds up
  // twice as hard as it slows down.
  // A window that ran short raises the reserve by one for the next few seconds (`starve_hold`).
  if (slack < 0) c.starved_at = c.seq + 1;
  const bool recent =
      c.starved_at && c.seq + 1 - c.starved_at < static_cast<uint32_t>(c.starve_hold);
  const int off = slack - (k.input_slack + (recent ? 1 : 0));
  const double ask = -k.dilate_gain * off * (off < 0 ? 2 : 1);
  c.dilation = 1 + std::clamp(ask, -k.dilate_max, k.dilate_max);
  const auto t = static_cast<double>(tick);
  if (!c.clocked) {
    c.clocked = true;
    c.server_tick = t;
    c.spacing = static_cast<double>(std::max(1, k.send_every));
    c.jitter = 0;
    c.delay = c.want = c.spacing + 1;
    c.render_x256 = 0;
    c.spacing_from = tick;
    return;
  }
  const double late = t - c.server_tick;  // + early, - late against the reckoning
  c.jitter += 0.05 * ((late < 0 ? -late : late) - c.jitter);
  // What this arrival needed: how far the reckoning had run past the newest snapshot before it.
  // A delay below it would have drawn past every snapshot on hand, the others standing still.
  if (!c.needs.empty()) {
    c.needs[c.need_count % c.needs.size()] = c.server_tick - static_cast<double>(c.spacing_from);
    c.need_count++;
    const size_t n = std::min<size_t>(c.need_count, c.needs.size());
    c.sorted.assign(c.needs.begin(), c.needs.begin() + static_cast<std::ptrdiff_t>(n));
    // The quantile rounded down to a rank: 1 is the window's most, 0 its least.
    const auto at = static_cast<size_t>(k.interp_quantile * static_cast<double>(n - 1));
    std::nth_element(c.sorted.begin(), c.sorted.begin() + static_cast<std::ptrdiff_t>(at),
                     c.sorted.end());
    c.want = std::max(c.sorted[at], 1.0) + 0.25;
  }
  c.server_tick += 0.1 * late;
  if (c.spacing_from) c.spacing += 0.1 * (static_cast<double>(tick - c.spacing_from) - c.spacing);
  c.spacing_from = tick;
}

void on_snapshot(ReplicateClient& c, const uint8_t* data, size_t size) {
  Reader r = make_reader(data, size);
  const uint8_t kind = read_u8(r);
  if (kind != kSnapshot && kind != kSnapshotInputs) return;
  // THE PREDICTED WORLD's trailer, read off the end: the other players near and their last inputs.
  size_t trailer = 0;
  if (kind == kSnapshotInputs) {
    if (size < kSnapshotHeader + 3 + 2) return;
    const size_t len = data[size - 2] | (static_cast<size_t>(data[size - 1]) << 8);
    const size_t each = 2 + static_cast<size_t>(c.schema.input_size);
    if (len + 2 > size - kSnapshotHeader - 3 || len % each) return;
    trailer = len + 2;
  }
  const uint32_t tick = read_u32(r), baseline = read_u32(r), applied = read_u32(r);
  const uint16_t own = read_u16(r);
  const auto slack = static_cast<int8_t>(read_u8(r));
  const uint64_t entries = read_varint(r);
  if (r.bad || size < kSnapshotHeader + 3 + trailer) return;
  if (c.has_snapshot && tick <= c.newest) {
    c.stats.dropped++;  // older than one already decoded
    return;
  }
  const Snap* base = baseline ? find_snap(c, baseline) : nullptr;
  if (baseline && !base) {
    c.stats.dropped++;  // its baseline is gone: the next one, against a newer ack, will do
    return;
  }
  const int nf = c.schema.fields;
  std::vector<Row>& rows = c.scratch;
  rows.clear();
  if (base) rows.insert(rows.end(), base->rows.begin(), base->rows.end());
  BitReader br{data + kSnapshotHeader + 3, size - kSnapshotHeader - 3 - trailer};
  bool gone = false;
  for (uint64_t e = 0; e < entries && !br.bad; e++) {
    const auto id = static_cast<uint16_t>(get_bits(br, 16));
    const uint32_t op = get_bits(br, 2);
    auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& x) { return x.id == id; });
    if (op == kGone) {
      if (it != rows.end()) {
        it->id = kNoEntity;
        gone = true;
      }
    } else if (op == kUpdate) {
      const uint32_t mask = get_bits(br, nf);
      if (it == rows.end()) {
        br.bad = true;  // an update to a row the baseline lacks
        break;
      }
      for (int f = 0; f < nf; f++) {
        if (mask & (1u << f)) it->q[f] = get_bits(br, c.schema.spec[f].bits);
      }
    } else if (op == kNew) {
      Row row;
      row.id = id;
      for (int f = 0; f < nf; f++) row.q[f] = get_bits(br, c.schema.spec[f].bits);
      if (it != rows.end()) {
        *it = row;
      } else {
        rows.push_back(row);
      }
    } else {
      br.bad = true;
    }
  }
  if (br.bad) {
    c.stats.dropped++;
    return;
  }
  if (gone)
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [](const Row& x) { return x.id == kNoEntity; }),
        rows.end());
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });
  // Whether the others were being drawn past every snapshot on hand when this one came: the
  // sample blends from where they were carried to back onto the curve.
  const uint32_t drawing = replicate_client_render_tick(c);
  Snap& slot = c.ring[tick % c.ring.size()];
  slot.late_x256 = c.has_snapshot && drawing > (c.newest << 8) ? drawing : 0;
  slot.rows.swap(rows);
  slot.tick = tick;
  slot.valid = true;
  c.newest = tick;
  c.has_snapshot = true;
  c.local_tick = 0;
  c.applied = applied;
  if (own != c.own) c.predicting = false;  // a new entity: nothing to smooth from
  c.own = own;
  c.stats.snapshots++;
  c.stats.bytes += size;
  if (!baseline) c.stats.full++;
  if (c.config.sync) sync_clock(c, tick, slack);
  c.others_id.clear();
  c.others_input.clear();
  if (trailer) {
    const auto n = static_cast<size_t>(c.schema.input_size);
    const uint8_t* t = data + size - trailer;
    for (size_t at = 0; at + 2 + n <= trailer - 2; at += 2 + n) {
      c.others_id.push_back(static_cast<uint16_t>(t[at] | (t[at + 1] << 8)));
      c.others_input.insert(c.others_input.end(), t + at + 2, t + at + 2 + n);
    }
  }
  reconcile(c, slot);
  // A predicted event whose input the server has now applied starts its wait for the server's.
  for (PredictedEvent& pe : c.pending) {
    if (!pe.due && pe.e.seq <= applied) pe.due = c.seq ? c.seq : 1;
  }
}

// The server's event: a predicted one confirmed, or one the client did not predict.
void on_event(ReplicateClient& c, const uint8_t* data, size_t size) {
  Reader r = make_reader(data, size);
  if (read_u8(r) != kEvent) return;
  EventResult e;
  e.kind = read_u8(r);
  e.seq = read_u32(r);
  e.size = read_u8(r);
  if (r.bad || e.size > kMaxEventPayload || remaining(r) != e.size) return;
  std::memcpy(e.payload, data + r.at, e.size);
  // The nearest predicted one of the same kind and payload within the slop: a contact's tick can
  // differ by one or two between the prediction and the server.
  size_t best = c.pending.size();
  uint32_t best_off = 0;
  for (size_t i = 0; i < c.pending.size(); i++) {
    const EventResult& p = c.pending[i].e;
    if (p.kind != e.kind || p.size != e.size || std::memcmp(p.payload, e.payload, e.size) != 0)
      continue;
    const uint32_t off = p.seq > e.seq ? p.seq - e.seq : e.seq - p.seq;
    if (off > static_cast<uint32_t>(c.event_slop) || (best < c.pending.size() && off >= best_off))
      continue;
    best = i;
    best_off = off;
  }
  for (size_t i = best; i < c.pending.size(); i = c.pending.size()) {
    e.outcome = EventOutcome::confirmed;
    c.stats.confirmed++;
    c.outcomes.push_back(e);
    c.pending.erase(c.pending.begin() + static_cast<std::ptrdiff_t>(i));
    return;
  }
  // A contact with an entity the client was not predicting could not have been predicted: heard,
  // not missed.
  bool reach = true;
  if (e.kind == kContactEvent && e.size == 2) {
    const Predicted* p =
        find_predicted(c, static_cast<uint16_t>(e.payload[0] | (e.payload[1] << 8)));
    reach = p != nullptr && p->active;
  }
  e.outcome = reach ? EventOutcome::unpredicted : EventOutcome::heard;
  (reach ? c.stats.unpredicted : c.stats.heard)++;
  c.outcomes.push_back(e);
}

}  // namespace

void replicate_client_init(ReplicateClient& c, const Schema& schema, const ReplicateConfig& config,
                           Node& node, int server_peer) {
  c = ReplicateClient{};
  c.schema = schema;
  c.config = config;
  if (c.config.ring < 2) c.config.ring = 2;
  if (c.config.tick_hz < 1) c.config.tick_hz = 1;
  c.node = &node;
  c.server = server_peer;
  c.ring.resize(static_cast<size_t>(c.config.ring));
  for (Snap& s : c.ring) s.rows.reserve(static_cast<size_t>(config.max_entities));
  c.scratch.reserve(static_cast<size_t>(config.max_entities));
  c.history.assign(kInputRing * static_cast<size_t>(schema.input_size), 0);
  c.needs.assign(static_cast<size_t>(std::max(1, c.config.interp_window)), 0.0);
  c.sorted.reserve(c.needs.size());
  // The durations in ticks at this rate. A critically damped spring's error halves in about its
  // half-life: w = 2 ln 2 / half-life.
  const auto hz = static_cast<double>(c.config.tick_hz);
  c.spring_w = 1.3862943611198906 / std::max(0.25, c.config.spring_halflife_s * hz);
  c.spring_decay = core::libm::exp(-c.spring_w);
  c.starve_hold = std::max(1, static_cast<int>(c.config.starve_hold_s * hz));
  c.extrapolate_ticks = std::max(0.0, c.config.extrapolate_s * hz);
  c.event_wait = std::max(1, static_cast<int>(c.config.event_wait_s * hz));
  c.event_slop = std::max(0, static_cast<int>(c.config.event_slop_s * hz));
  c.predict_max = std::max(0, static_cast<int>(c.config.predict_max_s * hz));
  c.world.assign(static_cast<size_t>(std::clamp(c.config.max_predicted, 0, 32)), Predicted{});
  c.others_id.reserve(kMaxPeers);
  c.others_input.reserve(kMaxPeers * static_cast<size_t>(std::max(1, schema.input_size)));
  c.pending.reserve(64);
  c.outcomes.reserve(64);
  c.blend_ticks = std::max(0.0, c.config.extrapolate_blend_s * hz);
}

void replicate_client_receive(ReplicateClient& c) {
  for (const Event& e : c.node->events) {
    if (e.type != EventType::message || e.peer != c.server) continue;
    if (e.channel == c.config.snapshot_channel) {
      on_snapshot(c, event_data(*c.node, e), e.size);
    } else if (e.channel == c.config.event_channel) {
      on_event(c, event_data(*c.node, e), e.size);
    }
  }
}

void replicate_client_input(ReplicateClient& c, const uint8_t* input) {
  // Nothing is sent before the first snapshot: an input the connection could not carry yet would
  // be one the prediction replays and the server never applied.
  if (!c.has_snapshot) return;
  const auto n = static_cast<size_t>(c.schema.input_size);
  c.seq++;
  std::memcpy(&c.history[(c.seq % kInputRing) * n], input, n);
  c.local_tick++;
  if (c.config.sync && c.clocked) {
    // A tick of the server's per tick of the server's: this one runs `dilation` times as fast.
    c.server_tick += 1 / c.dilation;
    // The delay grows quickly (the others slowed a few percent, so they do not stand still) and
    // shrinks slowly (sped a little), so a burst's need outlasts the burst.
    c.delay += std::clamp(c.want - c.delay, -c.config.interp_ease / 4, c.config.interp_ease);
    const double at = c.server_tick - c.delay;
    const uint32_t x256 = at <= 0 ? 0 : static_cast<uint32_t>(at * 256);
    c.render_x256 = std::max(c.render_x256, x256);
  }
  if (c.predicting) {
    step_own(c, c.predicted, input);
    step_world(c, c.predicted, true);
  }
  // The predicted world's errors drawn out, and a fading one freed once it is on the curve.
  for (Predicted& p : c.world) {
    if (p.id == kNoEntity) continue;
    spring(c, p.error, p.error_v);
    if (p.active) continue;
    double most = 0;
    for (int f = 0; f < c.schema.fields; f++) most = std::max(most, std::abs(p.error[f]));
    if (most < 1e-4) p = Predicted{};
  }
  // A predicted event the server has applied the input of and not sent within the wait: undone.
  for (size_t i = 0; i < c.pending.size();) {
    PredictedEvent& pe = c.pending[i];
    if (!pe.due || c.seq - pe.due <= static_cast<uint32_t>(c.event_wait)) {
      i++;
      continue;
    }
    pe.e.outcome = EventOutcome::undone;
    c.stats.undone++;
    c.outcomes.push_back(pe.e);
    c.pending.erase(c.pending.begin() + static_cast<std::ptrdiff_t>(i));
  }
  if (c.config.spring) {
    spring(c, c.error, c.error_v);
  } else {
    for (int f = 0; f < c.schema.fields; f++) c.error[f] *= c.config.smoothing;
  }
  // cmd_rate: a message every tick_hz / cmd_rate ticks, carrying every input since the last one
  // and `redundancy` before them.
  c.unsent++;
  const int every =
      c.config.cmd_rate > 0
          ? std::max(1, (c.config.tick_hz + c.config.cmd_rate - 1) / c.config.cmd_rate)
          : 1;
  if (c.unsent < every) return;
  const auto room = static_cast<uint32_t>(
      std::min<size_t>(255, (kMaxMessage - kInputHeader) / std::max<size_t>(1, n)));
  const auto want = static_cast<uint32_t>(std::max(1, c.config.redundancy) + c.unsent - 1);
  const uint32_t count = std::min({c.seq, want, room, kInputRing});
  c.unsent = 0;
  uint8_t out[kMaxMessage];
  Writer w = make_writer(out, sizeof out);
  write_u8(w, kInput);
  write_u32(w, c.has_snapshot ? c.newest : 0);
  write_u32(w, replicate_client_render_tick(c));
  write_u32(w, c.config.rate);
  write_u16(w, static_cast<uint16_t>(std::clamp(c.config.update_rate, 0, 65535)));
  write_u32(w, c.seq);
  write_u8(w, static_cast<uint8_t>(count));
  for (uint32_t s = c.seq - count + 1; s <= c.seq; s++)
    write_bytes(w, &c.history[(s % kInputRing) * n], n);
  if (!w.overflow) node_send(*c.node, c.server, c.config.input_channel, out, w.size);
}

void replicate_client_predicted(const ReplicateClient& c, double* out) {
  for (int f = 0; f < c.schema.fields; f++) out[f] = c.predicted[f] + c.error[f];
}

uint32_t replicate_client_render_tick(const ReplicateClient& c) {
  if (!c.has_snapshot) return 0;
  if (c.config.sync) return c.render_x256;
  const int64_t t = int64_t{c.newest} + c.local_tick - c.config.interp_ticks;
  return t <= 0 ? 0 : static_cast<uint32_t>(t) << 8;
}

bool replicate_client_sample(const ReplicateClient& c, uint16_t id, double* out) {
  const Predicted* p = find_predicted(c, id);
  if (!p) return sample_curve(c, id, out);
  const int nf = c.schema.fields;
  if (!p->active && !sample_curve(c, id, out)) std::memcpy(out, p->state, sizeof(double) * nf);
  for (int f = 0; f < nf; f++) out[f] = (p->active ? p->state[f] : out[f]) + p->error[f];
  return true;
}

bool replicate_client_predicts(const ReplicateClient& c, uint16_t id) {
  const Predicted* p = find_predicted(c, id);
  return p && p->active;
}

void replicate_client_predict_event(ReplicateClient& c, uint8_t kind, const uint8_t* payload,
                                    int size) {
  if (c.config.event_channel < 0) return;
  PredictedEvent pe;
  pe.e.kind = kind;
  pe.e.seq = c.seq;
  pe.e.size = static_cast<uint8_t>(std::clamp(size, 0, kMaxEventPayload));
  if (pe.e.size) std::memcpy(pe.e.payload, payload, pe.e.size);
  c.pending.push_back(pe);
}

}  // namespace ge::net
