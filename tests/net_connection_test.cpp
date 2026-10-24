// engine/net/connection.hpp: two connections joined by a scripted wire (no network) to hold the
// stream's rules exactly: acks and the round trip, reliable messages once and in order through
// loss, sequenced messages never going back, unreliable ones dropped when there is no room, and
// the reliable window's back-pressure.
//
// ge:summary Test: a connection's acks, round trip, loss and each channel's delivery rules
// ge:keywords test net connection reliable sequenced unreliable ack rtt loss window
// ge:skill net testing
#include "net/connection.hpp"

#include <string>
#include <vector>

#include "check.hpp"

using namespace ge::net;

namespace {

struct Got {
  std::vector<std::pair<int, std::string>> msgs;
};
void sink_fn(void* ctx, int channel, const uint8_t* data, size_t size) {
  static_cast<Got*>(ctx)->msgs.push_back({channel, std::string(data, data + size)});
}

ChannelConfig three() {
  ChannelConfig c;
  c.count = 3;
  c.delivery[0] = Delivery::unreliable;
  c.delivery[1] = Delivery::sequenced;
  c.delivery[2] = Delivery::reliable;
  return c;
}

// Every packet `from` writes now, each delivered to `to` unless `drop` says otherwise.
int pump(Connection& from, Connection& to, Got& got, int64_t now, bool force,
         bool (*drop)(int index), int& index) {
  uint8_t buf[kMaxPacket];
  int n = 0;
  while (size_t size = connection_write(from, 1, buf, now, force && n == 0)) {
    n++;
    if (drop && drop(index++)) continue;
    Reader r = make_reader(buf, size);
    uint32_t p;
    PacketType t;
    PayloadHeader h;
    read_packet_start(r, p, t);
    read_payload_header(r, h);
    connection_read(to, h, r, now, {sink_fn, &got});
  }
  connection_end_flush(from);
  return n;
}

bool drop_every_third(int i) { return i % 3 == 1; }

}  // namespace

TEST(connection_acks_give_the_round_trip) {
  Connection a, b;
  connection_init(a, three(), 5, 0);
  connection_init(b, three(), 5, 0);
  Got ga, gb;
  int ia = 0, ib = 0;
  // a sends at t, b answers 40 ms later: every round trip is 40 ms.
  for (int64_t t = 0; t < 2000000; t += 40000) {
    pump(a, b, gb, t, true, nullptr, ia);
    pump(b, a, ga, t + 40000, true, nullptr, ib);
    connection_audit(a, t + 40000);
  }
  CHECK(a.stats.rtt_samples > 40);
  CHECK_MSG(a.stats.rtt_ms > 39.9 && a.stats.rtt_ms < 40.1, std::to_string(a.stats.rtt_ms));
  CHECK(a.stats.loss == 0 && a.stats.packets_lost == 0);
  CHECK(a.stats.packets_acked + 1 >= a.stats.packets_sent);
}

TEST(connection_reliable_arrives_once_in_order_through_loss) {
  Connection a, b;
  connection_init(a, three(), 5, 0);
  connection_init(b, three(), 5, 0);
  Got ga, gb;
  int ia = 0, ib = 0;
  int queued = 0;
  for (int64_t t = 0; t < 6000000; t += 16000) {
    if (t < 3000000) {
      for (int k = 0; k < 3; k++) {
        const std::string m = "event " + std::to_string(queued);
        if (connection_queue(a, 2, reinterpret_cast<const uint8_t*>(m.data()), m.size())) queued++;
      }
    }
    pump(a, b, gb, t, false, drop_every_third, ia);
    pump(b, a, ga, t + 8000, true, drop_every_third, ib);
    connection_audit(a, t + 8000);
    connection_audit(b, t + 8000);
  }
  CHECK(queued > 100);
  CHECK_MSG(static_cast<int>(gb.msgs.size()) == queued,
            std::to_string(gb.msgs.size()) + " of " + std::to_string(queued) + " arrived");
  for (size_t i = 0; i < gb.msgs.size(); i++) {
    CHECK(gb.msgs[i].first == 2);
    if (gb.msgs[i].second != "event " + std::to_string(i)) {
      CHECK_MSG(false, "message " + std::to_string(i) + " is " + gb.msgs[i].second);
      break;
    }
  }
  CHECK(a.stats.resends > 0);
  CHECK(a.stats.packets_lost > 0 && a.stats.loss > 0.2 && a.stats.loss < 0.45);
}

TEST(connection_sequenced_never_goes_back) {
  Connection a, b;
  connection_init(a, three(), 5, 0);
  connection_init(b, three(), 5, 0);
  // Two packets written, delivered newest first: the older message is stale.
  uint8_t p1[kMaxPacket], p2[kMaxPacket];
  connection_queue(a, 1, reinterpret_cast<const uint8_t*>("old"), 3);
  const size_t n1 = connection_write(a, 1, p1, 0, false);
  connection_end_flush(a);
  connection_queue(a, 1, reinterpret_cast<const uint8_t*>("new"), 3);
  const size_t n2 = connection_write(a, 1, p2, 1000, false);
  Got g;
  for (const auto& [buf, n] : {std::pair{p2, n2}, std::pair{p1, n1}}) {
    Reader r = make_reader(buf, n);
    uint32_t p;
    PacketType t;
    PayloadHeader h;
    read_packet_start(r, p, t);
    read_payload_header(r, h);
    CHECK(connection_read(b, h, r, 2000, {sink_fn, &g}));
  }
  CHECK(g.msgs.size() == 1 && g.msgs[0].second == "new");
  CHECK(b.stats.stale == 1);
  // The same packet again is a duplicate and delivers nothing.
  Reader r = make_reader(p2, n2);
  uint32_t p;
  PacketType t;
  PayloadHeader h;
  read_packet_start(r, p, t);
  read_payload_header(r, h);
  CHECK(!connection_read(b, h, r, 3000, {sink_fn, &g}));
  CHECK(b.stats.duplicates == 1 && g.msgs.size() == 1);
}

TEST(connection_a_sequence_far_ahead_forgets_the_ack_bits) {
  // A jump of hundreds of packets (a long outage, or a forged sequence the fuzzer sends) must
  // clear the ack bits, never shift them by 64 or more.
  Connection a, b;
  connection_init(a, three(), 5, 0);
  connection_init(b, three(), 5, 0);
  uint8_t buf[kMaxPacket];
  for (int64_t i = 0; i < 300; i++) {
    const size_t n = connection_write(a, 1, buf, i * 1000, true);
    connection_end_flush(a);
    if (i != 0 && i != 299) continue;  // only the first and the 300th arrive
    Reader r = make_reader(buf, n);
    uint32_t p;
    PacketType t;
    PayloadHeader h;
    read_packet_start(r, p, t);
    read_payload_header(r, h);
    Got g;
    CHECK(connection_read(b, h, r, i * 1000, {sink_fn, &g}));
  }
  CHECK(b.recv_newest == 300);
  CHECK(b.recv_bits == 0);
}

TEST(connection_drops_unreliable_with_no_room_and_refuses_past_the_window) {
  Connection a;
  connection_init(a, three(), 5, 0);
  std::vector<uint8_t> big(1000, 7);
  for (int i = 0; i < 12; i++) CHECK(connection_queue(a, 0, big.data(), big.size()));
  uint8_t buf[kMaxPacket];
  int packets = 0;
  while (connection_write(a, 1, buf, 0, false)) packets++;
  connection_end_flush(a);
  CHECK(packets == 12);  // one each: the node caps a flush, the connection does not
  CHECK(a.stats.dropped_unsent == 0);
  CHECK(!connection_queue(a, 0, big.data(), kMaxMessage + 1));
  CHECK(!connection_queue(a, 3, big.data(), 1));
  int queued = 0;
  while (connection_queue(a, 2, big.data(), 4)) queued++;
  CHECK(queued == kReliableInFlight);
}

namespace {

ChannelConfig with_big(uint32_t largest) {
  ChannelConfig c = three();
  c.largest[2] = largest;
  return c;
}

std::string pattern(size_t n, int seed) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; i++)
    s[i] = static_cast<char>((i * 31 + static_cast<size_t>(seed)) & 0xff);
  return s;
}

bool queue_text(Connection& c, int channel, const std::string& m) {
  return connection_queue(c, channel, reinterpret_cast<const uint8_t*>(m.data()), m.size());
}

}  // namespace

TEST(connection_big_message_arrives_whole_through_loss) {
  Connection a, b;
  connection_init(a, with_big(kMaxBigMessage), 5, 0);
  connection_init(b, with_big(kMaxBigMessage), 5, 0);
  const std::string big = pattern(kMaxBigMessage, 3), mid = pattern(5000, 9);
  CHECK(queue_text(a, 2, "before"));
  CHECK(queue_text(a, 2, big));
  CHECK(queue_text(a, 2, mid));
  CHECK(queue_text(a, 2, "after"));
  CHECK(a.stats.messages_sent == 4);
  Got ga, gb;
  int ia = 0, ib = 0;
  for (int64_t t = 0; t < 8000000 && gb.msgs.size() < 4; t += 16000) {
    pump(a, b, gb, t, false, drop_every_third, ia);
    pump(b, a, ga, t + 8000, true, drop_every_third, ib);
    connection_audit(a, t + 8000);
    connection_audit(b, t + 8000);
  }
  CHECK_MSG(gb.msgs.size() == 4, std::to_string(gb.msgs.size()) + " of 4 arrived");
  if (gb.msgs.size() == 4) {
    CHECK(gb.msgs[0].second == "before" && gb.msgs[3].second == "after");
    CHECK_MSG(gb.msgs[1].second == big,
              "the 64 KB message is " + std::to_string(gb.msgs[1].second.size()) + " bytes");
    CHECK(gb.msgs[2].second == mid);
  }
  CHECK(b.stats.messages_received == 4 && b.stats.oversize == 0);
  CHECK(a.stats.resends > 0);
}

TEST(connection_big_message_limits) {
  Connection a;
  connection_init(a, with_big(10000), 5, 0);
  const std::string over = pattern(10001, 1);
  CHECK(!queue_text(a, 2, over));                         // past the channel's largest
  CHECK(!queue_text(a, 1, pattern(kMaxMessage + 1, 1)));  // a sequenced channel takes no pieces
  Connection plain;
  connection_init(plain, three(), 5, 0);
  CHECK(!queue_text(plain, 2, pattern(kMaxMessage + 1, 1)));  // largest unset: one packet
  CHECK(queue_text(plain, 2, pattern(kMaxMessage, 1)));
  // All of a message's pieces or none: with fewer free slots than pieces, nothing is queued.
  int small = 0;
  while (small < kReliableInFlight - 5 && queue_text(a, 2, "x")) small++;
  const uint64_t before = a.stats.messages_sent;
  CHECK(!queue_text(a, 2, pattern(9000, 2)));  // eight pieces, five slots
  CHECK(a.stats.messages_sent == before);
  CHECK(queue_text(a, 2, pattern(5000, 2)));  // five pieces fit
}

TEST(connection_big_message_past_the_receiver_s_largest_is_dropped_whole) {
  Connection a, b;
  connection_init(a, with_big(kMaxBigMessage), 5, 0);
  connection_init(b, with_big(3000), 5, 0);
  CHECK(queue_text(a, 2, pattern(8000, 4)));
  CHECK(queue_text(a, 2, "next"));
  Got ga, gb;
  int ia = 0, ib = 0;
  for (int64_t t = 0; t < 2000000; t += 16000) {
    pump(a, b, gb, t, false, nullptr, ia);
    pump(b, a, ga, t + 8000, true, nullptr, ib);
  }
  CHECK(b.stats.oversize == 1);
  CHECK(gb.msgs.size() == 1 && gb.msgs[0].second == "next");
}

TEST(connection_priority_rides_first) {
  ChannelConfig c;
  c.count = 2;
  c.delivery[0] = Delivery::unreliable;
  c.delivery[1] = Delivery::unreliable;
  c.priority[1] = 5;
  Connection a, b;
  connection_init(a, c, 5, 0);
  connection_init(b, c, 5, 0);
  const std::string low = pattern(900, 1), high = pattern(900, 2);
  CHECK(queue_text(a, 0, low));
  CHECK(queue_text(a, 1, high));
  uint8_t buf[kMaxPacket];
  Got g;
  for (int k = 0; k < 2; k++) {
    const size_t n = connection_write(a, 1, buf, 0, false);
    CHECK(n > 0);
    Reader r = make_reader(buf, n);
    uint32_t p;
    PacketType t;
    PayloadHeader h;
    read_packet_start(r, p, t);
    read_payload_header(r, h);
    connection_read(b, h, r, 0, {sink_fn, &g});
  }
  CHECK(g.msgs.size() == 2 && g.msgs[0].first == 1 && g.msgs[1].first == 0);
}

TEST(connection_budget_caps_what_it_sends) {
  ChannelConfig c = three();
  c.budget_kbps = 100;
  Connection a;
  connection_init(a, c, 5, 0);
  const std::string snap = pattern(1000, 5);
  uint8_t buf[kMaxPacket];
  const int64_t end = 2000000;
  for (int64_t t = 0; t < end; t += 16000) {
    for (int k = 0; k < 4; k++) queue_text(a, 0, snap);  // 2 Mbit/s offered
    for (int k = 0; k < 8 && connection_write(a, 1, buf, t, false); k++) {
    }
    connection_end_flush(a);
  }
  // 100 kbit/s for 2 s is 25000 bytes, and the bucket's 50 ms (two packets) on top.
  CHECK_MSG(a.stats.bytes_sent <= 25000 + 2 * kMaxPacket + kMaxPacket,
            std::to_string(a.stats.bytes_sent) + " bytes in 2 s");
  CHECK(a.stats.bytes_sent > 20000);
  CHECK(a.stats.dropped_unsent > 0 && a.stats.budget_held > 0);
}
