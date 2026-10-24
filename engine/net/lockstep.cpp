// Lockstep and rollback over a node (net/lockstep.hpp): the turns, the message, the digests.
//
// THE MESSAGE (one a send, to every peer alike):
//   u8 'L', u8 seat, varint first, u8 count, count x (u8 size, size bytes)   this seat's inputs
//   u8 seats, seats x varint have           turns of each seat's inputs the sender holds
//   varint dfirst, u8 dcount, dcount x u64  this seat's final digests
//   seats x varint have_digests             steps of each seat's digests the sender holds
// A receiver reads its own entry of each `have` list as its ack of what it sent.
//
// ge:summary Lockstep's turns, rollback's resimulation, the inputs message and the digest check
// ge:keywords net lockstep rollback turns message acks digest desync resimulate predict stall
// ge:skill net
// ge:test tests/net_lockstep_test.cpp
#include "net/lockstep.hpp"

#include <algorithm>
#include <cstring>

#include "net/packet.hpp"
#include "net/wire.hpp"

namespace ge::net {

namespace {

constexpr uint8_t kKind = 'L';
constexpr int kMaxDigestsSent = 32;

bool rolling(const Lockstep& ls) { return ls.config.mode == LockstepMode::rollback; }

TurnInput& input_at(Lockstep& ls, LockstepSeat& s, uint32_t turn) {
  (void)ls;
  return s.inputs[turn % s.inputs.size()];
}

bool same(const TurnInput& a, const TurnInput& b) {
  return a.size == b.size && std::memcmp(a.bytes, b.bytes, a.size) == 0;
}

// The oldest turn of any seat's inputs this peer may still read: below it a ring slot is free.
uint32_t floor_turn(const Lockstep& ls) { return rolling(ls) ? ls.confirmed : lockstep_turn(ls); }

// The fewest turns of this seat's inputs any other seat still in the session holds.
uint32_t min_acked(const Lockstep& ls) {
  uint32_t m = ls.closed;
  for (int k = 0; k < ls.config.seats; k++) {
    if (k == ls.config.seat || ls.seats[static_cast<size_t>(k)].lost) continue;
    m = std::min(m, ls.seats[static_cast<size_t>(k)].acked);
  }
  return m;
}

uint32_t min_acked_digests(const Lockstep& ls) {
  uint32_t m = ls.final;
  for (int k = 0; k < ls.config.seats; k++) {
    if (k == ls.config.seat || ls.seats[static_cast<size_t>(k)].lost) continue;
    m = std::min(m, ls.seats[static_cast<size_t>(k)].acked_digests);
  }
  return m;
}

bool all_have(const Lockstep& ls, uint32_t turn) {
  for (const LockstepSeat& s : ls.seats)
    if (s.have <= turn) return false;
  return true;
}

// Close the pending input as this seat's input for the next turn it owes.
void close_pending(Lockstep& ls) {
  LockstepSeat& me = ls.seats[static_cast<size_t>(ls.config.seat)];
  TurnInput& in = input_at(ls, me, ls.closed);
  in = ls.pending;
  in.real = true;
  ls.closed++;
  me.have = ls.closed;
  me.newest = in;
  me.newest_turn = ls.closed - 1;
  me.has_newest = true;
  if (!rolling(ls)) ls.pending = TurnInput{};
}

// Run step `s` once with the best inputs this peer holds, keeping its digest.
void run_step(Lockstep& ls, uint32_t s) {
  const int n = ls.config.seats;
  const uint8_t* ptrs[kMaxSeats];
  uint8_t sizes[kMaxSeats];
  const auto ts = static_cast<uint32_t>(ls.config.turn_steps);
  const bool first = s % ts == 0;
  const uint32_t turn = s / ts;
  std::vector<TurnInput>* used = rolling(ls) ? &ls.used[s % ls.used.size()] : nullptr;
  for (int k = 0; k < n; k++) {
    LockstepSeat& seat = ls.seats[static_cast<size_t>(k)];
    const TurnInput* in = nullptr;
    static const TurnInput kNone{};
    if (!first) {
      in = &kNone;
    } else if (seat.have > turn) {
      in = &input_at(ls, seat, turn);
    } else {
      in = seat.has_newest ? &seat.newest : &kNone;  // rollback: the seat's newest, predicted
    }
    if (used) {
      TurnInput& u = (*used)[static_cast<size_t>(k)];
      u = *in;
      u.real = seat.have > turn;
      in = &u;
    }
    ptrs[k] = in->bytes;
    sizes[k] = in->size;
  }
  if (rolling(ls)) ls.game.save(ls.game.ctx, static_cast<int>(s % (ls.config.max_rollback + 1)));
  ls.game.step(ls.game.ctx, s, ptrs, sizes);
  LockstepSeat& me = ls.seats[static_cast<size_t>(ls.config.seat)];
  me.digests[s % me.digests.size()] = ls.game.digest(ls.game.ctx);
}

void check_digests(Lockstep& ls) {
  const LockstepSeat& me = ls.seats[static_cast<size_t>(ls.config.seat)];
  const auto h = static_cast<uint32_t>(ls.history);
  for (int k = 0; k < ls.config.seats; k++) {
    if (k == ls.config.seat) continue;
    LockstepSeat& s = ls.seats[static_cast<size_t>(k)];
    if (ls.final > h) s.checked = std::max(s.checked, ls.final - h);  // too old to compare
    const uint32_t upto = std::min(s.have_digests, ls.final);
    for (; s.checked < upto; s.checked++) {
      const uint64_t mine = me.digests[s.checked % h], theirs = s.digests[s.checked % h];
      if (mine == theirs || ls.desync.found) continue;
      ls.desync = {true, s.checked, k, mine, theirs, ls.step};
    }
  }
}

void send(Lockstep& ls) {
  const LockstepSeat& me = ls.seats[static_cast<size_t>(ls.config.seat)];
  Writer w = make_writer(ls.scratch.data(), ls.scratch.size());
  write_u8(w, kKind);
  write_u8(w, static_cast<uint8_t>(ls.config.seat));
  const uint32_t first = min_acked(ls);
  write_varint(w, first);
  const size_t count_at = w.size;
  write_u8(w, 0);
  // Room for the acks and the digests after the inputs.
  const size_t tail =
      2 + 2 * static_cast<size_t>(ls.config.seats) * 5 + 6 + size_t{kMaxDigestsSent} * 8;
  int count = 0;
  for (uint32_t t = first; t < ls.closed && count < 255; t++) {
    const TurnInput& in = me.inputs[t % me.inputs.size()];
    if (w.size + 1 + in.size + tail > ls.scratch.size()) break;
    write_u8(w, in.size);
    write_bytes(w, in.bytes, in.size);
    count++;
  }
  ls.scratch[count_at] = static_cast<uint8_t>(count);
  write_u8(w, static_cast<uint8_t>(ls.config.seats));
  for (const LockstepSeat& s : ls.seats) write_varint(w, s.have);
  const uint32_t dfirst =
      std::max(min_acked_digests(ls), ls.final > static_cast<uint32_t>(ls.history)
                                          ? ls.final - static_cast<uint32_t>(ls.history)
                                          : 0);
  const uint32_t dcount = std::min<uint32_t>(ls.final - dfirst, kMaxDigestsSent);
  write_varint(w, dfirst);
  write_u8(w, static_cast<uint8_t>(dcount));
  for (uint32_t s = dfirst; s < dfirst + dcount; s++)
    write_u64(w, me.digests[s % me.digests.size()]);
  for (int k = 0; k < ls.config.seats; k++)
    write_varint(w, k == ls.config.seat ? ls.final : ls.seats[static_cast<size_t>(k)].have_digests);
  if (w.overflow) return;
  node_broadcast(*ls.node, ls.config.channel, ls.scratch.data(), w.size);
  ls.stats.messages++;
  ls.stats.bytes += w.size;
}

void on_message(Lockstep& ls, int peer, const uint8_t* data, size_t size) {
  Reader r = make_reader(data, size);
  if (read_u8(r) != kKind) {
    ls.stats.bad++;
    return;
  }
  const int k = read_u8(r);
  if (k >= ls.config.seats || k == ls.config.seat) {
    ls.stats.bad++;
    return;
  }
  LockstepSeat& s = ls.seats[static_cast<size_t>(k)];
  const auto first = static_cast<uint32_t>(read_varint(r));
  const int count = read_u8(r);
  const uint32_t floor = floor_turn(ls);
  const auto window = static_cast<uint32_t>(ls.window);
  for (int i = 0; i < count; i++) {
    const uint8_t n = read_u8(r);
    const uint8_t* bytes = read_view(r, n);
    if (!bytes || n > kMaxOrder) {
      ls.stats.bad++;
      return;
    }
    const uint32_t t = first + static_cast<uint32_t>(i);
    if (t < s.have) {
      ls.stats.stale++;
      continue;
    }
    if (t > s.have || t >= floor + window) break;  // a gap, or no room yet: it comes again
    TurnInput in;
    in.size = n;
    in.real = true;
    std::memcpy(in.bytes, bytes, n);
    if (rolling(ls) && t < ls.step) {
      const TurnInput& was = ls.used[t % ls.used.size()][static_cast<size_t>(k)];
      if (!was.real && !same(was, in)) {
        ls.stats.mispredicted++;
        if (ls.rollback_to < 0 || t < ls.rollback_to) ls.rollback_to = t;
      }
    }
    input_at(ls, s, t) = in;
    s.have = t + 1;
    s.newest = in;
    s.newest_turn = t;
    s.has_newest = true;
  }
  if (read_u8(r) != ls.config.seats) {
    ls.stats.bad++;
    return;
  }
  for (int j = 0; j < ls.config.seats; j++) {
    const auto v = static_cast<uint32_t>(read_varint(r));
    if (j == ls.config.seat) s.acked = std::max(s.acked, std::min(v, ls.closed));
  }
  const auto dfirst = static_cast<uint32_t>(read_varint(r));
  const int dcount = read_u8(r);
  const auto h = static_cast<uint32_t>(ls.history);
  for (int i = 0; i < dcount; i++) {
    const uint64_t d = read_u64(r);
    const uint32_t step = dfirst + static_cast<uint32_t>(i);
    if (step != s.have_digests || step >= s.checked + h) continue;
    s.digests[step % h] = d;
    s.have_digests = step + 1;
  }
  for (int j = 0; j < ls.config.seats; j++) {
    const auto v = static_cast<uint32_t>(read_varint(r));
    if (j == ls.config.seat) s.acked_digests = std::max(s.acked_digests, std::min(v, ls.final));
  }
  if (r.bad) {
    ls.stats.bad++;
    return;
  }
  s.peer = peer;
  ls.stats.received++;
}

void advance_confirmed(Lockstep& ls) {
  while (ls.confirmed < ls.step && all_have(ls, ls.confirmed)) ls.confirmed++;
  ls.final = ls.confirmed;
}

int update_rollback(Lockstep& ls) {
  if (ls.rollback_to >= 0) {
    const auto from = static_cast<uint32_t>(ls.rollback_to);
    ls.rollback_to = -1;
    ls.game.load(ls.game.ctx, static_cast<int>(from % (ls.config.max_rollback + 1)));
    for (uint32_t s = from; s < ls.step; s++) run_step(ls, s);
    const auto depth = static_cast<int>(ls.step - from);
    ls.stats.rollbacks++;
    ls.stats.resimulated += static_cast<uint64_t>(depth);
    ls.stats.deepest = std::max(ls.stats.deepest, depth);
  }
  advance_confirmed(ls);
  const auto most = static_cast<uint32_t>(ls.config.max_rollback);
  if (ls.step - ls.confirmed >= most ||
      ls.closed - min_acked(ls) >= static_cast<uint32_t>(ls.window))
    return 0;
  close_pending(ls);
  run_step(ls, ls.step);
  ls.step++;
  advance_confirmed(ls);
  return 1;
}

int update_lockstep(Lockstep& ls) {
  const auto ts = static_cast<uint32_t>(ls.config.turn_steps);
  if (ls.step % ts == 0) {
    const uint32_t turn = ls.step / ts;
    if (!all_have(ls, turn) || ls.closed - min_acked(ls) >= static_cast<uint32_t>(ls.window))
      return 0;
    close_pending(ls);
  }
  run_step(ls, ls.step);
  ls.step++;
  ls.final = ls.step;
  return 1;
}

}  // namespace

bool lockstep_init(Lockstep& ls, const LockstepConfig& config, const LockstepGame& game, Node& node,
                   const char** why) {
  const char* reason = nullptr;
  if (config.seats < 1 || config.seats > kMaxSeats)
    reason = "seats must be 1 to 32";
  else if (config.seat < 0 || config.seat >= config.seats)
    reason = "seat must be one of the seats";
  else if (config.turn_steps < 1 || config.delay < 0 || config.send_every < 1)
    reason = "turn_steps and send_every must be 1 or more, delay 0 or more";
  else if (config.mode == LockstepMode::rollback && config.turn_steps != 1)
    reason = "rollback steps one turn a step: turn_steps must be 1";
  else if (config.mode == LockstepMode::rollback &&
           (config.max_rollback < 1 || !game.save || !game.load))
    reason = "rollback needs max_rollback 1 or more and the game's save and load";
  else if (!game.step || !game.digest)
    reason = "the game must give its step and its digest";
  else if (config.channel < 0 || config.channel >= node.config.channels.count ||
           node.config.channels.delivery[config.channel] != Delivery::unreliable)
    reason = "the channel must be one of the node's unreliable channels";
  if (why) *why = reason;
  if (reason) return false;
  ls = Lockstep{};
  ls.config = config;
  ls.game = game;
  ls.node = &node;
  const int ahead =
      config.delay + (config.mode == LockstepMode::rollback ? config.max_rollback : 0);
  ls.window = config.window > 0 ? config.window : 4 * ahead + 16;
  ls.history = std::max(256, 4 * ls.window * config.turn_steps);
  ls.seats.resize(static_cast<size_t>(config.seats));
  const auto delay = static_cast<uint32_t>(config.delay);
  for (LockstepSeat& s : ls.seats) {
    s.inputs.assign(static_cast<size_t>(ls.window), TurnInput{});
    for (TurnInput& in : s.inputs) in.real = true;  // the turns below the delay: empty, held
    s.digests.assign(static_cast<size_t>(ls.history), 0);
    s.have = delay;
    s.acked = delay;
  }
  ls.closed = delay;
  if (config.mode == LockstepMode::rollback)
    ls.used.assign(static_cast<size_t>(ls.window),
                   std::vector<TurnInput>(static_cast<size_t>(config.seats)));
  ls.scratch.assign(kMaxMessage, 0);
  return true;
}

void lockstep_receive(Lockstep& ls) {
  const Node& n = *ls.node;
  for (const Event& e : n.events) {
    if (e.type == EventType::message && e.channel == ls.config.channel) {
      on_message(ls, e.peer, event_data(n, e), e.size);
    } else if (e.type == EventType::disconnected) {
      for (LockstepSeat& s : ls.seats)
        if (s.peer == e.peer) s.lost = true;
    }
  }
}

bool lockstep_input(Lockstep& ls, const uint8_t* bytes, size_t size) {
  if (ls.pending.size + size > kMaxOrder) return false;
  std::memcpy(ls.pending.bytes + ls.pending.size, bytes, size);
  ls.pending.size = static_cast<uint8_t>(ls.pending.size + size);
  return true;
}

void lockstep_set_input(Lockstep& ls, const uint8_t* bytes, size_t size) {
  size = std::min<size_t>(size, kMaxOrder);
  std::memcpy(ls.pending.bytes, bytes, size);
  ls.pending.size = static_cast<uint8_t>(size);
}

int lockstep_update(Lockstep& ls) {
  ls.stats.updates++;
  const int ran = rolling(ls) ? update_rollback(ls) : update_lockstep(ls);
  ls.stats.steps += static_cast<uint64_t>(ran);
  if (!ran) ls.stats.stalls++;
  check_digests(ls);
  if (ls.stats.updates % static_cast<uint64_t>(ls.config.send_every) == 0) send(ls);
  return ran;
}

uint32_t lockstep_waiting(const Lockstep& ls) {
  uint32_t turn = 0;
  if (rolling(ls)) {
    if (ls.step - ls.confirmed < static_cast<uint32_t>(ls.config.max_rollback)) return 0;
    turn = ls.confirmed;
  } else {
    if (ls.step % static_cast<uint32_t>(ls.config.turn_steps) != 0) return 0;
    turn = lockstep_turn(ls);
  }
  uint32_t mask = 0;
  for (int k = 0; k < ls.config.seats; k++)
    if (ls.seats[static_cast<size_t>(k)].have <= turn) mask |= 1u << k;
  return mask;
}

}  // namespace ge::net
