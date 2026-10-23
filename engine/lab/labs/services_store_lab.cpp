// LAB `services`: a game's whole use of the platform services played on the simulated store by
// four players (two of them friends) on one script: runs counted into stats and committed, an
// achievement unlocked by its stat's threshold and one by the game, times uploaded to a board
// with a ghost riding on each, the board downloaded globally, around a player and among
// friends and the ghost fetched back, the record book written to the cloud and refused past the
// quota, presence shown to a friend who joins by it, an invite accepted, the overlay opened, the
// screenshot key hooked, a connection cut and restored, and a commit staged to fail. It answers
// whether a game written against the interface gets every answer it is owed, in order, and how
// long each kind of answer takes at the store's latency (`--latency-ms`, `--jitter-ms`). The
// store's journal and every event are tabled; everything runs on a virtual clock off one seed,
// so the lab is a determinism test too.
//
// ge:summary Lab: a game's use of the platform services by four players on the simulated store
// ge:keywords lab services store sim achievement stat board ghost blob cloud presence invite
// ge:keywords join overlay screenshot offline latency journal
// ge:skill services labs
// ge:test tests/services_sim_store_test.cpp
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lab/lab.hpp"
#include "services/sim_store.hpp"

namespace {

namespace gl = ge::lab;
namespace gs = ge::services;

constexpr std::array<gs::StatDef, 2> kStats{{
    {"RUNS", gs::StatKind::integer, 0, 1000000, true},
    {"METRES", gs::StatKind::real, 0, 1e9, true},
}};
constexpr std::array<gs::AchievementDef, 3> kAchievements{{
    {"FIRST_RUN", "RUNS", 1},
    {"FIVE_RUNS", "RUNS", 5},
    {"CLEAN_RUN", {}, 0},
}};
constexpr std::array<gs::BoardDef, 1> kBoards{
    {{"DOWNHILL_TIME", gs::BoardOrder::ascending, gs::BoardDisplay::milliseconds}}};
constexpr std::array<gs::DlcDef, 1> kDlc{{{9002, "night_pack"}}};
const gs::Catalog kCatalog{9001, kAchievements, kStats, kBoards, kDlc};

constexpr int64_t kFrame = 16667;  // the game polls once a 60 Hz frame

struct Player {
  std::string name;
  gs::UserId id = 0;
  std::unique_ptr<gs::Backend> backend;
};

struct Asked {
  int64_t at_us;
  std::string player;
};

void run(gl::Lab& lab) {
  gs::SimConfig config;
  config.latency_us = static_cast<int64_t>(lab.arg_num("latency-ms") * 1000.0);
  config.jitter_us = static_cast<int64_t>(lab.arg_num("jitter-ms") * 1000.0);
  config.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  config.cloud_quota_bytes = 4096;
  gs::SimStore store(kCatalog, config);
  for (const std::string& f : gs::check_catalog(kCatalog))
    lab.fail(f, "next: fix the lab's catalog; skill: services");

  std::array<Player, 4> players;
  const std::array<const char*, 4> names{"ana", "bo", "cy", "dee"};
  for (size_t i = 0; i < players.size(); i++) players[i].name = names[i];
  for (Player& p : players) p.id = store.add_user(p.name);
  store.befriend(players[0].id, players[1].id);
  store.grant(players[0].id, 9002);
  for (Player& p : players) p.backend = store.backend(p.id);
  gs::Backend& ana = *players[0].backend;
  gs::Backend& bo = *players[1].backend;

  gl::LabTable events{"Every event, in the order each player's poll delivered it",
                      {"t s", "player", "event", "request", "result", "what"},
                      {}};
  std::map<std::pair<std::string, gs::RequestId>, Asked> asked;
  std::map<std::string, std::vector<double>> waits_ms;  // by event kind
  std::map<std::string, int> counts;
  std::vector<uint8_t> fetched_ghost;
  gs::BlobId best_blob = 0;
  const auto ask = [&](const std::string& who, gs::RequestId r, int64_t now) {
    asked[{who, r}] = {now, who};
  };

  const int64_t end_us = 12000000;
  for (int64_t now = 0; now <= end_us; now += kFrame) {
    const int frame = static_cast<int>(now / kFrame);
    // The script, by frame: each player's runs end at its own times.
    for (size_t i = 0; i < players.size(); i++) {
      Player& p = players[i];
      if (frame % 60 == static_cast<int>(i) * 7 && frame / 60 < 6) {  // a run a second, six each
        const int run_n = frame / 60;
        p.backend->add_stat("RUNS", 1);
        p.backend->add_stat("METRES", 1850.5);
        if (i == 2 && run_n == 3) p.backend->unlock("CLEAN_RUN");
        ask(p.name, p.backend->store_stats(), now);
        const int32_t time_ms = 62000 - static_cast<int32_t>(i) * 900 - run_n * (300 + 50 * run_n);
        std::vector<uint8_t> ghost(256 + static_cast<size_t>(run_n) * 64,
                                   static_cast<uint8_t>(i * 16 + static_cast<size_t>(run_n)));
        const std::array<int32_t, 2> details{38, run_n};
        ask(p.name,
            p.backend->upload_score("DOWNHILL_TIME", time_ms, details, gs::UploadMode::keep_best,
                                    ghost),
            now);
      }
    }
    if (frame == 30) {
      ana.set_presence("status", "Skiing seed 38");
      ana.set_presence("connect", "+join 38");
      ana.hook_screenshots(true);
    }
    if (frame == 100) store.fail_next(players[2].id, gs::Result::rate_limited);
    if (frame == 90) store.join_friend(players[1].id, players[0].id);
    if (frame == 120) ana.invite(players[1].id, "+join 38 --team=blue");
    if (frame == 180) store.accept_invite(players[1].id, players[0].id);
    if (frame == 200) store.press_screenshot_key(players[0].id);
    if (frame == 240) ana.open_overlay(gs::OverlayPage::achievements);
    if (frame == 300) store.set_online(players[3].id, false);
    if (frame == 420) store.set_online(players[3].id, true);
    if (frame == 500) {
      std::vector<uint8_t> book(3000, 1);
      if (ana.cloud_write("book/records.bin", book) != gs::Result::ok)
        lab.fail("services: the record book was refused under the quota", "rerun with the journal");
      if (ana.cloud_write("ghosts/best.bin", std::vector<uint8_t>(2000, 2)) !=
          gs::Result::over_quota)
        lab.fail("services: a cloud write past the quota was taken",
                 "next: check SimBackend::cloud_write; skill: services");
    }
    if (frame == 560) {
      ask("ana", ana.download_scores("DOWNHILL_TIME", gs::BoardRange::global, 1, 10), now);
      ask("ana", ana.download_scores("DOWNHILL_TIME", gs::BoardRange::around_user, -1, 1), now);
      ask("bo", bo.download_scores("DOWNHILL_TIME", gs::BoardRange::friends, 0, 0), now);
    }
    if (frame == 600 && best_blob != 0) ask("bo", bo.download_blob(best_blob), now);

    for (Player& p : players) {
      std::vector<gs::Event> out;
      p.backend->poll(now, out);
      for (const gs::Event& e : out) {
        const std::string kind(gs::event_name(e.kind));
        counts[kind]++;
        std::string what = e.id;
        if (e.kind == gs::EventKind::score_uploaded)
          what += " " + std::to_string(e.score) + " ms rank " + std::to_string(e.rank) +
                  (e.changed ? " (new best)" : "");
        if (e.kind == gs::EventKind::scores_downloaded) {
          what += " " + std::to_string(e.entries.size()) + "/" + std::to_string(e.total) + ":";
          for (const gs::BoardEntry& en : e.entries)
            what += " " + std::to_string(en.rank) + "." + en.name;
          if (!e.entries.empty() && e.entries[0].rank == 1) best_blob = e.entries[0].blob;
        }
        if (e.kind == gs::EventKind::blob_downloaded) {
          what = std::to_string(e.bytes.size()) + " B";
          fetched_ghost = e.bytes;
        }
        if (!e.text.empty()) what += " " + e.text;
        const auto it = asked.find({p.name, e.request});
        if (e.request != 0 && it != asked.end() && e.kind != gs::EventKind::achievement_unlocked)
          waits_ms[kind].push_back(static_cast<double>(now - it->second.at_us) / 1000.0);
        char t[16];
        std::snprintf(t, sizeof t, "%.3f", static_cast<double>(now) / 1e6);
        events.rows.push_back({t, p.name, kind, e.request ? std::to_string(e.request) : "",
                               std::string(gs::result_name(e.result)), what});
      }
    }
  }

  // What the script is owed.
  const auto owed = [&](const char* kind, int n) {
    if (counts[kind] != n)
      lab.fail("services: " + std::string(kind) + " arrived " + std::to_string(counts[kind]) +
                   " times, the script is owed " + std::to_string(n),
               "read the events table and the journal; skill: services");
  };
  owed("stats_stored", 24);
  owed("score_uploaded", 24);
  owed("achievement_unlocked", 9);  // FIRST_RUN and FIVE_RUNS each, CLEAN_RUN once
  owed("scores_downloaded", 3);
  owed("blob_downloaded", 1);
  owed("join_requested", 2);
  owed("invite_received", 1);
  owed("screenshot_requested", 1);
  owed("overlay_shown", 1);
  owed("overlay_hidden", 1);
  owed("connection_lost", 1);
  owed("connection_restored", 1);
  if (fetched_ghost.empty())
    lab.fail("services: the best entry's ghost never came back",
             "next: check upload_score's blob; skill: services");

  int refused = 0;
  for (const auto& row : events.rows)
    if (row[4] != "ok") refused++;
  lab.metric("events", static_cast<double>(events.rows.size()), "count");
  lab.metric("answers-refused", refused, "count");
  lab.metric("ghost-bytes", static_cast<double>(fetched_ghost.size()), "B");
  gl::LabTable waits{
      "How long each kind of answer took, from the call to the poll that "
      "delivered it (a 60 Hz poll rounds it up to a frame)",
      {"answer", "count", "min ms", "max ms"},
      {}};
  for (const auto& [kind, ms] : waits_ms) {
    double lo = ms[0], hi = ms[0];
    for (const double v : ms) lo = std::min(lo, v), hi = std::max(hi, v);
    char a[16], b[16];
    std::snprintf(a, sizeof a, "%.1f", lo);
    std::snprintf(b, sizeof b, "%.1f", hi);
    waits.rows.push_back({kind, std::to_string(ms.size()), a, b});
    lab.metric("wait-max-" + kind, hi, "ms");
  }
  lab.table(std::move(waits));
  lab.table(std::move(events));
  gl::LabTable journal{
      "The store's journal: every call each side made, and its answer", {"line"}, {}};
  for (const std::string& line : store.journal()) journal.rows.push_back({line});
  lab.table(std::move(journal));
}

const gl::LabSpec kSpec{
    "services",
    gl::LabKind::scenario,
    "engine",
    "engine/services",
    "services",
    "Does a game written against the platform services get every answer it is owed, in order, "
    "on the simulated store, and how long does each kind of answer take?",
    {
        {"latency-ms", "50", "the store's round trip"},
        {"jitter-ms", "30", "up to this much more per answer, off the seed"},
        {"seed", "38", "the jitter's stream"},
    },
    "",
    run,
};
GE_LAB(kSpec);

}  // namespace
