// engine/services/sim_store.hpp's offline store: a player's committed stats and achievements,
// best scores with their blobs and cloud files survive closing the store and opening the
// directory again; a store saved whole loads back the same; a damaged file is refused with a
// line that says what to do.
//
// ge:summary Test: the offline store kept in a directory and read back
// ge:keywords test services offline store save load open_offline persist cloud blob
// ge:skill services testing
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "services/sim_store.hpp"

using namespace ge::services;
namespace fs = std::filesystem;

namespace {

constexpr std::array<StatDef, 1> kStats{{{"RUNS", StatKind::integer, 0, 1000000, true}}};
constexpr std::array<AchievementDef, 1> kAchievements{{{"FIRST_RUN", "RUNS", 1}}};
constexpr std::array<BoardDef, 1> kBoards{
    {{"TIME", BoardOrder::ascending, BoardDisplay::milliseconds}}};
const Catalog kCatalog{2001, kAchievements, kStats, kBoards, {}};

fs::path fresh_dir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

}  // namespace

TEST(the_offline_store_keeps_progress_between_runs) {
  const fs::path dir = fresh_dir("ge_services_offline_test");
  {
    std::string error;
    Offline o = open_offline(kCatalog, dir.string(), "Ana Skier", &error);
    CHECK_MSG(o.store != nullptr, error.c_str());
    CHECK(o.backend->name() == "offline");
    CHECK(o.backend->user().name == "Ana Skier");
    o.backend->add_stat("RUNS", 3);
    o.backend->store_stats();
    const std::array<uint8_t, 4> ghost{9, 8, 7, 6};
    o.backend->upload_score("TIME", 61234, {}, UploadMode::keep_best, ghost);
    CHECK(o.backend->cloud_write("book/records.bin", std::vector<uint8_t>{1, 2, 3}) == Result::ok);
    std::vector<Event> ev;
    o.backend->poll(0, ev);  // no latency offline
    CHECK(ev.size() == 3);
    o.backend->add_stat("RUNS", 1);  // never committed: not kept
  }
  std::string error;
  Offline o = open_offline(kCatalog, dir.string(), "someone else", &error);
  CHECK_MSG(o.store != nullptr, error.c_str());
  CHECK(o.backend->user().name == "Ana Skier");
  double runs = 0;
  bool got = false;
  CHECK(o.backend->stat("RUNS", runs) == Result::ok && runs == 3);
  CHECK(o.backend->achieved("FIRST_RUN", got) == Result::ok && got);
  std::vector<uint8_t> back;
  CHECK(o.backend->cloud_read("book/records.bin", back) == Result::ok && back.size() == 3);
  o.backend->download_scores("TIME", BoardRange::global, 1, 1);
  std::vector<Event> ev;
  o.backend->poll(0, ev);
  CHECK(ev.size() == 1 && ev[0].entries.size() == 1 && ev[0].entries[0].score == 61234);
  o.backend->download_blob(ev[0].entries[0].blob);
  ev.clear();
  o.backend->poll(0, ev);
  CHECK(ev.size() == 1 && ev[0].bytes.size() == 4 && ev[0].bytes[3] == 6);
  fs::remove_all(dir);
}

TEST(a_whole_store_saves_and_loads_back) {
  const fs::path dir = fresh_dir("ge_services_offline_test_whole");
  SimStore a(kCatalog);
  const UserId ana = a.add_user("ana", "swedish", "SE"), bo = a.add_user("bo");
  a.befriend(ana, bo);
  a.grant(bo, 2002, false);
  auto b = a.backend(bo);
  b->add_stat("RUNS", 2);
  b->store_stats();
  b->upload_score("TIME", 5000, std::vector<int32_t>{4, 5}, UploadMode::force, {});
  std::string error;
  CHECK_MSG(a.save(dir.string(), &error), error.c_str());
  SimStore c(kCatalog);
  CHECK_MSG(c.load(dir.string(), &error), error.c_str());
  CHECK(c.users().size() == 2);
  auto cb = c.backend(bo);
  auto ca = c.backend(ana);
  CHECK(ca->user().language == "swedish" && ca->user().country == "SE");
  double runs = 0;
  CHECK(cb->stat("RUNS", runs) == Result::ok && runs == 2);
  CHECK(cb->owns(2002) && !cb->installed(2002));
  std::vector<Friend> friends;
  cb->friends(friends);
  CHECK(friends.size() == 1 && friends[0].id == ana);
  // A new player after a load takes a new number.
  CHECK(c.add_user("cy") != ana && c.users().size() == 3);
  fs::remove_all(dir);
}

TEST(a_damaged_store_is_refused_with_a_next_action) {
  const fs::path dir = fresh_dir("ge_services_offline_test_bad");
  fs::create_directories(dir);
  std::ofstream(dir / "store.txt") << "ge-services-store 1\nmystery 1 2 3\n";
  std::string error;
  Offline o = open_offline(kCatalog, dir.string(), "ana", &error);
  CHECK(o.store == nullptr && o.backend == nullptr);
  CHECK(error.find("line 2") != std::string::npos && error.find("next:") != std::string::npos);
  std::ofstream(dir / "store.txt") << "something else\n";
  o = open_offline(kCatalog, dir.string(), "ana", &error);
  CHECK(o.store == nullptr && error.find("not a store file") != std::string::npos);
  fs::remove_all(dir);
}
