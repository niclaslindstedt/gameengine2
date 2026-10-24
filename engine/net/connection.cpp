// A connection's payloads: writing a packet (priority by priority, the reliable messages due and
// then the loose ones, within the budget), reading one (placing its sequence, its acks, then its
// messages by channel, a big message's pieces joined), and the audit that counts the lost packets
// and rolls the bandwidth window. The model is stated in connection.hpp.
//
// ge:summary A connection's packet writing, reading, acks, round trip, loss and channel delivery
// ge:keywords net connection ack rtt loss resend reliable sequenced deliver audit bandwidth
// ge:skill net
// ge:test tests/net_connection_test.cpp
#include "net/connection.hpp"

#include <cstring>

namespace ge::net {

namespace {

constexpr int64_t kWindowUs = 250000;
constexpr size_t kLooseCapacity = kMaxPacket * 16;

double ms(int64_t us) { return static_cast<double>(us) / 1000.0; }
double absd(double v) { return v < 0 ? -v : v; }

uint16_t next_sequence(uint16_t s) { return s == 0xffff ? 1 : static_cast<uint16_t>(s + 1); }

size_t largest_of(const ChannelConfig& ch, int channel) {
  if (ch.delivery[channel] != Delivery::reliable || ch.largest[channel] <= kMaxMessage) {
    return kMaxMessage;
  }
  return ch.largest[channel] < kMaxBigMessage ? ch.largest[channel] : kMaxBigMessage;
}

// The budget's bucket holds 50 ms of the rate, and two packets at least.
double budget_burst(const ChannelConfig& ch) {
  const double b = ch.budget_kbps / 8000.0 * 50000.0;
  return b > 2.0 * kMaxPacket ? b : 2.0 * kMaxPacket;
}

void finalize(Connection& c, Connection::Sent& s, bool lost) {
  s.final = true;
  if (lost) c.stats.packets_lost++;
  c.stats.loss += ((lost ? 1.0 : 0.0) - c.stats.loss) / 64.0;
}

void rtt_sample(Connection& c, double r) {
  LinkStats& st = c.stats;
  if (st.rtt_samples == 0) {
    st.rtt_ms = r;
    st.rtt_var_ms = r / 2;
  } else {
    st.rtt_var_ms = 0.75 * st.rtt_var_ms + 0.25 * absd(st.rtt_ms - r);
    st.rtt_ms = 0.875 * st.rtt_ms + 0.125 * r;
  }
  if (c.last_rtt_sample_ms >= 0) {
    st.jitter_ms += (absd(r - c.last_rtt_sample_ms) - st.jitter_ms) / 16.0;
  }
  c.last_rtt_sample_ms = r;
  st.rtt_samples++;
}

void ack_one(Connection& c, uint16_t seq, bool newest, int64_t now_us) {
  Connection::Sent& s = c.sent[seq % kSentRing];
  if (!s.used || s.seq != seq || s.acked) return;
  s.acked = true;
  c.stats.packets_acked++;
  if (newest) rtt_sample(c, ms(now_us - s.sent_us));
  for (int k = 0; k < s.reliable; k++) {
    Connection::Out& o = c.out[s.slots[k]];
    if (o.used && o.id == s.ids[k] && o.channel == s.chans[k]) o.used = false;
  }
}

// Whether `pieces` reliable ids may be queued on `channel`: its oldest waiting id stays within the
// span of the last of them.
bool span_open(const Connection& c, int channel, int pieces) {
  const auto last = static_cast<uint16_t>(c.next_id[channel] + pieces - 1);
  for (const Connection::Out& o : c.out) {
    if (o.used && o.channel == channel && seq_diff(last, o.id) >= kReliableSpan) return false;
  }
  return pieces <= kReliableSpan;
}

// Every message of the payload parses and each channel's kind matches its id.
bool payload_valid(const Connection& c, Reader r) {
  MessageView m;
  while (read_message(r, m)) {
    if (m.channel >= c.channels.count) return false;
    if (m.has_id != (c.channels.delivery[m.channel] != Delivery::unreliable)) return false;
    if (m.more && largest_of(c.channels, m.channel) <= kMaxMessage) return false;
  }
  return !r.bad;
}

// Place a received sequence; false for a duplicate or one too old to place.
bool place_sequence(Connection& c, uint16_t seq) {
  if (seq == 0) return false;
  if (!c.any_received) {
    c.any_received = true;
    c.recv_newest = seq;
    c.recv_bits = 0;
    return true;
  }
  const int d = seq_diff(seq, c.recv_newest);
  if (d > 0) {
    // A jump past the 32 bits (a long outage, or a forged sequence) forgets them all; shifting
    // by 64 or more would be undefined.
    c.recv_bits =
        d > 32
            ? 0
            : static_cast<uint32_t>((static_cast<uint64_t>(c.recv_bits) << d) | (1ull << (d - 1)));
    c.recv_newest = seq;
    return true;
  }
  if (d == 0) {
    c.stats.duplicates++;
    return false;
  }
  const int k = -d - 1;
  if (k >= 32) {
    c.stats.stale++;
    return false;
  }
  if (c.recv_bits & (1u << k)) {
    c.stats.duplicates++;
    return false;
  }
  c.recv_bits |= 1u << k;
  return true;
}

void deliver(Connection& c, MessageSink sink, int channel, const uint8_t* data, size_t size) {
  c.stats.messages_received++;
  sink.fn(sink.ctx, channel, data, size);
}

// A reliable message in its turn: delivered, or a big message's piece joined to the ones before.
void deliver_piece(Connection& c, MessageSink sink, int ch, const uint8_t* data, size_t size,
                   bool more) {
  if (!more && c.assembly_size[ch] == 0 && !c.assembly_bad[ch]) {
    deliver(c, sink, ch, data, size);
    return;
  }
  if (!c.assembly_bad[ch] && c.assembly_size[ch] + size <= largest_of(c.channels, ch)) {
    if (size) {
      std::memcpy(c.assembly.data() + c.assembly_at[ch] + c.assembly_size[ch], data, size);
    }
    c.assembly_size[ch] += static_cast<uint32_t>(size);
  } else {
    c.assembly_bad[ch] = true;
  }
  if (more) return;
  if (c.assembly_bad[ch]) {
    c.stats.oversize++;
  } else {
    deliver(c, sink, ch, c.assembly.data() + c.assembly_at[ch], c.assembly_size[ch]);
  }
  c.assembly_size[ch] = 0;
  c.assembly_bad[ch] = false;
}

void receive_reliable(Connection& c, MessageSink sink, const MessageView& m) {
  const int ch = m.channel;
  const int d = seq_diff(m.id, c.expect_id[ch]);
  if (d < 0) return;  // delivered already: a resend whose ack was lost
  if (d > 0) {
    if (d >= kReliableSpan) return;  // past the window: the sender will send it again
    int free_slot = -1;
    for (int i = 0; i < kReliableSpan; i++) {
      const Connection::Held& h = c.held[i];
      if (h.used && h.channel == ch && h.id == m.id) return;
      if (!h.used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return;
    Connection::Held& h = c.held[free_slot];
    h = {true, m.more, static_cast<uint8_t>(ch), m.id, static_cast<uint16_t>(m.size)};
    std::memcpy(c.held_bytes.data() + static_cast<size_t>(free_slot) * kMaxMessage, m.data, m.size);
    return;
  }
  deliver_piece(c, sink, ch, m.data, m.size, m.more);
  c.expect_id[ch]++;
  for (bool more = true; more;) {
    more = false;
    for (int i = 0; i < kReliableSpan; i++) {
      Connection::Held& h = c.held[i];
      if (h.used && h.channel == ch && h.id == c.expect_id[ch]) {
        deliver_piece(c, sink, ch, c.held_bytes.data() + static_cast<size_t>(i) * kMaxMessage,
                      h.size, h.more);
        h.used = false;
        c.expect_id[ch]++;
        more = true;
      }
    }
  }
}

}  // namespace

const char* delivery_name(Delivery d) {
  switch (d) {
    case Delivery::unreliable:
      return "unreliable";
    case Delivery::sequenced:
      return "sequenced";
    case Delivery::reliable:
      return "reliable";
  }
  return "unknown";
}

void connection_init(Connection& c, const ChannelConfig& channels, uint64_t key, int64_t now_us) {
  c = Connection{};
  c.channels = channels;
  c.key = key;
  c.sent.assign(kSentRing, {});
  c.out.assign(kReliableInFlight, {});
  c.out_bytes.assign(static_cast<size_t>(kReliableInFlight) * kMaxMessage, 0);
  c.held.assign(kReliableSpan, {});
  c.held_bytes.assign(static_cast<size_t>(kReliableSpan) * kMaxMessage, 0);
  c.loose.reserve(kLooseCapacity);
  c.loose_ends.reserve(256);
  c.loose_chan.reserve(256);
  c.loose_done.reserve(256);
  size_t joined = 0;
  for (int ch = 0; ch < channels.count; ch++) {
    const size_t most = largest_of(channels, ch);
    c.assembly_at[ch] = static_cast<uint32_t>(joined);
    if (most > kMaxMessage) joined += most;
    bool seen = false;
    for (int k = 0; k < c.level_count; k++) seen = seen || c.levels[k] == channels.priority[ch];
    if (!seen) c.levels[c.level_count++] = channels.priority[ch];
  }
  for (int a = 1; a < c.level_count; a++) {  // highest first
    for (int b = a; b > 0 && c.levels[b] > c.levels[b - 1]; b--) {
      const uint8_t t = c.levels[b];
      c.levels[b] = c.levels[b - 1];
      c.levels[b - 1] = t;
    }
  }
  c.assembly.assign(joined, 0);
  c.budget_bytes = budget_burst(channels);
  c.budget_at_us = now_us;
  c.window_start_us = now_us;
}

int64_t resend_timeout_us(const Connection& c) {
  if (c.stats.rtt_samples == 0) return 200000;
  double t = c.stats.rtt_ms + 4 * c.stats.rtt_var_ms;
  t = t < 20 ? 20 : t > 1000 ? 1000 : t;
  return static_cast<int64_t>(t * 1000.0);
}

bool connection_queue(Connection& c, int channel, const uint8_t* data, size_t size) {
  if (channel < 0 || channel >= c.channels.count || size > largest_of(c.channels, channel)) {
    return false;
  }
  const Delivery d = c.channels.delivery[channel];
  if (d != Delivery::reliable) {
    const bool has_id = d == Delivery::sequenced;
    if (c.loose.size() + message_wire_size(has_id, size) > kLooseCapacity) return false;
    const size_t at = c.loose.size();
    c.loose.resize(at + message_wire_size(has_id, size));
    Writer w = make_writer(c.loose.data() + at, c.loose.size() - at);
    write_message(w, static_cast<uint8_t>(channel), has_id, has_id ? c.next_id[channel]++ : 0, data,
                  size);
    c.loose_ends.push_back(static_cast<uint32_t>(c.loose.size()));
    c.loose_chan.push_back(static_cast<uint8_t>(channel));
    c.loose_done.push_back(0);
    c.stats.messages_sent++;
    return true;
  }
  const int pieces =
      size <= kMaxMessage ? 1 : static_cast<int>((size + kMaxMessage - 1) / kMaxMessage);
  if (!span_open(c, channel, pieces)) return false;
  int free = 0;
  for (const Connection::Out& o : c.out) free += o.used ? 0 : 1;
  if (free < pieces) return false;
  int i = 0;
  for (int k = 0; k < pieces; k++) {
    while (c.out[i].used) i++;
    const size_t at = static_cast<size_t>(k) * kMaxMessage;
    const size_t n = size - at < kMaxMessage ? size - at : kMaxMessage;
    c.out[i] = {true,
                k + 1 < pieces,
                static_cast<uint8_t>(channel),
                c.next_id[channel]++,
                static_cast<uint16_t>(n),
                -1};
    if (n) std::memcpy(c.out_bytes.data() + static_cast<size_t>(i) * kMaxMessage, data + at, n);
  }
  c.stats.messages_sent++;
  return true;
}

size_t connection_write(Connection& c, uint32_t protocol, uint8_t* out, int64_t now_us,
                        bool force) {
  Writer w = make_writer(out, kMaxPacket);
  const uint16_t seq = c.next_seq;
  write_payload_header(w, protocol,
                       {c.key, seq, c.any_received ? c.recv_newest : uint16_t{0},
                        c.any_received ? c.recv_bits : 0u});
  Connection::Sent s;
  s.used = true;
  s.seq = seq;
  s.sent_us = now_us;
  bool any = false;
  bool starved = false;
  if (c.channels.budget_kbps > 0) {
    const double burst = budget_burst(c.channels);
    if (now_us > c.budget_at_us) {
      c.budget_bytes +=
          static_cast<double>(now_us - c.budget_at_us) * c.channels.budget_kbps / 8000.0;
      if (c.budget_bytes > burst) c.budget_bytes = burst;
      c.budget_at_us = now_us;
    }
    starved = c.budget_bytes <= 0;
  }
  const int64_t rto = resend_timeout_us(c);
  for (int level = 0; level < c.level_count && !starved; level++) {
    const uint8_t prio = c.levels[level];
    for (int i = 0; i < kReliableInFlight && s.reliable < kReliablePerPacket; i++) {
      Connection::Out& o = c.out[i];
      if (!o.used || (o.last_sent_us >= 0 && now_us - o.last_sent_us < rto)) continue;
      if (c.channels.priority[o.channel] != prio) continue;
      if (w.size + message_wire_size(true, o.size) > kMaxPacket) continue;
      write_message(w, o.channel, true, o.id,
                    c.out_bytes.data() + static_cast<size_t>(i) * kMaxMessage, o.size, o.more);
      if (o.last_sent_us >= 0) c.stats.resends++;
      o.last_sent_us = now_us;
      s.slots[s.reliable] = static_cast<uint8_t>(i);
      s.chans[s.reliable] = o.channel;
      s.ids[s.reliable] = o.id;
      s.reliable++;
      any = true;
    }
    for (size_t k = c.loose_first; k < c.loose_ends.size(); k++) {
      if (c.loose_done[k] || c.channels.priority[c.loose_chan[k]] != prio) continue;
      const size_t start = k ? c.loose_ends[k - 1] : 0;
      const size_t n = c.loose_ends[k] - start;
      if (w.size + n > kMaxPacket) break;
      write_bytes(w, c.loose.data() + start, n);
      c.loose_done[k] = 1;
      any = true;
    }
    while (c.loose_first < c.loose_done.size() && c.loose_done[c.loose_first]) c.loose_first++;
  }
  if (starved) {
    bool due = c.loose_first < c.loose_ends.size();
    for (int i = 0; i < kReliableInFlight && !due; i++) due = c.out[i].used;
    if (due) c.stats.budget_held++;
  }
  if (!any && !force && !c.ack_owed) return 0;

  Connection::Sent& slot = c.sent[seq % kSentRing];
  if (slot.used && !slot.final) finalize(c, slot, !slot.acked);  // the ring wrapped over it
  slot = s;
  c.next_seq = next_sequence(seq);
  c.ack_owed = false;
  c.stats.packets_sent++;
  c.stats.bytes_sent += w.size;
  c.window_sent += w.size;
  if (c.channels.budget_kbps > 0) c.budget_bytes -= static_cast<double>(w.size);
  return w.size;
}

void connection_end_flush(Connection& c) {
  for (size_t k = c.loose_first; k < c.loose_done.size(); k++) {
    c.stats.dropped_unsent += c.loose_done[k] ? 0 : 1;
  }
  c.loose.clear();
  c.loose_ends.clear();
  c.loose_chan.clear();
  c.loose_done.clear();
  c.loose_first = 0;
}

bool connection_read(Connection& c, const PayloadHeader& h, Reader& r, int64_t now_us,
                     MessageSink sink) {
  if (!payload_valid(c, r)) return false;
  if (!place_sequence(c, h.seq)) return false;
  c.ack_owed = true;
  c.stats.packets_received++;
  c.stats.bytes_received += r.size;
  c.window_recv += r.size;

  if (h.ack != 0) {
    ack_one(c, h.ack, true, now_us);
    for (int i = 0; i < 32; i++) {
      if (h.ack_bits & (1u << i)) {
        ack_one(c, static_cast<uint16_t>(h.ack - 1 - i), false, now_us);
      }
    }
    if (!c.any_acked || seq_newer(h.ack, c.newest_acked)) c.newest_acked = h.ack;
    c.any_acked = true;
  }

  MessageView m;
  while (read_message(r, m)) {
    switch (c.channels.delivery[m.channel]) {
      case Delivery::unreliable:
        deliver(c, sink, m.channel, m.data, m.size);
        break;
      case Delivery::sequenced:
        if (!c.seq_any[m.channel] || seq_newer(m.id, c.seq_newest[m.channel])) {
          c.seq_any[m.channel] = true;
          c.seq_newest[m.channel] = m.id;
          deliver(c, sink, m.channel, m.data, m.size);
        } else {
          c.stats.stale++;
        }
        break;
      case Delivery::reliable:
        receive_reliable(c, sink, m);
        break;
    }
  }
  return true;
}

void connection_audit(Connection& c, int64_t now_us) {
  const int64_t rto = resend_timeout_us(c);
  const int64_t expire = 2 * rto > 250000 ? 2 * rto : 250000;
  while (c.audit_seq != c.next_seq) {
    Connection::Sent& s = c.sent[c.audit_seq % kSentRing];
    if (s.used && s.seq == c.audit_seq && !s.final) {
      if (s.acked) {
        finalize(c, s, false);
      } else if ((c.any_acked && seq_diff(c.newest_acked, s.seq) > 32) ||
                 now_us - s.sent_us > expire) {
        finalize(c, s, true);
      } else {
        break;
      }
    }
    c.audit_seq = next_sequence(c.audit_seq);
  }
  const int64_t span = now_us - c.window_start_us;
  if (span >= kWindowUs) {
    c.stats.send_kbps = static_cast<double>(c.window_sent * 8) / ms(span);
    c.stats.recv_kbps = static_cast<double>(c.window_recv * 8) / ms(span);
    c.window_sent = c.window_recv = 0;
    c.window_start_us = now_us;
  }
}

}  // namespace ge::net
