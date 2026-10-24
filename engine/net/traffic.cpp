// The fake-traffic session: the nodes, the clock, the streams and their tallies, the series and
// the digest. Each message is [kind u8][client u16][count u32][sent_us u64] then filler bytes, so
// the receiver checks its stream's order and measures its latency on the shared virtual clock.
//
// ge:summary The fake-traffic session's loop, its streams' tallies, series and digest
// ge:keywords net traffic session loop tally latency digest series
// ge:skill net
// ge:test tests/net_traffic_test.cpp
#include "net/traffic.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/num_text.hpp"
#include "net/node.hpp"

namespace ge::net {

namespace {

enum Kind : uint8_t { kInput = 1, kSnapshot = 2, kEventUp = 3, kEventDown = 4, kBulk = 5 };
constexpr size_t kHeader = 1 + 2 + 4 + 8;

struct Fnv {
  uint64_t h = 0xcbf29ce484222325ull;
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) {
      h ^= b[i];
      h *= 0x100000001b3ull;
    }
  }
  template <class T>
  void value(T v) {
    bytes(&v, sizeof v);
  }
};

struct Stream {
  uint32_t next_send = 0;
  uint32_t base = 0;  // next_send when its sender last (re)started: what `due` counts from
  uint32_t expect = 0;
};

struct Side {
  Transport transport;
  Node node;
  int64_t connected_us = -1;
  bool gone = false;  // denied, failed or dropped: sends nothing until it tries again
  Stream inputs, events_up, snapshots, events_down, bulk;  // up sent here, down received here
  // CHAOS: the client's own clock (its offset from the network's, and its rate), its port, and
  // its tries at connecting again.
  double offset_us = 0, rate = 1;
  uint16_t port = 1;
  int tries_left = 0;
  int64_t retry_at_us = -1, lost_at_us = -1;
  bool ever_connected = false, gave_up = false;
};

int64_t side_now(const Side& c, int64_t now) {
  return now + static_cast<int64_t>(std::floor(c.offset_us));
}

void make_message(uint8_t* out, size_t size, Kind kind, int client, uint32_t count, int64_t now) {
  Writer w = make_writer(out, size);
  write_u8(w, kind);
  write_u16(w, static_cast<uint16_t>(client));
  write_u32(w, count);
  write_u64(w, static_cast<uint64_t>(now));
  for (size_t i = kHeader; i < size; i++) out[i] = static_cast<uint8_t>(count + i);
}

// A message's filler is what make_message wrote: a big message joined whole and in order.
bool filler_whole(const uint8_t* d, size_t size, uint32_t count) {
  for (size_t i = kHeader; i < size; i++) {
    if (d[i] != static_cast<uint8_t>(count + i)) return false;
  }
  return true;
}

// How many of a stream are due `elapsed_us` after it started at `hz`.
uint32_t due(int64_t elapsed_us, double hz) {
  if (elapsed_us < 0 || hz <= 0) return 0;
  return static_cast<uint32_t>(std::floor(static_cast<double>(elapsed_us) * hz / 1e6)) + 1;
}

void tally(StreamTally& t, Stream& s, uint32_t count, int64_t sent_us, int64_t now_us, bool ordered,
           bool keep) {
  if (ordered) {
    if (count < s.expect) {
      t.duplicated++;
      return;
    }
    if (count > s.expect) t.out_of_order++;
    s.expect = count + 1;
  }
  t.delivered++;
  const double ms = static_cast<double>(now_us - sent_us) / 1000.0;
  if (keep) t.latency_ms.push_back(ms);
  t.latency_sum_ms += ms;
  if (ms > t.latency_max_ms) t.latency_max_ms = ms;
}

}  // namespace

const char* chaos_kind_name(ChaosKind k) {
  switch (k) {
    case ChaosKind::link:
      return "link";
    case ChaosKind::restore:
      return "restore";
    case ChaosKind::host_down:
      return "host_down";
    case ChaosKind::host_up:
      return "host_up";
    case ChaosKind::rebind:
      return "rebind";
    case ChaosKind::skew:
      return "skew";
  }
  return "?";
}

uint32_t traffic_protocol() { return protocol_id("ge-net-traffic", 1); }

size_t sim_held_bytes(const SimNetwork& net) {
  size_t b = net.store.capacity() * sizeof(Datagram) + net.heap.capacity() * sizeof(net.heap[0]) +
             net.free_slots.capacity() * 4;
  for (const auto& sock : net.sockets) b += sock.inbox.capacity() * 4;
  for (const SimNat& nat : net.nats) b += nat.maps.capacity() * sizeof(SimNat::Mapping);
  return b;
}

size_t node_held_bytes(const Node& node) {
  size_t b = node.events.capacity() * sizeof(Event) + node.event_bytes.capacity() +
             node.foreign.capacity() * sizeof(Foreign) + node.foreign_bytes.capacity();
  for (const Peer& p : node.peers) {
    const Connection& c = p.conn;
    b += c.sent.capacity() * sizeof(Connection::Sent) + c.out.capacity() * sizeof(Connection::Out) +
         c.out_bytes.capacity() + c.loose.capacity() + c.loose_ends.capacity() * 4 +
         c.loose_chan.capacity() + c.loose_done.capacity() +
         c.held.capacity() * sizeof(Connection::Held) + c.held_bytes.capacity() +
         c.assembly.capacity();
  }
  return b;
}

TrafficReport run_traffic(const TrafficPlan& plan) {
  TrafficReport rep;
  const int n = plan.clients < 0 ? 0 : plan.clients;
  const bool keep = plan.keep_latencies;
  const uint32_t protocol = traffic_protocol();
  ChannelConfig channels;
  channels.count = 3;
  channels.delivery[kTrafficInputs] = Delivery::sequenced;
  channels.delivery[kTrafficSnapshots] = Delivery::unreliable;
  channels.delivery[kTrafficEvents] = Delivery::reliable;
  const bool bulk = plan.mix.bulk_hz > 0 && plan.mix.bulk_bytes > 0;
  const size_t bulk_bytes = !bulk                                  ? 0
                            : plan.mix.bulk_bytes < kHeader        ? kHeader
                            : plan.mix.bulk_bytes > kMaxBigMessage ? kMaxBigMessage
                                                                   : plan.mix.bulk_bytes;
  if (bulk) {
    // The bulk rides behind the game's own traffic: inputs and events first, then snapshots.
    channels.count = 4;
    channels.delivery[kTrafficBulk] = Delivery::reliable;
    channels.largest[kTrafficBulk] = static_cast<uint32_t>(bulk_bytes);
    channels.priority[kTrafficInputs] = 2;
    channels.priority[kTrafficEvents] = 2;
    channels.priority[kTrafficSnapshots] = 1;
  }
  channels.budget_kbps = plan.budget_kbps;

  SimNetwork net = make_sim_network(plan.seed, plan.conditions);
  auto host = std::make_unique<Side>();
  std::vector<std::unique_ptr<Side>> clients;
  if (plan.capture) {
    rep.capture = make_capture();
    host->transport.capture = &rep.capture;
  }
  open_sim_transport(host->transport, net, sim_address(0, 1));
  NodeConfig hc;
  hc.protocol = protocol;
  hc.max_peers = plan.max_peers;
  hc.accept = true;
  hc.channels = channels;
  hc.seed = plan.seed * 2654435761ull + 1;
  node_init(host->node, hc, host->transport);
  for (int i = 0; i < n; i++) {
    clients.push_back(std::make_unique<Side>());
    Side& c = *clients.back();
    open_sim_transport(c.transport, net, sim_address(static_cast<uint32_t>(i + 1), 1));
    NodeConfig cc = hc;
    cc.max_peers = 1;
    cc.accept = false;
    cc.seed = hc.seed + static_cast<uint64_t>(i + 1) * 0x9e3779b97f4a7c15ull;
    node_init(c.node, cc, c.transport);
    c.tries_left = plan.reconnect_tries;
  }
  int restarts = 0;
  bool host_alive = true;
  size_t next_chaos = 0;
  const auto clients_of = [&](const ChaosStep& st, int& lo, int& hi) {
    lo = st.first < 0 ? 0 : st.first;
    hi = st.count < 0 ? n : lo + st.count;
    if (hi > n) hi = n;
  };
  std::vector<int> client_of_peer(static_cast<size_t>(host->node.config.max_peers), -1);
  std::vector<Stream> host_up_inputs(static_cast<size_t>(n)),
      host_up_events(static_cast<size_t>(n));
  std::vector<Stream> host_down_snaps(static_cast<size_t>(n)),
      host_down_events(static_cast<size_t>(n)), host_down_bulk(static_cast<size_t>(n));
  std::vector<int64_t> host_known_us(static_cast<size_t>(n), -1);

  const double tick_us = 1e6 / plan.tick_hz;
  const int64_t send_end_us = static_cast<int64_t>(std::floor(plan.seconds * 1e6));
  const int64_t end_us = send_end_us + static_cast<int64_t>(std::floor(plan.drain_seconds * 1e6));
  const int64_t sample_us = static_cast<int64_t>(std::floor(plan.sample_s * 1e6));
  int64_t next_sample = 0;
  bool summed = false;
  Fnv fnv;
  uint8_t msg[kMaxMessage];
  std::vector<uint8_t> big(bulk_bytes);

  const auto host_means = [&](double& rtt, double& var, double& jit, double& loss) {
    rtt = var = jit = loss = 0;
    int k = 0;
    for (const Peer& p : host->node.peers) {
      if (p.state != PeerState::connected || p.conn.stats.rtt_samples == 0) continue;
      rtt += p.conn.stats.rtt_ms;
      var += p.conn.stats.rtt_var_ms;
      jit += p.conn.stats.jitter_ms;
      loss += p.conn.stats.loss;
      k++;
    }
    if (k) {
      rtt /= k;
      var /= k;
      jit /= k;
      loss /= k;
    }
  };

  for (int64_t tick = 0;; tick++) {
    const auto now = static_cast<int64_t>(std::floor(static_cast<double>(tick) * tick_us));
    if (now > end_us) break;
    const bool sending = now < send_end_us;

    // CHAOS: the script's steps due by now, in order.
    while (next_chaos < plan.chaos.size() &&
           static_cast<int64_t>(std::floor(plan.chaos[next_chaos].at_s * 1e6)) <= now) {
      const ChaosStep& st = plan.chaos[next_chaos++];
      int lo, hi;
      clients_of(st, lo, hi);
      fnv.value(uint8_t{0xc4});
      fnv.value(static_cast<uint8_t>(st.kind));
      switch (st.kind) {
        case ChaosKind::link:
        case ChaosKind::restore:
          for (int i = lo; i < hi; i++) {
            sim_set_path(net, 0, static_cast<uint32_t>(i + 1),
                         st.kind == ChaosKind::link ? st.link : plan.conditions);
          }
          break;
        case ChaosKind::host_down:
          if (host_alive) close_transport(host->transport);
          host_alive = false;
          break;
        case ChaosKind::host_up:
          if (host_alive) break;
          host = std::make_unique<Side>();
          if (plan.capture) host->transport.capture = &rep.capture;
          open_sim_transport(host->transport, net, sim_address(0, 1));
          hc.seed += 0x2545f4914f6cdd1dull * static_cast<uint64_t>(++restarts);
          node_init(host->node, hc, host->transport);
          for (int& c : client_of_peer) c = -1;
          for (int64_t& k : host_known_us) k = -1;
          host_alive = true;
          break;
        case ChaosKind::rebind:
          for (int i = lo; i < hi; i++) {
            Side& c = *clients[static_cast<size_t>(i)];
            c.port++;
            open_sim_transport(c.transport, net, sim_address(static_cast<uint32_t>(i + 1), c.port));
          }
          break;
        case ChaosKind::skew:
          for (int i = lo; i < hi; i++) clients[static_cast<size_t>(i)]->rate = st.rate;
          break;
      }
    }

    sim_advance(net, now);
    if (plan.hook) plan.hook(plan.hook_ctx, net, now, tick);
    if (tick < n) node_connect(clients[static_cast<size_t>(tick)]->node, sim_address(0, 1), now);
    for (int i = 0; i < n; i++) {
      Side& c = *clients[static_cast<size_t>(i)];
      if (!c.gone || c.retry_at_us < 0 || now < c.retry_at_us) continue;
      c.retry_at_us = -1;
      c.tries_left--;
      c.gone = false;
      c.connected_us = -1;
      node_connect(c.node, sim_address(0, 1), side_now(c, now));
    }

    // The host's events.
    if (host_alive) node_update(host->node, now);
    for (const Event& e : host->node.events) {
      if (!host_alive) break;
      fnv.value(uint8_t{0});
      fnv.value(static_cast<uint8_t>(e.type));
      fnv.value(e.peer);
      if (e.type == EventType::connected || e.type == EventType::disconnected) {
        const int was = client_of_peer[static_cast<size_t>(e.peer)];
        client_of_peer[static_cast<size_t>(e.peer)] = -1;
        if (e.type == EventType::disconnected && was >= 0 &&
            std::find(client_of_peer.begin(), client_of_peer.end(), was) == client_of_peer.end())
          host_known_us[static_cast<size_t>(was)] = -1;  // learnt again from its next message
        continue;
      }
      if (e.type != EventType::message || e.size < kHeader) continue;
      const uint8_t* d = event_data(host->node, e);
      fnv.bytes(d, e.size);
      Reader r = make_reader(d, e.size);
      const uint8_t kind = read_u8(r);
      const int client = read_u16(r);
      const uint32_t count = read_u32(r);
      const auto sent = static_cast<int64_t>(read_u64(r));
      if (client >= n) continue;
      client_of_peer[static_cast<size_t>(e.peer)] = client;
      if (host_known_us[static_cast<size_t>(client)] < 0) {
        host_known_us[static_cast<size_t>(client)] = now;
        for (Stream* st : {&host_down_snaps[static_cast<size_t>(client)],
                           &host_down_events[static_cast<size_t>(client)],
                           &host_down_bulk[static_cast<size_t>(client)]})
          st->base = st->next_send;
      }
      if (kind == kInput) {
        tally(rep.inputs, host_up_inputs[static_cast<size_t>(client)], count, sent, now, false,
              keep);
      } else if (kind == kEventUp) {
        tally(rep.events_up, host_up_events[static_cast<size_t>(client)], count, sent, now, true,
              keep);
      }
    }

    // Each client's events.
    for (int i = 0; i < n; i++) {
      Side& c = *clients[static_cast<size_t>(i)];
      const int64_t cnow = side_now(c, now);
      node_update(c.node, cnow);
      for (const Event& e : c.node.events) {
        fnv.value(static_cast<uint8_t>(i + 1));
        fnv.value(static_cast<uint8_t>(e.type));
        fnv.value(static_cast<uint8_t>(e.reason));
        switch (e.type) {
          case EventType::connected:
            c.connected_us = now;
            c.inputs.base = c.inputs.next_send;
            c.events_up.base = c.events_up.next_send;
            if (c.ever_connected) {
              rep.reconnects++;
              const double s = static_cast<double>(now - c.lost_at_us) / 1e6;
              if (s > rep.recover_s) rep.recover_s = s;
            } else {
              rep.connected++;
            }
            c.ever_connected = true;
            c.lost_at_us = -1;
            break;
          case EventType::connect_failed:
            c.gone = true;
            rep.denied += e.reason == Reason::denied_full;
            if (e.reason != Reason::denied_full && c.tries_left > 0) {
              c.retry_at_us = now + static_cast<int64_t>(std::floor(plan.reconnect_s * 1e6));
            } else {
              c.gave_up = true;
            }
            break;
          case EventType::disconnected:
            c.gone = true;
            rep.dropped++;
            c.lost_at_us = now;
            if (c.tries_left > 0) {
              c.retry_at_us = now + static_cast<int64_t>(std::floor(plan.reconnect_s * 1e6));
            } else {
              c.gave_up = true;
            }
            break;
          case EventType::message: {
            if (e.size < kHeader) break;
            const uint8_t* d = event_data(c.node, e);
            fnv.bytes(d, e.size);
            Reader r = make_reader(d, e.size);
            const uint8_t kind = read_u8(r);
            read_u16(r);
            const uint32_t count = read_u32(r);
            const auto sent = static_cast<int64_t>(read_u64(r));
            if (kind == kSnapshot) tally(rep.snapshots, c.snapshots, count, sent, now, false, keep);
            if (kind == kEventDown)
              tally(rep.events_down, c.events_down, count, sent, now, true, keep);
            if (kind == kBulk) {
              if (e.size == bulk_bytes && filler_whole(d, e.size, count)) {
                tally(rep.bulk, c.bulk, count, sent, now, true, keep);
              } else {
                rep.bulk_broken++;
              }
            }
            break;
          }
        }
      }
      // What this client sends now.
      if (!sending || c.gone || c.connected_us < 0) continue;
      const int64_t elapsed = now - c.connected_us;
      for (uint32_t k = c.inputs.next_send; k < c.inputs.base + due(elapsed, plan.mix.input_hz);
           k++) {
        const size_t size = plan.mix.input_bytes < kHeader ? kHeader : plan.mix.input_bytes;
        make_message(msg, size, kInput, i, k, now);
        node_send(c.node, 0, kTrafficInputs, msg, size) ? rep.inputs.sent++ : rep.inputs.refused++;
        c.inputs.next_send = k + 1;
      }
      for (uint32_t k = c.events_up.next_send;
           k < c.events_up.base + due(elapsed, plan.mix.event_hz); k++) {
        const size_t size = plan.mix.event_bytes < kHeader ? kHeader : plan.mix.event_bytes;
        make_message(msg, size, kEventUp, i, c.events_up.next_send, now);
        if (!node_send(c.node, 0, kTrafficEvents, msg, size)) {
          rep.events_up.refused++;
          break;  // the window is full: the same count is tried again next tick
        }
        rep.events_up.sent++;
        c.events_up.next_send++;
      }
    }

    // What the host sends each client it knows.
    if (sending && host_alive) {
      for (int peer = 0; peer < static_cast<int>(client_of_peer.size()); peer++) {
        const int client = client_of_peer[static_cast<size_t>(peer)];
        if (client < 0 || !peer_connected(host->node, peer)) continue;
        const int64_t elapsed = now - host_known_us[static_cast<size_t>(client)];
        Stream& snaps = host_down_snaps[static_cast<size_t>(client)];
        Stream& events = host_down_events[static_cast<size_t>(client)];
        for (uint32_t k = snaps.next_send; k < snaps.base + due(elapsed, plan.mix.snapshot_hz);
             k++) {
          const size_t size = plan.mix.snapshot_bytes < kHeader ? kHeader : plan.mix.snapshot_bytes;
          make_message(msg, size, kSnapshot, client, k, now);
          node_send(host->node, peer, kTrafficSnapshots, msg, size) ? rep.snapshots.sent++
                                                                    : rep.snapshots.refused++;
          snaps.next_send = k + 1;
        }
        for (uint32_t k = events.next_send; k < events.base + due(elapsed, plan.mix.event_hz);
             k++) {
          const size_t size = plan.mix.event_bytes < kHeader ? kHeader : plan.mix.event_bytes;
          make_message(msg, size, kEventDown, client, events.next_send, now);
          if (!node_send(host->node, peer, kTrafficEvents, msg, size)) {
            rep.events_down.refused++;
            break;
          }
          rep.events_down.sent++;
          events.next_send++;
        }
        Stream& bulks = host_down_bulk[static_cast<size_t>(client)];
        for (uint32_t k = bulks.next_send; bulk && k < bulks.base + due(elapsed, plan.mix.bulk_hz);
             k++) {
          make_message(big.data(), bulk_bytes, kBulk, client, bulks.next_send, now);
          if (!node_send(host->node, peer, kTrafficBulk, big.data(), bulk_bytes)) {
            rep.bulk.refused++;
            break;  // no room for all its pieces: the same one is tried again next tick
          }
          rep.bulk.sent++;
          bulks.next_send++;
        }
      }
    }

    if (host_alive) node_flush(host->node, now);
    for (auto& c : clients) {
      node_flush(c->node, side_now(*c, now));
      c->offset_us += (c->rate - 1) * tick_us;
    }

    if (now >= next_sample) {
      double rtt = 0, var = 0, jit = 0, loss = 0;
      if (host_alive) host_means(rtt, var, jit, loss);
      rep.t_s.push_back(static_cast<double>(now) / 1e6);
      rep.series_rtt_ms.push_back(rtt);
      rep.series_loss.push_back(loss);
      int up = 0;
      size_t held = sim_held_bytes(net) + node_held_bytes(host->node);
      for (const auto& c : clients) {
        up += peer_connected(c->node, 0);
        held += node_held_bytes(c->node);
      }
      rep.series_connected.push_back(up);
      rep.series_held_kb.push_back(static_cast<double>(held) / 1024.0);
      next_sample += sample_us;
    }
    if (!sending && !summed) {
      summed = true;
      host_means(rep.rtt_ms, rep.rtt_var_ms, rep.jitter_ms, rep.loss);
      uint64_t sent = 0, recv = 0, lost = 0, acked = 0;
      for (const Peer& p : host->node.peers) {
        sent += p.conn.stats.bytes_sent;
        recv += p.conn.stats.bytes_received;
        lost += p.conn.stats.packets_lost;
        acked += p.conn.stats.packets_acked;
      }
      rep.loss_total =
          lost + acked ? static_cast<double>(lost) / static_cast<double>(lost + acked) : 0;
      rep.host_send_kbps = static_cast<double>(sent * 8) / (plan.seconds * 1000.0);
      rep.host_recv_kbps = static_cast<double>(recv * 8) / (plan.seconds * 1000.0);
    }
  }

  for (const Peer& p : host->node.peers) {
    rep.resends += p.conn.stats.resends;
    rep.dropped_unsent += p.conn.stats.dropped_unsent;
    rep.duplicates += p.conn.stats.duplicates;
    rep.stale += p.conn.stats.stale;
  }
  for (const auto& c : clients) {
    const LinkStats& s = c->node.peers[0].conn.stats;
    rep.resends += s.resends;
    rep.dropped_unsent += s.dropped_unsent;
    rep.duplicates += s.duplicates;
    rep.stale += s.stale;
  }
  for (int i = 0; i < n; i++) {
    const Side& c = *clients[static_cast<size_t>(i)];
    if (peer_connected(c.node, 0)) {
      rep.ended_connected++;
    } else if (c.gone && (c.gave_up || c.retry_at_us < 0)) {
      rep.ended_timed_out++;
    } else {
      rep.ended_stuck++;
    }
  }
  for (const Peer& p : host->node.peers) {
    if (!host_alive || p.state != PeerState::connected) continue;
    bool behind = false;
    for (const auto& c : clients) {
      behind |= c->transport.local == p.address && peer_connected(c->node, 0);
    }
    rep.host_zombies += !behind;
  }
  bool any_peer = false;
  for (const Peer& p : host->node.peers) {
    if (p.state != PeerState::connected) continue;
    if (!any_peer || p.conn.stats.packets_sent < rep.host_packets_min)
      rep.host_packets_min = p.conn.stats.packets_sent;
    any_peer = true;
  }
  rep.host_strangers = host->node.stats.strangers;
  rep.host_bad_keys = host->node.stats.bad_keys;
  rep.host_bad_cookies = host->node.stats.bad_cookies;
  rep.host_foreign = host->node.stats.foreign;
  rep.wire = net.stats;
  fnv.value(rep.wire.sent);
  fnv.value(rep.wire.delivered);
  fnv.value(rep.resends);
  rep.digest = fnv.h;
  host->transport.capture = nullptr;
  return rep;
}

namespace {

struct NamedMix {
  const char* name;
  TrafficMix mix;
};

const NamedMix kMixes[] = {
    {"racing", {60, 24, 20, 320, 4, 48, 0, 0}},
    {"action", {60, 32, 30, 700, 8, 32, 0, 0}},
    {"rpg", {20, 16, 10, 400, 10, 64, 0, 0}},
    {"world", {10, 24, 10, 1000, 20, 96, 0, 0}},
    {"lockstep", {10, 16, 0, 320, 2, 64, 0, 0}},
    // A racing session while every client downloads a 64 KB map every two seconds.
    {"download", {60, 24, 20, 320, 4, 48, 0.5, size_t{64} * 1024}},
};

}  // namespace

const std::vector<std::string>& mix_names() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> n;
    for (const NamedMix& m : kMixes) n.emplace_back(m.name);
    return n;
  }();
  return names;
}

bool traffic_mix(std::string_view name, TrafficMix& out) {
  for (const NamedMix& m : kMixes) {
    if (name == m.name) {
      out = m.mix;
      return true;
    }
  }
  return false;
}

std::string describe_mix(const TrafficMix& m) {
  const auto one = [](const char* what, double hz, size_t bytes) {
    if (hz <= 0) return std::string("no ") + what;
    return std::string(what) + " " + core::num::to_string(hz) + " Hz x " + std::to_string(bytes) +
           " B";
  };
  return one("inputs", m.input_hz, m.input_bytes) + ", " +
         one("snapshots", m.snapshot_hz, m.snapshot_bytes) + ", " +
         one("events", m.event_hz, m.event_bytes) +
         (m.bulk_hz > 0 ? ", " + one("bulk", m.bulk_hz, m.bulk_bytes) : std::string());
}

}  // namespace ge::net
