// The fuzzer: datagrams made or mutated each tick of an honest fake-traffic session, put on the
// simulated network beside it and run through the packet parser, and the verdict that the honest
// session never noticed. The fuzz draws off its own stream, never the network's, so the same
// plan is the same fuzz.
//
// ge:summary The fuzzer: random and mutated datagrams beside an honest session, and its verdict
// ge:keywords net fuzz fuzzer mutate bit-flip truncate splice spoof datagram parser robustness
// ge:skill net
// ge:test tests/net_stress_test.cpp
#include <algorithm>
#include <cmath>

#include "core/prng.hpp"
#include "net/packet.hpp"
#include "net/stress.hpp"

namespace ge::net {

namespace {

enum FuzzKind : uint8_t { kRandom, kFlip, kCut, kLong, kSplice, kHeader, kRequest, kPayload };

constexpr const char* kKindNames[kFuzzKinds] = {"random", "flip",   "cut",     "long",
                                                "splice", "header", "request", "payload"};

struct Fuzzer {
  const FuzzPlan* plan = nullptr;
  FuzzReport* report = nullptr;
  core::Rng rng{1};
  uint32_t protocol = 0;
  int64_t sending_ticks = 1;
  uint64_t made = 0;
  uint8_t bytes[kMaxDatagram];
};

uint32_t word(core::Rng& r) {
  return static_cast<uint32_t>(std::floor(core::next(r) * 4294967296.0));
}

void fill(core::Rng& r, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; i++) out[i] = static_cast<uint8_t>(word(r));
}

// A datagram in flight on the network, or null when nothing is.
const Datagram* in_flight(Fuzzer& f, const SimNetwork& net) {
  if (net.heap.empty()) return nullptr;
  const auto& p = net.heap[core::pick_index(f.rng, net.heap.size())];
  return &net.store[p.slot];
}

Address stranger(Fuzzer& f) {
  return sim_address(1000 + static_cast<uint32_t>(core::int_in(f.rng, 0, 63)),
                     static_cast<uint16_t>(core::int_in(f.rng, 1, 999)));
}

// One datagram: its bytes in f.bytes, its size, and where it goes from and to.
size_t make_one(Fuzzer& f, const SimNetwork& net, int kind, Address& from, Address& to) {
  const int clients = std::min(f.plan->session.clients, f.plan->session.max_peers);
  from = stranger(f);
  // The host, mostly; a client sometimes.
  to = core::chance(f.rng, 0.7) || clients == 0
           ? sim_address(0, 1)
           : sim_address(static_cast<uint32_t>(core::int_in(f.rng, 1, clients)), 1);
  const Datagram* src = kind >= kFlip && kind <= kSplice ? in_flight(f, net) : nullptr;
  if (kind >= kFlip && kind <= kSplice && !src) kind = kRandom;
  if (src) {
    to = src->to;
    if (f.plan->spoof && core::chance(f.rng, 0.5)) from = src->from;
  }
  size_t n = 0;
  switch (kind) {
    case kRandom:
      n = static_cast<size_t>(core::int_in(f.rng, 0, static_cast<int32_t>(kMaxDatagram)));
      fill(f.rng, f.bytes, n);
      break;
    case kFlip: {
      n = src->size;
      std::copy(src->data, src->data + n, f.bytes);
      const int flips = core::int_in(f.rng, 1, 8);
      for (int i = 0; i < flips && n; i++) {
        const size_t at = core::pick_index(f.rng, n);
        f.bytes[at] ^= static_cast<uint8_t>(1u << core::int_in(f.rng, 0, 7));
      }
      break;
    }
    case kCut:
      n = src->size ? core::pick_index(f.rng, src->size) : 0;
      std::copy(src->data, src->data + n, f.bytes);
      break;
    case kLong: {
      std::copy(src->data, src->data + src->size, f.bytes);
      const size_t extra = core::pick_index(f.rng, kMaxDatagram - src->size + 1);
      fill(f.rng, f.bytes + src->size, extra);
      n = src->size + extra;
      break;
    }
    case kSplice: {
      const Datagram* other = in_flight(f, net);
      const size_t head = src->size ? core::pick_index(f.rng, src->size + 1) : 0;
      const size_t tail_from = other->size ? core::pick_index(f.rng, other->size + 1) : 0;
      const size_t tail = std::min<size_t>(other->size - tail_from, kMaxDatagram - head);
      std::copy(src->data, src->data + head, f.bytes);
      std::copy(other->data + tail_from, other->data + tail_from + tail, f.bytes + head);
      n = head + tail;
      break;
    }
    case kHeader: {
      // The game's own protocol id and any type byte, then garbage: past the first gate.
      n = static_cast<size_t>(core::int_in(f.rng, 5, static_cast<int32_t>(kMaxPacket)));
      Writer w = make_writer(f.bytes, n);
      write_u32(w, f.protocol);
      write_u8(w, static_cast<uint8_t>(core::int_in(f.rng, 0, 9)));
      fill(f.rng, f.bytes + 5, n - 5);
      break;
    }
    case kRequest: {
      // A well-formed request or a response with a made-up cookie: a handshake flood.
      Writer w = make_writer(f.bytes, kMaxPacket);
      Handshake h;
      h.salt = (static_cast<uint64_t>(word(f.rng)) << 32) | word(f.rng);
      h.value = (static_cast<uint64_t>(word(f.rng)) << 32) | word(f.rng);
      write_handshake(w, f.protocol,
                      core::chance(f.rng, 0.5) ? PacketType::request : PacketType::response, h);
      n = w.size;
      break;
    }
    case kPayload: {
      // A payload with a guessed key, any sequence and acks, and messages of any shape.
      Writer w = make_writer(f.bytes, kMaxPacket);
      PayloadHeader h;
      h.key = (static_cast<uint64_t>(word(f.rng)) << 32) | word(f.rng);
      h.seq = static_cast<uint16_t>(word(f.rng));
      h.ack = static_cast<uint16_t>(word(f.rng));
      h.ack_bits = word(f.rng);
      write_payload_header(w, f.protocol, h);
      const int messages = core::int_in(f.rng, 0, 6);
      uint8_t body[64];
      for (int i = 0; i < messages; i++) {
        const size_t size = core::pick_index(f.rng, sizeof body + 1);
        fill(f.rng, body, size);
        if (w.size + message_wire_size(true, size) > kMaxPacket) break;
        write_message(w, static_cast<uint8_t>(core::int_in(f.rng, 0, 9)), core::chance(f.rng, 0.5),
                      static_cast<uint16_t>(word(f.rng)), body, size, core::chance(f.rng, 0.2));
      }
      n = w.size;
      break;
    }
    default:
      break;
  }
  f.report->by_kind[kind]++;
  return n;
}

// The parser on its own, the whole way through: the start, the body, every message, the line.
void parse(Fuzzer& f, const uint8_t* data, size_t n) {
  Reader r = make_reader(data, n);
  uint32_t protocol;
  PacketType type;
  if (read_packet_start(r, protocol, type)) {
    f.report->parsed++;
    if (type == PacketType::payload) {
      PayloadHeader h;
      if (read_payload_header(r, h)) {
        MessageView m;
        while (read_message(r, m)) {
        }
      }
    } else {
      Handshake h;
      read_handshake(r, type, h);
    }
  }
  f.report->described += !describe_packet(data, n).empty();
}

void fuzz_tick(void* ctx, SimNetwork& net, int64_t /*now_us*/, int64_t tick) {
  Fuzzer& f = *static_cast<Fuzzer*>(ctx);
  if (tick >= f.sending_ticks) return;
  const auto target = static_cast<uint64_t>(
      std::floor(static_cast<double>(f.plan->datagrams) * static_cast<double>(tick + 1) /
                 static_cast<double>(f.sending_ticks)));
  for (; f.made < target; f.made++) {
    const int kind = core::int_in(f.rng, 0, kFuzzKinds - 1);
    Address from, to;
    const size_t n = make_one(f, net, kind, from, to);
    parse(f, f.bytes, n);
    if (sim_send(net, from, to, f.bytes, n)) f.report->sent++;
  }
}

}  // namespace

const char* fuzz_kind_name(int k) { return k >= 0 && k < kFuzzKinds ? kKindNames[k] : "?"; }

FuzzReport run_fuzz(const FuzzPlan& plan) {
  FuzzReport rep;
  Fuzzer f;
  f.plan = &plan;
  f.report = &rep;
  f.rng = core::make_rng(plan.seed * 2654435761u + 7);
  f.protocol = traffic_protocol();
  f.sending_ticks = std::max<int64_t>(
      1, static_cast<int64_t>(std::floor(plan.session.seconds * plan.session.tick_hz)));
  TrafficPlan session = plan.session;
  session.hook = fuzz_tick;
  session.hook_ctx = &f;
  rep.session = run_traffic(session);
  StressBounds bounds;
  bounds.input_p95_ms = 1e9;  // the fuzz shares no link with the session; only losses count
  bounds.reliable_p95_ms = 1e9;
  bounds.snapshot_share = 0;
  rep.why = stress_breaks(rep.session, session, bounds);
  if (!rep.why && rep.session.host_zombies > 0) rep.why = "the host gave a slot to a stranger";
  if (!rep.why &&
      rep.session.ended_connected < std::min(plan.session.clients, plan.session.max_peers))
    rep.why = "a client lost its connection";
  rep.honest = rep.why == nullptr;
  uint64_t h = rep.session.digest;
  for (uint64_t c : rep.by_kind) h = (h ^ c) * 0x100000001b3ull;
  h = (h ^ rep.parsed) * 0x100000001b3ull;
  rep.hash = (h ^ rep.described) * 0x100000001b3ull;
  return rep;
}

}  // namespace ge::net
