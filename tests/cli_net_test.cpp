// `ge net` (engine/cli/net.hpp): ge net sim runs the same session the traffic generator does (its
// digest), writes the host's capture, and ge net inspect and ge net stats read that capture back:
// every datagram a line, every client a connection, the series and its charts written; usage
// errors for an unknown preset, mix, link key or packet type and a missing file; ge net stress
// names each knob's breaking point and runs the chaos scripts and the fuzzer it is asked for.
//
// ge:summary Test: ge net sim, inspect and stats on a capture; their usage errors
// ge:keywords test cli net sim inspect stats pcap capture digest stress chaos fuzz
// ge:skill testing agent-interface net
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "cli/finding.hpp"
#include "cli/net.hpp"
#include "net/capture.hpp"
#include "net/conditions.hpp"
#include "net/tap.hpp"
#include "net/traffic.hpp"
#include "net/udp_socket.hpp"

namespace fs = std::filesystem;
using namespace ge::cli;

namespace {

fs::path scratch() {
  const fs::path dir = fs::temp_directory_path() / "ge_cli_net_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

std::string hex16(uint64_t v) {
  static const char* k = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = k[v & 15];
  return s;
}

std::string say(const Outcome& o) {
  std::string s;
  for (const Finding& f : o.findings) s += format_finding("ge net", f);
  return s;
}

}  // namespace

TEST(net_sim_is_the_traffic_generator_s_session_and_its_capture_reads_back) {
  const fs::path dir = scratch();
  NetSimOptions sim;
  sim.presets = {"wifi"};
  sim.clients = 3;
  sim.seconds = 3;
  sim.drain = 2;
  sim.capture = (dir / "wifi.pcap").string();
  const Outcome s = run_net_sim(sim);
  CHECK_MSG(exit_of(s) == 0, say(s));
  CHECK(fs::exists(dir / "wifi.pcap") && s.written.size() == 1);

  ge::net::TrafficPlan plan;
  plan.clients = 3;
  plan.seconds = 3;
  plan.drain_seconds = 2;
  ge::net::conditions_preset("wifi", plan.conditions);
  const uint64_t digest = ge::net::run_traffic(plan).digest;
  CHECK_MSG(s.result.find("\"digest\":\"" + hex16(digest) + "\"") != std::string::npos,
            "ge net sim's digest is not run_traffic's " + hex16(digest) + ": " + s.result);

  NetInspectOptions in;
  in.file = sim.capture;
  in.limit = 5;
  const Outcome i = run_net_inspect(in);
  CHECK_MSG(exit_of(i) == 0, say(i));
  CHECK(i.text.find("request") != std::string::npos &&
        i.text.find("more (--skip=5") != std::string::npos);
  in.type = "accept";
  in.limit = 0;
  const Outcome accepts = run_net_inspect(in);
  CHECK(accepts.result.find("\"matched\":3") != std::string::npos);

  NetStatsOptions st;
  st.file = sim.capture;
  const Outcome t = run_net_stats(st);
  CHECK_MSG(exit_of(t) == 0, say(t));
  CHECK(t.result.find("\"local\":\"sim:0:1\"") != std::string::npos);
  CHECK(t.summary.starts_with("3 connections"));
  CHECK(fs::exists(dir / "wifi-net" / "wifi.net.tsv"));
  CHECK_MSG(t.written.size() > 1, "the series' charts were not written");
}

TEST(net_sim_writes_a_capture_a_preset_when_several) {
  const fs::path dir = scratch();
  NetSimOptions sim;
  sim.presets = {"lan", "none"};
  sim.mix = "lockstep";
  sim.link = "latency=20,loss=1%";
  sim.clients = 2;
  sim.seconds = 1;
  sim.drain = 1;
  sim.capture = (dir / "s.pcap").string();
  const Outcome s = run_net_sim(sim);
  CHECK_MSG(exit_of(s) == 0, say(s));
  CHECK(fs::exists(dir / "s.lan.pcap") && fs::exists(dir / "s.none.pcap"));
  CHECK(s.text.find("no snapshots") != std::string::npos);
  CHECK(s.text.find("20 ms") != std::string::npos);
}

TEST(net_usage_errors_say_what_to_pass) {
  NetSimOptions sim;
  sim.presets = {"moon"};
  CHECK(exit_of(run_net_sim(sim)) == 2);
  sim.presets = {"lan"};
  sim.mix = "chess";
  CHECK(exit_of(run_net_sim(sim)) == 2);
  sim.mix = "racing";
  sim.link = "latency=fast";
  CHECK(exit_of(run_net_sim(sim)) == 2);
  sim.link = "colour=blue";
  CHECK(exit_of(run_net_sim(sim)) == 2);
  sim.link = "loss=1.5";
  CHECK(exit_of(run_net_sim(sim)) == 2);

  NetInspectOptions in;
  in.file = (fs::temp_directory_path() / "ge_cli_net_test_missing.pcap").string();
  const Outcome missing = run_net_inspect(in);
  CHECK(exit_of(missing) == 1 && missing.findings.size() == 1 && !missing.findings[0].next.empty());
  NetStatsOptions st;
  st.file = in.file;
  CHECK(exit_of(run_net_stats(st)) == 1);
}

TEST(net_stress_names_each_knob_s_breaking_point_and_runs_the_scripts) {
  NetStressOptions n;
  n.knobs = {"loss", "latency"};
  n.clients = 3;
  n.seconds = 3;
  n.steps = 4;
  n.refine = 2;
  n.chaos = {"partition", "host-lost"};
  n.fuzz = 5000;
  const Outcome o = run_net_stress(n);
  CHECK_MSG(exit_of(o) == 0, say(o));
  CHECK(o.text.find("loss") != std::string::npos && o.text.find("latency") != std::string::npos);
  CHECK(o.result.find("\"knob\":\"loss\",\"found\":true") != std::string::npos);
  CHECK(o.result.find("\"script\":\"host-lost\",\"ok\":true") != std::string::npos);
  CHECK(o.result.find("\"honest\":true") != std::string::npos);
  CHECK(o.summary == "2 knobs ramped, 2 chaos scripts");

  n.knobs = {"colour"};
  CHECK(exit_of(run_net_stress(n)) == 2);
  n.knobs = {};
  n.chaos = {"earthquake"};
  CHECK(exit_of(run_net_stress(n)) == 2);
}

TEST(net_watch_reads_a_live_tap_and_measures_what_it_saw) {
  const fs::path dir = scratch();
  // What a live node would mirror: a short session's datagrams, replayed through a tap.
  ge::net::TrafficPlan plan;
  plan.clients = 2;
  plan.seconds = 1;
  plan.drain_seconds = 1;
  plan.capture = true;
  const ge::net::TrafficReport r = ge::net::run_traffic(plan);
  std::vector<ge::net::CapturedDatagram> ds;
  CHECK(ge::net::read_capture(r.capture.bytes.data(), r.capture.bytes.size(), ds, nullptr));
  CHECK(ds.size() > 50);
  ge::net::UdpSocket probe;
  std::string err;
  if (!ge::net::udp_open(probe, ge::net::loopback_v4(0), &err)) {
    ge::test::skip("no loopback UDP socket here: " + err);
    return;
  }
  const int port = probe.local.port;  // a free port, handed to the watch
  ge::net::udp_close(probe);
  std::thread sender([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ge::net::Tap tap;
    std::string e;
    if (!ge::net::open_tap(tap, static_cast<uint16_t>(port), &e)) return;
    for (const auto& d : ds) {
      ge::net::tap_datagram(tap, d.at_us, d.from, d.to, d.data.data(), d.data.size());
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    ge::net::close_tap(tap);
  });
  NetWatchOptions w;
  w.port = port;
  w.seconds = 20;
  w.frames = static_cast<long long>(ds.size());
  w.save = (dir / "watch.pcap").string();
  const Outcome o = run_net_watch(w);
  sender.join();
  CHECK_MSG(exit_of(o) == 0, say(o));
  std::vector<ge::net::CapturedDatagram> back;
  CHECK(ge::net::load_capture(w.save, back, nullptr));
  CHECK(back.size() == ds.size());
  CHECK(o.text.find("watched 127.0.0.1:") != std::string::npos);
  // Nobody tapping: the watch says so and how to open a tap.
  NetWatchOptions idle;
  idle.port = port;
  idle.seconds = 0.3;
  idle.save = (dir / "idle.pcap").string();
  const Outcome none = run_net_watch(idle);
  CHECK(exit_of(none) == 1);
  CHECK(say(none).find("open_tap") != std::string::npos);
}
