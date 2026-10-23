// THE GAME'S CATALOG: every achievement, stat, leaderboard and downloadable content a game
// declares to a store, stated once as constant tables in the game's own code. Every backend reads
// it: the simulated and offline stores (sim_store.hpp) hold a game to it (an unknown id, a stat
// out of its range, an increment-only stat lowered are refused at the call), and a store's real
// backend finds its boards and checks its achievements against it at start-up. A store's own
// configuration (the achievements and stats entered on its partner site) must name the same ids.
//
// Ids are the API names a store keys on: ASCII letters, digits and '_', at most 64 characters
// (a board's at most 128). `check_catalog` names every id that breaks that, every duplicate, an
// achievement whose stat is missing or whose threshold is out of its stat's range.
//
// ge:summary The game's catalog: achievements, stats, leaderboards and DLC declared once
// ge:keywords services store catalog achievement stat leaderboard board dlc downloadable-content
// ge:keywords check_catalog find_achievement find_stat find_board stat-threshold increment-only
// ge:keywords api-name steam game-center play-games platform-services
// ge:skill services
// ge:test tests/services_catalog_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ge::services {

using AppId = uint32_t;  // the store's number for a game or one of its DLC

// A stat is a whole number or a real one; the store keeps each kind its own way.
enum class StatKind : uint8_t { integer, real };

struct StatDef {
  std::string_view id;
  StatKind kind = StatKind::integer;
  double min = 0;  // the store refuses a value outside [min, max]
  double max = 2147483647.0;
  bool increment_only = false;  // a lifetime counter: a lower value is refused
  double max_change = 0;        // the most it may move in one store_stats; 0: no limit
  double initial = 0;
};

// An achievement unlocked by the game (`unlock`), or by its stat reaching `threshold` (the store
// unlocks it when the stats are stored, as a store with progress stats does).
struct AchievementDef {
  std::string_view id;
  std::string_view stat;  // empty: unlocked only by `unlock`
  double threshold = 0;
  bool hidden = false;  // not shown until unlocked
};

// Which score is better: `ascending` keeps the lowest first (a time), `descending` the highest.
enum class BoardOrder : uint8_t { ascending, descending };
// How the store shows a score: a number, or a time kept in seconds or in milliseconds.
enum class BoardDisplay : uint8_t { numeric, seconds, milliseconds };

struct BoardDef {
  std::string_view id;
  BoardOrder order = BoardOrder::descending;
  BoardDisplay display = BoardDisplay::numeric;
};

struct DlcDef {
  AppId app = 0;
  std::string_view id;  // the game's own name for it
};

struct Catalog {
  AppId app = 0;  // the game's own number on the store
  std::span<const AchievementDef> achievements;
  std::span<const StatDef> stats;
  std::span<const BoardDef> boards;
  std::span<const DlcDef> dlc;
};

// The catalog's faults, one agent-actionable line each; empty when it is clean.
std::vector<std::string> check_catalog(const Catalog& catalog);

// An entry's index in its table, or -1.
int find_achievement(const Catalog& catalog, std::string_view id);
int find_stat(const Catalog& catalog, std::string_view id);
int find_board(const Catalog& catalog, std::string_view id);
int find_dlc(const Catalog& catalog, AppId app);

}  // namespace ge::services
