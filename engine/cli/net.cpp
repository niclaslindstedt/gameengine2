// `ge net`'s body (net.hpp): a capture loaded and decoded or analyzed, a fake-traffic session
// run, each answered as text, findings and one JSON result.
//
// ge:summary ge net's body: inspect, stats (series and charts) and sim, and their flags read
// ge:keywords cli net run_net run_net_inspect run_net_stats run_net_sim link-overrides
// ge:skill agent-interface net
// ge:test tests/cli_net_test.cpp
#include "cli/net.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

#include "cli/graph.hpp"
#include "cli/json.hpp"
#include "core/num_text.hpp"
#include "lab/series.hpp"
#include "net/analyze.hpp"
#include "net/capture.hpp"
#include "net/conditions.hpp"
#include "net/packet.hpp"
#include "net/replicate_trial.hpp"
#include "net/tap.hpp"
#include "net/traffic.hpp"
#include "net/udp_socket.hpp"

namespace ge::cli {

namespace {

std::string fixed(double v, int digits) {
  return std::isfinite(v) ? core::num::to_fixed(v, digits) : std::string("-");
}

std::string num(double v) { return std::isfinite(v) ? core::num::to_string(v) : "null"; }

std::string pct(double share) { return fixed(100.0 * share, 2) + "%"; }

std::string hex(uint64_t v, int digits) {
  static const char* k = "0123456789abcdef";
  std::string s(static_cast<size_t>(digits), '0');
  for (int i = digits - 1; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = k[v & 15];
  return s;
}

// A table: every column as wide as its widest cell, two spaces between.
std::string table(const std::vector<std::vector<std::string>>& rows) {
  std::vector<size_t> width;
  for (const auto& r : rows) {
    if (width.size() < r.size()) width.resize(r.size(), 0);
    for (size_t i = 0; i < r.size(); i++) width[i] = std::max(width[i], r[i].size());
  }
  std::string out;
  for (const auto& r : rows) {
    std::string line = " ";
    for (size_t i = 0; i < r.size(); i++) {
      line += " " + r[i];
      if (i + 1 < r.size()) line += std::string(width[i] - r[i].size() + 1, ' ');
    }
    out += line + "\n";
  }
  return out;
}

bool parse_double(const std::string& text, double& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end && *end == '\0' && std::isfinite(out);
}

Outcome failed(const std::string& what, const std::string& where, const std::string& next,
               int exit = 1) {
  Outcome o;
  o.findings.push_back({Severity::error, what, where, next});
  o.exit = exit;
  return o;
}

const char* kCaptureNext =
    "pass a .pcap the engine wrote: a lab's attachment (previews/<lab>/*.pcap), "
    "`ge net sim --capture=FILE`, or a live node's save_capture (skill: multiplayer)";

}  // namespace

std::string apply_link(const std::string& spec, net::LinkConditions& c) {
  size_t start = 0;
  while (start < spec.size()) {
    size_t comma = spec.find(',', start);
    if (comma == std::string::npos) comma = spec.size();
    const std::string item = spec.substr(start, comma - start);
    start = comma + 1;
    if (item.empty()) continue;
    const size_t eq = item.find('=');
    if (eq == std::string::npos) return "\"" + item + "\" is not key=value";
    const std::string key = item.substr(0, eq);
    std::string value = item.substr(eq + 1);
    const bool percent = !value.empty() && value.back() == '%';
    if (percent) value.pop_back();
    double v = 0;
    if (!parse_double(value, v) || v < 0) return "\"" + item + "\" needs a number of 0 or more";
    const double share = percent ? v / 100.0 : v;
    if (key == "latency") {
      c.latency_ms = v;
    } else if (key == "jitter") {
      c.jitter_ms = v;
    } else if (key == "loss") {
      c.loss = share;
    } else if (key == "burst") {
      c.burst = v < 1 ? 1 : v;
    } else if (key == "duplicate") {
      c.duplicate = share;
    } else if (key == "reorder") {
      c.reorder = share;
    } else if (key == "reorder-ms") {
      c.reorder_ms = v;
    } else if (key == "bandwidth") {
      c.bandwidth_kbps = v;
    } else if (key == "queue") {
      c.queue_ms = v;
    } else if (key == "drop-every" || key == "drop-run") {
      if (v != static_cast<double>(static_cast<int>(v)) || v > 1000)
        return "\"" + item + "\" needs a whole number of packets up to 1000";
      (key == "drop-every" ? c.drop_every : c.drop_run) = static_cast<int>(v);
    } else {
      return "no link key \"" + key +
             "\" (latency, jitter, loss, burst, duplicate, reorder, reorder-ms, bandwidth, queue, "
             "drop-every, drop-run)";
    }
    if ((key == "loss" || key == "duplicate" || key == "reorder") && share >= 1) {
      return "\"" + item + "\" is a share: give it under 1, or as a percent (loss=2%)";
    }
  }
  return "";
}

namespace {

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    if (comma == std::string::npos) comma = text.size();
    if (comma > start) out.push_back(text.substr(start, comma - start));
    start = comma + 1;
  }
  return out;
}

}  // namespace

Outcome run_net_inspect(const NetInspectOptions& opts) {
  std::vector<net::CapturedDatagram> ds;
  std::string error;
  if (!net::load_capture(opts.file, ds, &error)) return failed(error, opts.file, kCaptureNext);
  net::Address peer;
  const bool by_peer = !opts.peer.empty();
  if (by_peer && !net::parse_address(opts.peer, peer)) {
    return failed("--peer=" + opts.peer + " is not an address", "the command line",
                  "give it as the lines print it: 10.0.0.2:4000, [::1]:4000 or sim:3:1", 2);
  }
  int type_filter = 0;
  if (!opts.type.empty()) {
    for (int t = 1; t <= 7; t++) {
      if (opts.type == net::packet_type_name(static_cast<net::PacketType>(t))) type_filter = t;
    }
    if (!type_filter) {
      return failed("no packet type \"" + opts.type + "\"", "the command line",
                    "rerun with --type=request|challenge|response|accept|deny|payload|disconnect",
                    2);
    }
  }

  Outcome o;
  const int64_t t0 = ds.empty() ? 0 : ds.front().at_us;
  long long matched = 0, shown = 0;
  std::vector<std::string> rows;
  for (const net::CapturedDatagram& d : ds) {
    if (by_peer && d.from != peer && d.to != peer) continue;
    if (type_filter) {
      net::Reader r = net::make_reader(d.data.data(), d.data.size());
      uint32_t protocol = 0;
      net::PacketType type{};
      if (!net::read_packet_start(r, protocol, type) || static_cast<int>(type) != type_filter) {
        continue;
      }
    }
    matched++;
    if (matched <= opts.skip || (opts.limit > 0 && shown >= opts.limit)) continue;
    shown++;
    const double ms = static_cast<double>(d.at_us - t0) / 1000.0;
    const std::string line = net::describe_packet(d.data.data(), d.data.size());
    o.text += "  " + fixed(ms, 1) + " ms  " + net::to_string(d.from) + " > " +
              net::to_string(d.to) + "  " + line + "\n";
    rows.push_back(json_obj({{"t_ms", num(ms)},
                             {"from", json_str(net::to_string(d.from))},
                             {"to", json_str(net::to_string(d.to))},
                             {"bytes", json_num(static_cast<long long>(d.data.size()))},
                             {"packet", json_str(line)}}));
  }
  const long long after = matched - opts.skip - shown;
  if (after > 0) {
    o.text += "  ... " + std::to_string(after) +
              " more (--skip=" + std::to_string(opts.skip + shown) +
              " for the next, --limit=0 for all)\n";
  }
  o.result = json_obj({{"file", json_str(opts.file)},
                       {"datagrams", json_num(static_cast<long long>(ds.size()))},
                       {"matched", json_num(matched)},
                       {"shown", json_num(shown)},
                       {"packets", json_arr(rows)}});
  o.summary = std::to_string(ds.size()) + " datagrams, " + std::to_string(matched) + " matched, " +
              std::to_string(shown) + " shown";
  o.next = "ge net stats " + opts.file;
  return o;
}

Outcome run_net_stats(const NetStatsOptions& opts) {
  std::vector<net::CapturedDatagram> ds;
  std::string error;
  if (!net::load_capture(opts.file, ds, &error)) return failed(error, opts.file, kCaptureNext);
  const net::CaptureAnalysis a = net::analyze_capture(ds, opts.bucket_ms / 1000.0);
  Outcome o;
  const double span_s =
      ds.size() > 1 ? static_cast<double>(ds.back().at_us - ds.front().at_us) / 1e6 : 0;
  o.text += "  " + std::to_string(a.datagrams) + " datagrams over " + fixed(span_s, 2) +
            " s, protocol " + hex(a.protocol, 8) + ", " + std::to_string(a.foreign) + " foreign; " +
            (a.local_known ? "taken at " + net::to_string(a.local)
                           : std::string("the capturing node is unknown: a > b as first seen")) +
            "\n";
  if (a.foreign) {
    o.findings.push_back({Severity::warn,
                          std::to_string(a.foreign) + " datagrams are not the capture's protocol " +
                              hex(a.protocol, 8),
                          opts.file,
                          "ge net inspect " + opts.file +
                              " to read them: another build or another game on the port"});
  }

  std::vector<std::vector<std::string>> rows{{"peer", "pkts out", "in", "kbps out", "in", "rtt p50",
                                              "p95", "turn p50", "loss out", "loss in", "missing",
                                              "dup", "reord", "resends out", "in", "handshake"}};
  std::vector<std::string> conns;
  for (const net::ConnectionStats& c : a.connections) {
    const net::DirectionStats& out = a.local_known ? net::sent_by(c, a.local) : c.ab;
    const net::DirectionStats& in = a.local_known ? net::received_by(c, a.local) : c.ba;
    const int64_t first = std::min(out.first_us < 0 ? in.first_us : out.first_us,
                                   in.first_us < 0 ? out.first_us : in.first_us);
    const int64_t last = std::max(out.last_us, in.last_us);
    const double secs = static_cast<double>(last - first) / 1e6;
    const auto kbps = [&](const net::DirectionStats& s) {
      return secs > 0 ? static_cast<double>(s.bytes) * 8.0 / 1000.0 / secs : NAN;
    };
    const double rtt50 = net::percentile_of(out.ack_delay_ms, 50);
    const double rtt95 = net::percentile_of(out.ack_delay_ms, 95);
    const double turn50 = net::percentile_of(in.ack_delay_ms, 50);
    const auto type_n = [&](net::PacketType t) {
      return out.by_type[static_cast<size_t>(t)] + in.by_type[static_cast<size_t>(t)];
    };
    std::string hs = type_n(net::PacketType::accept)    ? "accepted"
                     : type_n(net::PacketType::deny)    ? "denied"
                     : type_n(net::PacketType::request) ? "asked"
                                                        : "mid-session";
    if (type_n(net::PacketType::disconnect)) hs += ", closed";
    const std::string who = a.local_known
                                ? net::to_string(in.from)
                                : net::to_string(out.from) + " > " + net::to_string(out.to);
    rows.push_back({who, std::to_string(out.packets), std::to_string(in.packets),
                    fixed(kbps(out), 1), fixed(kbps(in), 1), fixed(rtt50, 1), fixed(rtt95, 1),
                    fixed(turn50, 1), pct(out.loss), pct(in.loss), std::to_string(in.missing),
                    std::to_string(in.duplicated), std::to_string(in.reordered),
                    std::to_string(out.resends), std::to_string(in.resends), hs});
    const auto side = [&](const net::DirectionStats& s, double ack50, double ack95) {
      return json_obj({{"packets", json_num(static_cast<long long>(s.packets))},
                       {"bytes", json_num(static_cast<long long>(s.bytes))},
                       {"kbps", num(kbps(s))},
                       {"messages", json_num(static_cast<long long>(s.messages))},
                       {"keepalives", json_num(static_cast<long long>(s.empty))},
                       {"ack_delay_p50_ms", num(ack50)},
                       {"ack_delay_p95_ms", num(ack95)},
                       {"loss", num(s.loss)},
                       {"settled", json_num(static_cast<long long>(s.settled))},
                       {"missing", json_num(static_cast<long long>(s.missing))},
                       {"duplicated", json_num(static_cast<long long>(s.duplicated))},
                       {"reordered", json_num(static_cast<long long>(s.reordered))},
                       {"resends", json_num(static_cast<long long>(s.resends))}});
    };
    conns.push_back(json_obj({{"peer", json_str(net::to_string(in.from))},
                              {"handshake", json_str(hs)},
                              {"out", side(out, rtt50, rtt95)},
                              {"in", side(in, turn50, net::percentile_of(in.ack_delay_ms, 95))}}));
    if (out.loss > 0.05 || in.loss > 0.05) {
      o.findings.push_back({Severity::warn,
                            who + " loses " + pct(out.loss) + " of what it is sent and " +
                                pct(in.loss) + " of what it sends",
                            opts.file,
                            "ge net inspect " + opts.file + " --peer=" + net::to_string(in.from) +
                                " to see the gaps; ge net sim --link=loss=" +
                                fixed(100 * out.loss, 1) + "% reproduces such a link"});
    }
  }
  o.text += table(rows);
  o.text += "  out: what " +
            (a.local_known ? net::to_string(a.local) : std::string("the first side")) +
            " sent; rtt: its sends' wait for an ack, its own tick included; turn: how long it "
            "took to ack what it got; loss: settled by the acks, a send 32 behind the newest ack "
            "never acked; missing: sequences of the peer's never seen here\n";

  std::string series_path;
  if (a.local_known && !a.t_s.empty()) {
    const fs::path file(opts.file);
    const fs::path dir = opts.out.empty() ? file.parent_path() / (file.stem().string() + "-net")
                                          : fs::path(opts.out);
    lab::Series s = lab::make_series({"t_s", "out_kbps", "in_kbps", "rtt_ms", "loss_pct"});
    s.set("title", file.stem().string() + " at " + net::to_string(a.local));
    s.set("x", "t_s");
    s.set("unit.out_kbps", "kbit/s");
    s.set("unit.in_kbps", "kbit/s");
    s.set("unit.rtt_ms", "ms");
    s.set("unit.loss_pct", "%");
    for (size_t i = 0; i < a.t_s.size(); i++) {
      lab::add_row(s, {a.t_s[i], a.out_kbps[i], a.in_kbps[i], a.rtt_ms[i], a.loss_pct[i]});
    }
    const fs::path tsv = dir / (file.stem().string() + ".net.tsv");
    std::string werr;
    if (!write_text(tsv, lab::write_series(s), werr)) {
      o.findings.push_back({Severity::error, "could not write " + tsv.string() + ": " + werr,
                            dir.string(), "pass a writable directory with --out=DIR"});
    } else {
      series_path = tsv.string();
      o.written.push_back(series_path);
      if (opts.charts) {
        GraphOptions g;
        g.files = {series_path};
        g.kind = "line";
        g.out = dir.string();
        const Outcome charts = run_graph(g);
        o.written.insert(o.written.end(), charts.written.begin(), charts.written.end());
        for (const Finding& f : charts.findings) {
          if (f.severity == Severity::error) o.findings.push_back(f);
        }
      }
    }
  }
  o.result = json_obj({{"file", json_str(opts.file)},
                       {"datagrams", json_num(static_cast<long long>(a.datagrams))},
                       {"foreign", json_num(static_cast<long long>(a.foreign))},
                       {"protocol", json_str(hex(a.protocol, 8))},
                       {"local", a.local_known ? json_str(net::to_string(a.local)) : "null"},
                       {"connections", json_arr(conns)},
                       {"series", series_path.empty() ? "null" : json_str(series_path)}});
  o.summary = std::to_string(a.connections.size()) + " connections in " +
              std::to_string(a.datagrams) + " datagrams";
  o.next = series_path.empty() ? "ge net inspect " + opts.file
                               : "ge graph " + series_path + " --columns=rtt_ms,loss_pct";
  return o;
}

Outcome run_net_sim(const NetSimOptions& opts) {
  net::TrafficMix mix;
  if (!net::traffic_mix(opts.mix, mix)) {
    std::string all;
    for (const std::string& m : net::mix_names()) all += (all.empty() ? "" : "|") + m;
    return failed("no traffic mix \"" + opts.mix + "\"", "the command line",
                  "rerun with --mix=" + all, 2);
  }
  std::vector<std::string> presets = opts.presets;
  if (presets.empty()) presets = net::preset_names();
  if (opts.clients < 0 || opts.clients > 64 || opts.max_peers < 1 || opts.max_peers > 32 ||
      !(opts.seconds > 0) || !(opts.tick_hz > 0) || opts.drain < 0) {
    return failed("the session is out of range", "the command line",
                  "use --clients=0..64, --max-peers=1..32, --seconds and --tick-hz above 0", 2);
  }

  Outcome o;
  std::vector<std::vector<std::string>> rows{
      {"preset", "conditions", "joined", "rtt ms", "jitter", "loss", "events", "order", "event p50",
       "p95", "snaps", "inputs", "host kbps out", "in", "resends", "digest"}};
  std::vector<std::string> results;
  std::string bulk_lines;
  for (const std::string& name : presets) {
    net::TrafficPlan plan;
    if (name != "none" && !net::conditions_preset(name, plan.conditions)) {
      std::string all = "none";
      for (const std::string& p : net::preset_names()) all += "," + p;
      return failed("no preset \"" + name + "\"", "the command line", "rerun with --presets=" + all,
                    2);
    }
    if (const std::string bad = apply_link(opts.link, plan.conditions); !bad.empty()) {
      return failed("--link: " + bad, "the command line",
                    "rerun with --link=latency=40,jitter=10,loss=2%", 2);
    }
    plan.clients = opts.clients;
    plan.max_peers = opts.max_peers;
    plan.seconds = opts.seconds;
    plan.drain_seconds = opts.drain;
    plan.tick_hz = opts.tick_hz;
    plan.seed = opts.seed;
    plan.mix = mix;
    plan.conditions.stream = plan.conditions.stream || opts.stream;
    plan.budget_kbps = opts.budget_kbps;
    plan.capture = !opts.capture.empty();
    const net::TrafficReport r = net::run_traffic(plan);

    const uint64_t ev_sent = r.events_up.sent + r.events_down.sent;
    const uint64_t ev_got = r.events_up.delivered + r.events_down.delivered;
    const bool in_order = r.events_up.out_of_order + r.events_down.out_of_order +
                              r.events_up.duplicated + r.events_down.duplicated ==
                          0;
    std::vector<double> lat = r.events_up.latency_ms;
    lat.insert(lat.end(), r.events_down.latency_ms.begin(), r.events_down.latency_ms.end());
    const auto share = [](const net::StreamTally& t) {
      return t.sent ? static_cast<double>(t.delivered) / static_cast<double>(t.sent) : NAN;
    };
    const double p50 = net::percentile_of(lat, 50), p95 = net::percentile_of(lat, 95);
    rows.push_back({name, net::describe_conditions(plan.conditions), std::to_string(r.connected),
                    fixed(r.rtt_ms, 1), fixed(r.jitter_ms, 1), pct(r.loss_total),
                    std::to_string(ev_got) + "/" + std::to_string(ev_sent),
                    in_order ? "ok" : "BROKEN", fixed(p50, 1), fixed(p95, 1),
                    pct(share(r.snapshots)), pct(share(r.inputs)), fixed(r.host_send_kbps, 0),
                    fixed(r.host_recv_kbps, 0), std::to_string(r.resends), hex(r.digest, 16)});
    if (r.bulk_broken || r.bulk.out_of_order || r.bulk.duplicated) {
      o.findings.push_back(
          {Severity::error,
           name + ": " + std::to_string(r.bulk_broken) +
               " big messages arrived broken or out of "
               "order",
           "engine/net/connection.cpp",
           "the pieces of a big message were not joined whole: reproduce with these flags and "
           "run tests/net_connection_test.cpp (skill: net)"});
    }
    if (mix.bulk_hz > 0) {
      bulk_lines += "  " + name + ": big messages " + std::to_string(r.bulk.delivered) + "/" +
                    std::to_string(r.bulk.sent) + " whole, p50 " +
                    fixed(net::percentile_of(r.bulk.latency_ms, 50), 1) + " ms, p95 " +
                    fixed(net::percentile_of(r.bulk.latency_ms, 95), 1) + " ms\n";
    }
    if (ev_got != ev_sent || !in_order) {
      o.findings.push_back(
          {Severity::error,
           name + ": the reliable events arrived " + std::to_string(ev_got) + " of " +
               std::to_string(ev_sent) + (in_order ? ", in order" : ", out of order or twice"),
           "engine/net/connection.cpp",
           "a reliable channel lost or reordered a message: reproduce with these flags and read "
           "the capture (--capture=FILE, then ge net inspect) (skill: net)"});
    }
    std::string capture_path;
    if (plan.capture) {
      fs::path path(opts.capture);
      if (presets.size() > 1) {
        path.replace_filename(path.stem().string() + "." + name + path.extension().string());
      }
      std::string err;
      std::error_code ec;
      if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
      if (!net::save_capture(r.capture, path.string(), &err)) {
        o.findings.push_back(
            {Severity::error, err, path.string(), "pass a writable path with --capture=FILE"});
      } else {
        capture_path = path.string();
        o.written.push_back(capture_path);
      }
    }
    results.push_back(
        json_obj({{"preset", json_str(name)},
                  {"conditions", json_str(net::describe_conditions(plan.conditions))},
                  {"joined", json_num(r.connected)},
                  {"denied", json_num(r.denied)},
                  {"dropped", json_num(r.dropped)},
                  {"rtt_ms", num(r.rtt_ms)},
                  {"jitter_ms", num(r.jitter_ms)},
                  {"loss", num(r.loss_total)},
                  {"events_sent", json_num(static_cast<long long>(ev_sent))},
                  {"events_delivered", json_num(static_cast<long long>(ev_got))},
                  {"in_order", json_bool(in_order)},
                  {"event_p50_ms", num(p50)},
                  {"event_p95_ms", num(p95)},
                  {"snapshots", num(share(r.snapshots))},
                  {"inputs", num(share(r.inputs))},
                  {"host_send_kbps", num(r.host_send_kbps)},
                  {"host_recv_kbps", num(r.host_recv_kbps)},
                  {"resends", json_num(static_cast<long long>(r.resends))},
                  {"bulk_sent", json_num(static_cast<long long>(r.bulk.sent))},
                  {"bulk_delivered", json_num(static_cast<long long>(r.bulk.delivered))},
                  {"digest", json_str(hex(r.digest, 16))},
                  {"capture", capture_path.empty() ? "null" : json_str(capture_path)}}));
  }
  o.text = "  " + std::to_string(opts.clients) + " clients, " + net::describe_mix(mix) + ", " +
           core::num::to_string(opts.seconds) + " s sending and " +
           core::num::to_string(opts.drain) + " s draining at " +
           core::num::to_string(opts.tick_hz) + " Hz, seed " + std::to_string(opts.seed) + "\n" +
           table(rows) + bulk_lines +
           "  rtt: the host's smoothed round trip at the end of the sending, its tick included; "
           "loss: the host's sends never acked; events: reliable, both ways, every one owed in "
           "order; snaps and inputs may be lost\n";
  o.result = json_obj({{"mix", json_str(opts.mix)},
                       {"clients", json_num(opts.clients)},
                       {"runs", json_arr(results)}});
  o.summary = std::to_string(presets.size()) + " sessions run";
  o.next = o.written.empty() ? "ge net sim --capture=runs/net.pcap, then ge net stats on it"
                             : "ge net stats " + o.written.front();
  return o;
}

Outcome run_net_watch(const NetWatchOptions& opts) {
  if (opts.port < 1 || opts.port > 65535 || opts.seconds < 0 || !(opts.every > 0) ||
      (opts.seconds == 0 && opts.frames <= 0)) {
    return failed("the watch is out of range", "the command line",
                  "use --port=1..65535, --every above 0, and --seconds or --frames to end it", 2);
  }
  net::UdpSocket sock;
  std::string error;
  if (!net::udp_open(sock, net::loopback_v4(static_cast<uint16_t>(opts.port)), &error)) {
    return failed("cannot listen on 127.0.0.1:" + std::to_string(opts.port) + ": " + error,
                  "the command line",
                  "another watch holds the port: stop it, or pass --port and open the game's tap "
                  "on the same one",
                  1);
  }
  std::vector<net::CapturedDatagram> all;
  size_t window_start = 0;
  uint8_t buffer[65536];
  const auto start = std::chrono::steady_clock::now();
  auto next_line = start + std::chrono::duration<double>(opts.every);
  const auto elapsed = [&] {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  };
  while ((opts.seconds == 0 || elapsed() < opts.seconds) &&
         (opts.frames <= 0 || static_cast<long long>(all.size()) < opts.frames)) {
    net::Address from;
    const int n = net::udp_receive(sock, buffer, sizeof buffer, from);
    if (n >= 0) {
      net::CapturedDatagram d;
      if (net::read_tap_frame(buffer, static_cast<size_t>(n), d)) all.push_back(std::move(d));
      continue;
    }
    if (std::chrono::steady_clock::now() >= next_line) {
      next_line += std::chrono::duration<double>(opts.every);
      const std::vector<net::CapturedDatagram> window(all.begin() + static_cast<long>(window_start),
                                                      all.end());
      window_start = all.size();
      const net::CaptureAnalysis a = net::analyze_capture(window, opts.every);
      std::string line =
          "  " + fixed(elapsed(), 1) + " s: " + std::to_string(window.size()) + " datagrams\n";
      for (const net::ConnectionStats& c : a.connections) {
        const net::DirectionStats& out = a.local_known ? net::sent_by(c, a.local) : c.ab;
        const net::DirectionStats& in = a.local_known ? net::received_by(c, a.local) : c.ba;
        line += "    " + net::to_string(in.from) + "  out " + std::to_string(out.packets) +
                " pkts " + fixed(static_cast<double>(out.bytes) * 8 / 1000 / opts.every, 1) +
                " kbps, in " + std::to_string(in.packets) + " pkts " +
                fixed(static_cast<double>(in.bytes) * 8 / 1000 / opts.every, 1) + " kbps, rtt " +
                fixed(net::percentile_of(out.ack_delay_ms, 50), 1) + " ms, loss " + pct(out.loss) +
                " / " + pct(in.loss) + "\n";
      }
      std::fputs(line.c_str(), stderr);
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  net::udp_close(sock);
  if (all.empty()) {
    return failed("nothing arrived on 127.0.0.1:" + std::to_string(opts.port) + " in " +
                      fixed(elapsed(), 1) + " s",
                  "the command line",
                  "open the game's tap on this port (net::open_tap, then transport.tap: "
                  "net/tap.hpp) and play while ge net watch runs",
                  1);
  }
  net::Capture cap = net::make_capture();
  for (const net::CapturedDatagram& d : all)
    net::capture_datagram(cap, d.at_us, d.from, d.to, d.data.data(), d.data.size());
  fs::path path(opts.save);
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  if (!net::save_capture(cap, path.string(), &error)) {
    return failed(error, path.string(), "pass a writable path with --save=FILE", 1);
  }
  NetStatsOptions st;
  st.file = path.string();
  st.charts = false;
  Outcome o = run_net_stats(st);
  o.text = "  watched 127.0.0.1:" + std::to_string(opts.port) + " for " + fixed(elapsed(), 1) +
           " s: " + std::to_string(all.size()) + " datagrams, saved to " + path.string() + "\n" +
           o.text;
  o.written.insert(o.written.begin(), path.string());
  o.next =
      "ge net inspect " + path.string() + ", or ge net stats " + path.string() + " for the charts";
  return o;
}

Outcome run_net_sweep(const NetSweepOptions& opts) {
  static const char* kKnobs =
      "update-rate, cmd-rate, rate, interp, backup, tick-hz, or a link key (latency, jitter, "
      "loss, burst, reorder, bandwidth, drop-every, drop-run)";
  if (opts.knob.empty() || opts.values.empty()) {
    return failed("--knob and --values are needed", "the command line",
                  "rerun with e.g. --knob=update-rate --values=10,20,30,60 (knobs: " +
                      std::string(kKnobs) + ")",
                  2);
  }
  if (opts.clients < 1 || opts.clients > 32 || opts.npcs < 0 || opts.npcs > 512 ||
      !(opts.seconds >= 2) || opts.tick_hz < 1) {
    return failed("the world is out of range", "the command line",
                  "use --clients=1..32, --npcs=0..512, --seconds of 2 or more, --tick-hz above 0",
                  2);
  }
  std::vector<std::string> presets = opts.presets;
  if (presets.empty()) presets = {"wifi"};
  Outcome o;
  std::vector<std::vector<std::string>> rows{{opts.knob, "preset", "down kbit/s", "up kbit/s",
                                              "view ms", "corr/min", "worst m", "dropped",
                                              "starved", "repeated", "rewind p95 m", "digest"}};
  std::vector<std::string> results;
  struct Point {
    std::string preset, value;
    double cost, corr, view;
  };
  std::vector<Point> points;
  for (const std::string& name : presets) {
    for (const std::string& value : opts.values) {
      net::ReplicateTrialConfig k;
      k.clients = opts.clients;
      k.npcs = opts.npcs;
      k.seconds = opts.seconds;
      k.seed = opts.seed;
      k.replicate.tick_hz = opts.tick_hz;
      k.replicate.update_rate = opts.update_rate;
      k.replicate.cmd_rate = opts.cmd_rate;
      k.replicate.rate = static_cast<uint32_t>(opts.rate);
      k.replicate.interp_ticks = opts.interp;
      k.replicate.redundancy = opts.backup;
      if (name != "none" && !net::conditions_preset(name, k.link)) {
        return failed("no preset \"" + name + "\"", "the command line",
                      "rerun with --presets=none,lan,broadband,wifi,mobile,bad,awful", 2);
      }
      if (const std::string bad = apply_link(opts.link, k.link); !bad.empty()) {
        return failed("--link: " + bad, "the command line",
                      "rerun with --link=latency=40,jitter=10,loss=2%", 2);
      }
      double v = 0;
      const bool numeric = parse_double(value, v) && v >= 0;
      const std::string& knob = opts.knob;
      if (knob == "update-rate" || knob == "cmd-rate" || knob == "rate" || knob == "interp" ||
          knob == "backup" || knob == "tick-hz") {
        if (!numeric) {
          return failed("--values: \"" + value + "\" is not a number of 0 or more",
                        "the command line", "rerun with --values=10,20,30", 2);
        }
        if (knob == "update-rate") k.replicate.update_rate = static_cast<int>(v);
        if (knob == "cmd-rate") k.replicate.cmd_rate = static_cast<int>(v);
        if (knob == "rate") k.replicate.rate = static_cast<uint32_t>(v);
        if (knob == "interp") k.replicate.interp_ticks = static_cast<int>(v);
        if (knob == "backup") k.replicate.redundancy = std::max(1, static_cast<int>(v));
        if (knob == "tick-hz") k.replicate.tick_hz = std::max(1, static_cast<int>(v));
      } else if (const std::string bad =
                     apply_link(std::string(knob).append("=").append(value), k.link);
                 !bad.empty()) {
        return failed(std::string("--knob=").append(knob).append(": ").append(bad),
                      "the command line", std::string("pick a knob: ") + kKnobs, 2);
      }
      const net::ReplicateTrial r = net::run_replicate_trial(k);
      std::vector<double> gaps = r.gaps;
      const double p95 = net::percentile_of(gaps, 95);
      const double dropped = r.decoded + r.dropped ? static_cast<double>(r.dropped) /
                                                         static_cast<double>(r.decoded + r.dropped)
                                                   : 0;
      const double repeated = r.inputs + r.repeated ? static_cast<double>(r.repeated) /
                                                          static_cast<double>(r.inputs + r.repeated)
                                                    : 0;
      rows.push_back({value, name, fixed(r.wire_down_kbps, 1), fixed(r.wire_up_kbps, 1),
                      fixed(r.view_delay_ms, 0), fixed(r.corrections_per_min, 1),
                      fixed(r.worst_correction, 2), pct(dropped), std::to_string(r.starved),
                      pct(repeated), fixed(p95, 3), hex(r.hash, 16)});
      points.push_back(
          {name, value, r.wire_down_kbps + r.wire_up_kbps, r.corrections_per_min, r.view_delay_ms});
      results.push_back(json_obj({{"value", json_str(value)},
                                  {"preset", json_str(name)},
                                  {"down_kbps", num(r.wire_down_kbps)},
                                  {"up_kbps", num(r.wire_up_kbps)},
                                  {"view_delay_ms", num(r.view_delay_ms)},
                                  {"corrections_per_min", num(r.corrections_per_min)},
                                  {"worst_correction_m", num(r.worst_correction)},
                                  {"dropped", num(dropped)},
                                  {"starved", json_num(static_cast<long long>(r.starved))},
                                  {"repeated", num(repeated)},
                                  {"rewind_p95_m", num(p95)},
                                  {"owned", json_num(r.owned)},
                                  {"digest", json_str(hex(r.hash, 16))}}));
      if (r.owned != k.clients) {
        o.findings.push_back({Severity::error,
                              std::string(name)
                                  .append(" at ")
                                  .append(knob)
                                  .append("=")
                                  .append(value)
                                  .append(": ")
                                  .append(std::to_string(k.clients - r.owned))
                                  .append(" clients never heard of their own entity"),
                              "engine/net/replicate_server.cpp",
                              "the link starves the session: loosen it, or read send_snapshot "
                              "(skill: net)"});
      }
    }
  }
  // The knee, a preset at a time: the cheapest value whose corrections and view delay are within
  // a tenth (or a correction a minute, or 10 ms) of the best any value reached.
  std::string knees;
  for (const std::string& name : presets) {
    double best_corr = 1e300, best_view = 1e300;
    for (const Point& p : points) {
      if (p.preset != name) continue;
      best_corr = std::min(best_corr, p.corr);
      best_view = std::min(best_view, p.view);
    }
    const Point* knee = nullptr;
    for (const Point& p : points) {
      if (p.preset != name) continue;
      if (p.corr > std::max(best_corr * 1.1, best_corr + 1)) continue;
      if (p.view > std::max(best_view * 1.1, best_view + 10)) continue;
      if (!knee || p.cost < knee->cost) knee = &p;
    }
    if (knee) {
      knees += "  " + name + ": " + opts.knob + "=" + knee->value + " is the cheapest (" +
               fixed(knee->cost, 1) + " kbit/s both ways) within reach of the best corrections (" +
               fixed(best_corr, 1) + "/min) and view delay (" + fixed(best_view, 0) + " ms)\n";
    }
  }
  o.text = "  " + std::to_string(opts.clients) + " clients and " + std::to_string(opts.npcs) +
           " server movers, " + core::num::to_string(opts.seconds) + " s at " +
           std::to_string(opts.tick_hz) + " Hz, seed " + std::to_string(opts.seed) + "\n" +
           table(rows) + knees +
           "  down and up: every packet a client received and sent, headers and keepalives "
           "too; view: how far behind the server the others are drawn; corr/min: snapshots "
           "that moved a client's prediction; dropped: snapshots it could not decode; "
           "starved: rows a rate left out; repeated: server ticks that reused an input; "
           "rewind: the server's rewind against what the client drew\n";
  o.result = json_obj({{"knob", json_str(opts.knob)}, {"runs", json_arr(results)}});
  o.summary = std::to_string(points.size()) + " trials run";
  o.next = "ge net sweep --knob=" + opts.knob + " --presets=wifi,bad,awful (the same values)";
  return o;
}

Outcome run_net(const Args& a) {
  const std::string name(a.command->name);
  const auto number = [&](const char* flag, double fallback, double& out) {
    const std::string v = flag_value(a, flag);
    if (v.empty()) {
      out = fallback;
      return true;
    }
    return parse_double(v, out);
  };
  double x = 0;
  const auto bad = [&](const char* flag) {
    return failed(std::string("--") + flag + "=" + flag_value(a, flag) + " is not a number",
                  "the command line", "run `ge help " + name + "` for its flags", 2);
  };
  if (name == "net inspect") {
    NetInspectOptions n;
    n.file = a.words[0];
    n.peer = flag_value(a, "peer");
    n.type = flag_value(a, "type");
    if (!number("skip", 0, x) || x < 0) return bad("skip");
    n.skip = static_cast<long long>(x);
    if (!number("limit", 200, x) || x < 0) return bad("limit");
    n.limit = static_cast<long long>(x);
    return run_net_inspect(n);
  }
  if (name == "net stats") {
    NetStatsOptions n;
    n.file = a.words[0];
    if (!number("bucket", 250, n.bucket_ms) || n.bucket_ms <= 0) return bad("bucket");
    n.out = flag_value(a, "out");
    n.charts = !has_flag(a, "no-charts");
    return run_net_stats(n);
  }
  if (name == "net watch") {
    NetWatchOptions n;
    if (!number("port", 27950, x)) return bad("port");
    n.port = static_cast<int>(x);
    if (!number("seconds", 10, n.seconds)) return bad("seconds");
    if (!number("every", 1, n.every)) return bad("every");
    if (!number("frames", 0, x)) return bad("frames");
    n.frames = static_cast<long long>(x);
    n.save = flag_value(a, "save", "runs/net-watch.pcap");
    return run_net_watch(n);
  }
  if (name == "net stress") {
    NetStressOptions n;
    n.knobs = split_commas(flag_value(a, "knobs"));
    n.preset = flag_value(a, "preset", "broadband");
    n.mix = flag_value(a, "mix", "racing");
    n.link = flag_value(a, "link");
    n.chaos = split_commas(flag_value(a, "chaos"));
    if (!number("clients", 8, x)) return bad("clients");
    n.clients = static_cast<int>(x);
    if (!number("seconds", 6, n.seconds)) return bad("seconds");
    if (!number("steps", 8, x)) return bad("steps");
    n.steps = static_cast<int>(x);
    if (!number("refine", 4, x)) return bad("refine");
    n.refine = static_cast<int>(x);
    if (!number("soak", 0, n.soak) || n.soak < 0) return bad("soak");
    if (!number("soak-clients", 32, x)) return bad("soak-clients");
    n.soak_clients = static_cast<int>(x);
    if (!number("fuzz", 0, n.fuzz) || n.fuzz < 0) return bad("fuzz");
    if (!number("seed", 1, x) || x < 0) return bad("seed");
    n.seed = static_cast<unsigned>(x);
    return run_net_stress(n);
  }
  if (name == "net sweep") {
    NetSweepOptions n;
    n.knob = flag_value(a, "knob");
    n.values = split_commas(flag_value(a, "values"));
    n.presets = split_commas(flag_value(a, "presets"));
    n.link = flag_value(a, "link");
    if (!number("clients", 16, x)) return bad("clients");
    n.clients = static_cast<int>(x);
    if (!number("npcs", 32, x)) return bad("npcs");
    n.npcs = static_cast<int>(x);
    if (!number("seconds", 10, n.seconds)) return bad("seconds");
    if (!number("tick-hz", 60, x)) return bad("tick-hz");
    n.tick_hz = static_cast<int>(x);
    if (!number("update-rate", 0, x) || x < 0) return bad("update-rate");
    n.update_rate = static_cast<int>(x);
    if (!number("cmd-rate", 0, x) || x < 0) return bad("cmd-rate");
    n.cmd_rate = static_cast<int>(x);
    if (!number("rate", 0, n.rate) || n.rate < 0) return bad("rate");
    if (!number("interp", 6, x) || x < 0) return bad("interp");
    n.interp = static_cast<int>(x);
    if (!number("backup", 4, x) || x < 1) return bad("backup");
    n.backup = static_cast<int>(x);
    if (!number("seed", 1, x) || x < 0) return bad("seed");
    n.seed = static_cast<unsigned>(x);
    return run_net_sweep(n);
  }
  NetSimOptions n;
  n.presets = split_commas(flag_value(a, "presets"));
  n.mix = flag_value(a, "mix", "racing");
  n.link = flag_value(a, "link");
  const std::string transport = flag_value(a, "transport", "udp");
  if (transport != "udp" && transport != "tcp") {
    return failed("--transport=" + transport + " is neither udp nor tcp", "the command line",
                  "rerun with --transport=udp or --transport=tcp", 2);
  }
  n.stream = transport == "tcp";
  if (!number("budget", 0, n.budget_kbps) || n.budget_kbps < 0) return bad("budget");
  if (!number("clients", 8, x)) return bad("clients");
  n.clients = static_cast<int>(x);
  if (!number("max-peers", 32, x)) return bad("max-peers");
  n.max_peers = static_cast<int>(x);
  if (!number("seconds", 10, n.seconds)) return bad("seconds");
  if (!number("drain", 4, n.drain)) return bad("drain");
  if (!number("tick-hz", 60, n.tick_hz)) return bad("tick-hz");
  if (!number("seed", 1, x) || x < 0) return bad("seed");
  n.seed = static_cast<unsigned>(x);
  n.capture = flag_value(a, "capture");
  return run_net_sim(n);
}

}  // namespace ge::cli
