// engine/services/catalog.hpp: a clean catalog passes; every fault check_catalog names (a bad
// id, a duplicate, a missing app id, an achievement's missing stat or unreachable threshold, a
// stat starting out of range, a DLC with the game's own number) is named; the lookups.
//
// ge:summary Test: the services catalog's check and lookups
// ge:keywords test services catalog check_catalog achievement stat board dlc id duplicate
// ge:skill services testing
#include "services/catalog.hpp"

#include <array>
#include <string>

#include "check.hpp"

using namespace ge::services;

namespace {

constexpr std::array<StatDef, 2> kStats{{
    {"RUNS", StatKind::integer, 0, 1000000, true},
    {"METRES", StatKind::real, 0, 1e9, true},
}};
constexpr std::array<AchievementDef, 2> kAchievements{{
    {"FIRST_RUN", "RUNS", 1},
    {"SECRET", {}, 0, true},
}};
constexpr std::array<BoardDef, 1> kBoards{
    {{"DOWNHILL_TIME", BoardOrder::ascending, BoardDisplay::milliseconds}}};
constexpr std::array<DlcDef, 1> kDlc{{{2002, "night_pack"}}};

bool names(const std::vector<std::string>& findings, const std::string& word) {
  for (const std::string& f : findings)
    if (f.find(word) != std::string::npos && f.find("next:") != std::string::npos) return true;
  return false;
}

}  // namespace

TEST(clean_catalog_has_no_findings) {
  const Catalog c{2001, kAchievements, kStats, kBoards, kDlc};
  CHECK(check_catalog(c).empty());
  CHECK(find_achievement(c, "SECRET") == 1);
  CHECK(find_stat(c, "METRES") == 1);
  CHECK(find_board(c, "DOWNHILL_TIME") == 0);
  CHECK(find_dlc(c, 2002) == 0);
  CHECK(find_stat(c, "NOPE") == -1);
}

TEST(every_fault_is_named_with_a_next_action) {
  static constexpr std::array<StatDef, 2> stats{{
      {"RUNS", StatKind::integer, 0, 10, true},
      {"bad id", StatKind::integer, 5, 10, false, 0, 0},
  }};
  static constexpr std::array<AchievementDef, 3> achievements{{
      {"TOO_FAR", "RUNS", 11},
      {"GHOST_STAT", "MISSING", 1},
      {"TOO_FAR", {}, 0},
  }};
  static constexpr std::array<DlcDef, 1> dlc{{{0, "nothing"}}};
  const Catalog c{0, achievements, stats, {}, dlc};
  const std::vector<std::string> f = check_catalog(c);
  CHECK(names(f, "no app id"));
  CHECK(names(f, "'bad id' is not a store id"));
  CHECK(names(f, "'bad id' starts outside"));
  CHECK(names(f, "'TOO_FAR' has a threshold"));
  CHECK(names(f, "'GHOST_STAT' names stat 'MISSING'"));
  CHECK(names(f, "'TOO_FAR' is declared twice"));
  CHECK(names(f, "dlc 'nothing'"));
  CHECK(f.size() == 7);
}
