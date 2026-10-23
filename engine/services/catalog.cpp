// The catalog's check and its lookups: ids by the store's rules, duplicates, an achievement's
// stat and threshold.
//
// ge:summary The catalog's check (ids, duplicates, stat thresholds) and its lookups
// ge:keywords services catalog check_catalog find_achievement find_stat find_board find_dlc
// ge:skill services
// ge:test tests/services_catalog_test.cpp
#include "services/catalog.hpp"

namespace ge::services {

namespace {

bool id_ok(std::string_view id, size_t longest) {
  if (id.empty() || id.size() > longest) return false;
  for (const char c : id) {
    const bool ok =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

template <typename T>
int find_id(std::span<const T> table, std::string_view id) {
  for (size_t i = 0; i < table.size(); i++)
    if (table[i].id == id) return static_cast<int>(i);
  return -1;
}

template <typename T>
void check_ids(std::span<const T> table, const char* what, size_t longest,
               std::vector<std::string>& out) {
  for (size_t i = 0; i < table.size(); i++) {
    const std::string id(table[i].id);
    if (!id_ok(table[i].id, longest))
      out.push_back(std::string("services: catalog ") + what + " '" + id +
                    "' is not a store id (ASCII letters, digits and '_', at most " +
                    std::to_string(longest) +
                    " characters); next: rename it in the game's catalog");
    for (size_t j = 0; j < i; j++)
      if (table[j].id == table[i].id)
        out.push_back(std::string("services: catalog ") + what + " '" + id +
                      "' is declared twice; next: remove one row from the game's catalog");
  }
}

}  // namespace

std::vector<std::string> check_catalog(const Catalog& c) {
  std::vector<std::string> out;
  if (c.app == 0)
    out.push_back(
        "services: catalog has no app id (0); next: set Catalog::app to the game's store "
        "number (any non-zero number for a game not on a store yet)");
  check_ids(c.achievements, "achievement", 64, out);
  check_ids(c.stats, "stat", 64, out);
  check_ids(c.boards, "board", 128, out);
  for (const StatDef& s : c.stats) {
    if (!(s.min <= s.initial && s.initial <= s.max))
      out.push_back("services: catalog stat '" + std::string(s.id) +
                    "' starts outside [min, max]; next: fix its initial, min or max");
    if (s.max_change < 0)
      out.push_back("services: catalog stat '" + std::string(s.id) +
                    "' has a negative max_change; next: set it to 0 (no limit) or above");
  }
  for (const AchievementDef& a : c.achievements) {
    if (a.stat.empty()) continue;
    const int s = find_stat(c, a.stat);
    if (s < 0) {
      out.push_back("services: catalog achievement '" + std::string(a.id) + "' names stat '" +
                    std::string(a.stat) +
                    "', which is not declared; next: add the stat or fix the name");
      continue;
    }
    const StatDef& st = c.stats[static_cast<size_t>(s)];
    if (!(st.min < a.threshold && a.threshold <= st.max))
      out.push_back(
          "services: catalog achievement '" + std::string(a.id) +
          "' has a threshold its stat can never reach or always meets; next: set it inside "
          "(min, max] of stat '" +
          std::string(a.stat) + "'");
  }
  for (size_t i = 0; i < c.dlc.size(); i++) {
    if (c.dlc[i].app == 0 || c.dlc[i].app == c.app)
      out.push_back("services: catalog dlc '" + std::string(c.dlc[i].id) +
                    "' has app id 0 or the game's own; next: set its own store number");
    for (size_t j = 0; j < i; j++)
      if (c.dlc[j].app == c.dlc[i].app)
        out.push_back("services: catalog dlc app " + std::to_string(c.dlc[i].app) +
                      " is declared twice; next: remove one row");
  }
  return out;
}

int find_achievement(const Catalog& c, std::string_view id) { return find_id(c.achievements, id); }
int find_stat(const Catalog& c, std::string_view id) { return find_id(c.stats, id); }
int find_board(const Catalog& c, std::string_view id) { return find_id(c.boards, id); }
int find_dlc(const Catalog& c, AppId app) {
  for (size_t i = 0; i < c.dlc.size(); i++)
    if (c.dlc[i].app == app) return static_cast<int>(i);
  return -1;
}

}  // namespace ge::services
