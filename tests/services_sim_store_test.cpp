// engine/services/sim_store.hpp: the simulated store held to the catalog and to its clock.
// Stats and achievements change locally and commit by store_stats (a stat's threshold unlocks
// its achievement, a jump past max_change is thrown out); answers arrive after the latency and
// not before; a cut connection and a staged failure answer as asked; leaderboards rank, keep
// the best, carry details and a blob, and download by range, around the player and among
// friends; cloud files keep to their names and the quota; presence reaches friends only;
// invites and joins; the overlay; licences; screenshots; and a script replays to the same
// journal.
//
// ge:summary Test: the simulated store: commits, latency, failures, boards, cloud, friends
// ge:keywords test services sim store achievement stat store_stats leaderboard blob cloud quota
// ge:keywords presence invite join overlay dlc screenshot offline fail_next determinism
// ge:skill services testing
#include "services/sim_store.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "check.hpp"

using namespace ge::services;

namespace {

constexpr std::array<StatDef, 3> kStats{{
    {"RUNS", StatKind::integer, 0, 1000000, true},
    {"TOP_SPEED", StatKind::real, 0, 400, false, 0, 0},
    {"METRES", StatKind::real, 0, 1e9, true, 50000},
}};
constexpr std::array<AchievementDef, 3> kAchievements{{
    {"FIRST_RUN", "RUNS", 1},
    {"TEN_RUNS", "RUNS", 10},
    {"FINISHED", {}, 0},
}};
constexpr std::array<BoardDef, 2> kBoards{{
    {"TIME", BoardOrder::ascending, BoardDisplay::milliseconds},
    {"SCORE", BoardOrder::descending, BoardDisplay::numeric},
}};
constexpr std::array<DlcDef, 1> kDlc{{{2002, "pack"}}};
const Catalog kCatalog{2001, kAchievements, kStats, kBoards, kDlc};

constexpr int64_t kLatency = 50000;

std::vector<Event> poll(Backend& b, int64_t now) {
  std::vector<Event> out;
  b.poll(now, out);
  return out;
}

const Event* find(const std::vector<Event>& events, EventKind k) {
  for (const Event& e : events)
    if (e.kind == k) return &e;
  return nullptr;
}

}  // namespace

TEST(stats_commit_after_the_latency_and_unlock_by_threshold) {
  SimStore store(kCatalog);
  const UserId ana = store.add_user("ana");
  auto b = store.backend(ana);
  CHECK(b->name() == "sim");
  CHECK(b->unlock("FINISHED") == Result::ok);
  CHECK(b->add_stat("RUNS", 1) == Result::ok);
  bool got = false;
  CHECK(b->achieved("FINISHED", got) == Result::ok && got);    // local at once
  CHECK(b->achieved("FIRST_RUN", got) == Result::ok && !got);  // a threshold waits for the commit
  const RequestId r = b->store_stats();
  CHECK(poll(*b, kLatency - 1).empty());
  const std::vector<Event> ev = poll(*b, kLatency);
  CHECK(ev.size() == 3);
  CHECK(ev[0].kind == EventKind::stats_stored && ev[0].request == r && ev[0].result == Result::ok);
  CHECK(ev[1].kind == EventKind::achievement_unlocked && ev[1].id == "FIRST_RUN");
  CHECK(ev[2].kind == EventKind::achievement_unlocked && ev[2].id == "FINISHED");
  CHECK(b->achieved("FIRST_RUN", got) == Result::ok && got);
  // A second commit unlocks nothing again.
  b->store_stats();
  CHECK(poll(*b, 2 * kLatency).size() == 1);
}

TEST(the_catalog_refuses_bad_values) {
  SimStore store(kCatalog);
  auto b = store.backend(store.add_user("ana"));
  CHECK(b->unlock("NOPE") == Result::unknown_id);
  CHECK(b->set_stat("NOPE", 1) == Result::unknown_id);
  CHECK(b->set_stat("RUNS", 1.5) == Result::invalid);       // an integer stat
  CHECK(b->set_stat("RUNS", -1) == Result::invalid);        // under min
  CHECK(b->set_stat("TOP_SPEED", 401) == Result::invalid);  // over max
  CHECK(b->set_stat("RUNS", 5) == Result::ok);
  CHECK(b->set_stat("RUNS", 4) == Result::invalid);  // increment-only
  CHECK(b->set_stat("TOP_SPEED", 30) == Result::ok);
  CHECK(b->set_stat("TOP_SPEED", 20) == Result::ok);  // not increment-only
  double v = 0;
  CHECK(b->stat("RUNS", v) == Result::ok && v == 5);
  CHECK(b->show_progress("TEN_RUNS", 5, 10) == Result::ok);
  CHECK(b->show_progress("TEN_RUNS", 11, 10) == Result::invalid);
}

TEST(a_jump_past_max_change_is_thrown_out) {
  SimStore store(kCatalog);
  auto b = store.backend(store.add_user("ana"));
  CHECK(b->add_stat("METRES", 60000) == Result::ok);
  b->store_stats();
  const std::vector<Event> ev = poll(*b, kLatency);
  CHECK(ev.size() == 1 && ev[0].result == Result::invalid);
  double v = -1;
  CHECK(b->stat("METRES", v) == Result::ok && v == 0);
}

TEST(offline_and_staged_failures_answer_as_asked) {
  SimStore store(kCatalog);
  const UserId ana = store.add_user("ana");
  auto b = store.backend(ana);
  store.set_online(ana, false);
  CHECK(!b->online());
  b->add_stat("RUNS", 1);
  b->store_stats();
  std::vector<Event> ev = poll(*b, kLatency);
  CHECK(ev.size() == 2 && ev[0].kind == EventKind::connection_lost);
  CHECK(ev[1].kind == EventKind::stats_stored && ev[1].result == Result::offline);
  store.set_online(ana, true);
  store.fail_next(ana, Result::rate_limited);
  b->store_stats();
  ev = poll(*b, 2 * kLatency);
  CHECK(ev.size() == 2 && ev[0].kind == EventKind::connection_restored);
  CHECK(ev[1].result == Result::rate_limited);
  b->store_stats();  // the failure was for one call only; the unsent stat commits now
  ev = poll(*b, 3 * kLatency);
  CHECK(find(ev, EventKind::stats_stored)->result == Result::ok);
  CHECK(find(ev, EventKind::achievement_unlocked) != nullptr);
}

TEST(boards_rank_keep_the_best_and_carry_blobs) {
  SimStore store(kCatalog);
  const UserId ana = store.add_user("ana"), bo = store.add_user("bo"), cy = store.add_user("cy"),
               dee = store.add_user("dee");
  store.befriend(ana, bo);
  auto a = store.backend(ana), b = store.backend(bo), c = store.backend(cy), d = store.backend(dee);
  const std::array<int32_t, 2> details{38, 7};
  const std::array<uint8_t, 3> ghost{1, 2, 3};
  b->upload_score("TIME", 61000, {}, UploadMode::keep_best, {});
  c->upload_score("TIME", 59000, {}, UploadMode::keep_best, {});
  d->upload_score("TIME", 61000, {}, UploadMode::keep_best, {});  // ties bo, later: below him
  a->upload_score("TIME", 60000, details, UploadMode::keep_best, ghost);
  std::vector<Event> ev = poll(*a, kLatency);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::score_uploaded && ev[0].changed);
  CHECK(ev[0].rank == 2 && ev[0].previous_rank == 0 && ev[0].total == 4 && ev[0].blob != 0);
  const BlobId blob = ev[0].blob;
  a->upload_score("TIME", 65000, {}, UploadMode::keep_best, {});  // worse: kept as it was
  ev = poll(*a, 2 * kLatency);
  CHECK(!ev[0].changed && ev[0].rank == 2 && ev[0].previous_rank == 2 && ev[0].blob == 0);
  a->download_scores("TIME", BoardRange::global, 1, 10);
  ev = poll(*a, 3 * kLatency);
  CHECK(ev[0].entries.size() == 4);
  CHECK(ev[0].entries[0].name == "cy" && ev[0].entries[1].name == "ana");
  CHECK(ev[0].entries[2].name == "bo" && ev[0].entries[3].name == "dee");
  CHECK(ev[0].entries[1].details.size() == 2 && ev[0].entries[1].details[1] == 7);
  a->download_scores("TIME", BoardRange::around_user, -1, 0);
  ev = poll(*a, 4 * kLatency);
  CHECK(ev[0].entries.size() == 2 && ev[0].entries[0].rank == 1 && ev[0].entries[1].rank == 2);
  a->download_scores("TIME", BoardRange::friends, 0, 0);
  ev = poll(*a, 5 * kLatency);
  CHECK(ev[0].entries.size() == 2 && ev[0].entries[0].name == "ana" &&
        ev[0].entries[1].name == "bo" && ev[0].entries[1].rank == 3);
  b->download_blob(blob);
  ev = poll(*b, 5 * kLatency);
  CHECK(ev.size() == 2 && ev[1].kind == EventKind::blob_downloaded && ev[1].bytes.size() == 3);
  b->download_blob(999);
  CHECK(poll(*b, 6 * kLatency)[0].result == Result::not_found);
  a->upload_score("TIME", 99000, {}, UploadMode::force, {});
  ev = poll(*a, 6 * kLatency);
  CHECK(ev[0].changed && ev[0].rank == 4 && ev[0].previous_rank == 2);
  a->upload_score("NOPE", 1, {}, UploadMode::force, {});
  const std::vector<int32_t> many(65, 0);
  a->upload_score("SCORE", 1, many, UploadMode::force, {});
  ev = poll(*a, 7 * kLatency);
  CHECK(ev[0].result == Result::unknown_id && ev[1].result == Result::invalid);
}

TEST(cloud_files_keep_their_names_and_the_quota) {
  SimConfig config;
  config.cloud_quota_bytes = 10;
  SimStore store(kCatalog, config);
  auto b = store.backend(store.add_user("ana"));
  const std::vector<uint8_t> six(6, 7);
  CHECK(b->cloud_write("book/records.bin", six) == Result::ok);
  CHECK(b->cloud_write("ghost.bin", six) == Result::over_quota);
  CHECK(b->cloud_write("book/records.bin", std::vector<uint8_t>(10, 1)) == Result::ok);  // replaces
  CHECK(b->cloud_write("../escape", six) == Result::invalid);
  CHECK(b->cloud_write("/abs", six) == Result::invalid);
  CHECK(b->cloud_write("sp ace", six) == Result::invalid);
  std::vector<uint8_t> back;
  CHECK(b->cloud_read("book/records.bin", back) == Result::ok && back.size() == 10);
  CHECK(b->cloud_read("missing", back) == Result::not_found);
  uint64_t total = 0, available = 0;
  b->cloud_quota(total, available);
  CHECK(total == 10 && available == 0);
  std::vector<CloudFile> files;
  b->cloud_files(files);
  CHECK(files.size() == 1 && files[0].name == "book/records.bin");
  CHECK(b->cloud_delete("book/records.bin") == Result::ok);
  CHECK(b->cloud_delete("book/records.bin") == Result::not_found);
}

TEST(presence_invites_and_joins) {
  SimStore store(kCatalog);
  const UserId ana = store.add_user("ana"), bo = store.add_user("bo"), cy = store.add_user("cy");
  store.befriend(ana, bo);
  auto a = store.backend(ana), b = store.backend(bo), c = store.backend(cy);
  CHECK(a->set_presence("status", "Skiing the north face") == Result::ok);
  CHECK(a->set_presence("connect", "+join 7") == Result::ok);
  CHECK(b->friend_presence(ana, "status") == "Skiing the north face");
  CHECK(c->friend_presence(ana, "status").empty());  // not a friend
  std::vector<Event> ev = poll(*b, kLatency);
  CHECK(ev.size() == 2 && ev[0].kind == EventKind::presence_changed && ev[0].user == ana);
  std::vector<Friend> friends;
  b->friends(friends);
  CHECK(friends.size() == 1 && friends[0].name == "ana" && friends[0].state == Presence::online);
  CHECK(store.join_friend(bo, ana));
  ev = poll(*b, kLatency);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::join_requested && ev[0].text == "+join 7");
  CHECK(!store.join_friend(cy, ana));  // not a friend
  CHECK(a->invite(cy, "+join 7") == Result::not_found);
  CHECK(a->invite(bo, "+join 8") == Result::ok);
  ev = poll(*b, 2 * kLatency);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::invite_received && ev[0].user == ana);
  CHECK(store.accept_invite(bo, ana));
  ev = poll(*b, 2 * kLatency);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::join_requested && ev[0].text == "+join 8");
  CHECK(a->set_presence(std::string(65, 'k'), "v") == Result::invalid);
  for (int i = 0; i < 18; i++) CHECK(a->set_presence("k" + std::to_string(i), "v") == Result::ok);
  CHECK(a->set_presence("one_too_many", "v") == Result::invalid);
  a->clear_presence();
  CHECK(b->friend_presence(ana, "status").empty());
}

TEST(overlay_licences_and_screenshots) {
  SimStore store(kCatalog);
  const UserId ana = store.add_user("ana");
  auto b = store.backend(ana);
  CHECK(b->open_overlay(OverlayPage::achievements) == Result::ok);
  std::vector<Event> ev = poll(*b, 0);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::overlay_shown);
  ev = poll(*b, store.config().overlay_us);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::overlay_hidden);
  CHECK(b->open_overlay(OverlayPage::url) == Result::invalid);
  Environment env;
  env.overlay = false;
  env.handheld = true;
  store.set_environment(ana, env);
  CHECK(b->environment().handheld);
  CHECK(b->open_overlay(OverlayPage::friends) == Result::unsupported);
  CHECK(b->owns(2001) && !b->owns(2002) && !b->installed(2002));
  store.grant(ana, 2002, false);
  CHECK(b->owns(2002) && !b->installed(2002));
  const std::vector<uint8_t> rgb(size_t{4} * 2 * 3, 200);
  CHECK(b->add_screenshot(rgb, 4, 2, "the summit") == Result::ok);
  CHECK(b->add_screenshot(rgb, 4, 3, "wrong size") == Result::invalid);
  store.press_screenshot_key(ana);  // not hooked: the store captures it
  CHECK(store.screenshots(ana) == 2);
  b->hook_screenshots(true);
  store.press_screenshot_key(ana);
  ev = poll(*b, store.config().overlay_us);
  CHECK(ev.size() == 1 && ev[0].kind == EventKind::screenshot_requested);
  CHECK(store.screenshots(ana) == 2);
}

TEST(a_script_replays_to_the_same_journal) {
  const auto run = [] {
    SimConfig config;
    config.jitter_us = 30000;
    config.seed = 38;
    SimStore store(kCatalog, config);
    const UserId ana = store.add_user("ana"), bo = store.add_user("bo");
    store.befriend(ana, bo);
    auto a = store.backend(ana), b = store.backend(bo);
    std::vector<Event> all;
    for (int i = 0; i < 20; i++) {
      const int64_t t = int64_t{i} * 20000;
      a->add_stat("RUNS", 1);
      a->store_stats();
      b->upload_score("SCORE", i * 7 % 13, {}, UploadMode::keep_best, {});
      a->poll(t, all);
      b->poll(t, all);
    }
    std::string out;
    for (const std::string& line : store.journal()) out += line + "\n";
    for (const Event& e : all)
      out += std::string(event_name(e.kind)) + " " + std::to_string(e.request) + " " + e.id + "\n";
    return out;
  };
  const std::string first = run();
  CHECK(!first.empty());
  CHECK(first == run());
}
