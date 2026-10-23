// THE SIMULATED STORE: a whole store held in memory (its players, their friends, licences,
// achievements, stats, cloud files and presence, the leaderboards and their blobs) and a
// `Backend` for any of its players, so a game's use of the platform services is tested and
// measured headless, with no store's client and no account. Every answer that would cross the
// network arrives after `SimConfig::latency_us` (plus a jitter off a seeded stream) on the
// clock the players' polls pass in; a player's connection can be cut and restored, and the next
// server call made to fail with any result. The store holds a game to its catalog the way a
// real one does (an unknown id, a stat out of range, a lowered increment-only stat, a jump past
// `max_change`, a cloud write over the quota are refused), so a mistake fails in a test rather
// than on a store's servers.
//
// What a player does in the store's client rather than in the game (pressing the screenshot
// key, accepting an invite, joining a friend from the friends list) is a call on the store.
// Every call either side makes is written to the journal, one line each, for a lab or a failing
// test to print.
//
// THE OFFLINE STORE is the same store with one player, no latency, saved to a directory after
// every change it commits: a build sold outside a store keeps its player's achievements, stats,
// best scores and cloud files there (`open_offline`).
//
// ge:summary The simulated store: players, friends, catalog-checked stats, boards, cloud; offline
// ge:keywords services sim simulated store fake mock backend virtual-clock latency jitter
// ge:keywords fail_next set_online offline-store open_offline save load journal friends invite
// ge:keywords accept_invite join_friend press_screenshot_key grant licence quota
// ge:skill services
// ge:test tests/services_sim_store_test.cpp tests/services_offline_test.cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "services/backend.hpp"
#include "services/catalog.hpp"

namespace ge::services {

struct SimConfig {
  int64_t latency_us = 50000;  // a server round trip
  int64_t jitter_us = 0;       // up to this much more, drawn per answer off `seed`
  uint32_t seed = 1;
  uint64_t cloud_quota_bytes = 100ull << 20;  // per player
  int64_t overlay_us = 2000000;               // how long an opened overlay stays open
  std::string save_dir;  // when set, the store is saved there after every committed change
};

class SimStore {
 public:
  // The catalog's tables must outlive the store.
  explicit SimStore(const Catalog& catalog, SimConfig config = {});
  ~SimStore();
  SimStore(const SimStore&) = delete;
  SimStore& operator=(const SimStore&) = delete;

  const Catalog& catalog() const;
  const SimConfig& config() const;

  // ---- the store's side: players and what they do in the store's client
  UserId add_user(std::string name, std::string language = "english", std::string country = "");
  void befriend(UserId a, UserId b);
  // A licence for the game or a DLC; `installed` puts its content on the player's machine.
  void grant(UserId user, AppId app, bool installed = true);
  void set_environment(UserId user, Environment environment);
  // Cut or restore a player's connection: connection_lost / connection_restored on their next
  // poll, and every server call answers `offline` while it is cut.
  void set_online(UserId user, bool online);
  // The player's next server call (store_stats, an upload, a download) answers `result`.
  void fail_next(UserId user, Result result);
  void press_screenshot_key(UserId user);
  // The player accepts `inviter`'s latest invite, or joins `friend_id` from the friends list
  // (its "connect" presence): a join_requested event on their next poll. False when there is
  // no invite, or the friend shows no "connect".
  bool accept_invite(UserId user, UserId inviter);
  bool join_friend(UserId user, UserId friend_id);

  // A backend for one player. Several may be open at once, one per player; each must not
  // outlive the store.
  std::unique_ptr<Backend> backend(UserId user);

  // What the store kept, for a test or a lab to read.
  std::vector<UserId> users() const;
  int screenshots(UserId user) const;  // filed by add_screenshot or the store's own capture
  const std::vector<std::string>& journal() const;

  // The whole store to and from a directory (a text file and the cloud files and blobs beside
  // it). False and `error` (an agent-actionable line) when it cannot.
  bool save(const std::string& dir, std::string* error) const;
  bool load(const std::string& dir, std::string* error);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// The offline store for one player, saved in `dir` (made when missing). Loads what an earlier
// run saved there, or starts the player `player_name` afresh. `store` is null and `error` set
// when the directory holds a store it cannot read.
struct Offline {
  std::unique_ptr<SimStore> store;
  std::unique_ptr<Backend> backend;
};
Offline open_offline(const Catalog& catalog, const std::string& dir, std::string player_name,
                     std::string* error);

}  // namespace ge::services
