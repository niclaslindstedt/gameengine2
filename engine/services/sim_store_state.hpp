// The simulated store's state, shared by its two halves: the calls (sim_store.cpp) and saving
// and loading it (sim_store_io.cpp). Not installed; nothing else includes it.
//
// ge:summary The simulated store's state: players, boards, blobs, the event queues, the journal
// ge:keywords services sim store state impl player board entry blob pending queue journal
// ge:skill services
// ge:test tests/services_sim_store_test.cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/prng.hpp"
#include "services/sim_store.hpp"

namespace ge::services {

struct SimPending {
  int64_t due_us = 0;
  uint64_t seq = 0;  // ties at one time go in the order they were queued
  Event event;
};

struct SimInvite {
  UserId from = 0;
  std::string connect;
};

struct SimPlayer {
  UserId id = 0;
  std::string name, language, country;
  bool online = true;
  Environment environment;
  std::vector<UserId> friends;
  std::map<AppId, bool> licences;     // app -> installed
  std::vector<double> stats;          // local, by catalog index
  std::vector<double> stats_stored;   // what the store took
  std::vector<bool> achieved;         // local
  std::vector<bool> achieved_stored;  // what the store took
  std::map<std::string, std::vector<uint8_t>> cloud;
  std::map<std::string, std::string> presence;
  std::vector<SimInvite> invites;  // received, latest last
  bool hooked = false;
  int screenshots = 0;
  Result fail_next = Result::ok;  // `ok`: no failure staged
  int64_t now_us = 0;             // the player's last poll
  std::vector<SimPending> queue;
  RequestId next_request = 1;
};

struct SimEntry {
  UserId user = 0;
  int32_t score = 0;
  uint64_t seq = 0;  // when it was set: the earlier of two equal scores ranks first
  std::vector<int32_t> details;
  BlobId blob = 0;
};

struct SimStore::Impl {
  Catalog catalog;
  SimConfig config;
  core::Rng rng;
  std::vector<SimPlayer> players;
  std::vector<std::vector<SimEntry>> boards;  // by catalog index, unsorted
  std::map<BlobId, std::vector<uint8_t>> blobs;
  uint64_t next_seq = 1;
  BlobId next_blob = 1;
  UserId next_user = 1001;
  uint64_t queued = 0;
  std::vector<std::string> journal;
  std::string save_error;  // the last failed save's line, "" after a good one

  SimPlayer* find(UserId id);
  const SimPlayer* find(UserId id) const;
  void fresh_progress(SimPlayer& p) const;  // every stat at its initial value, nothing achieved
  void save_if_asked();
};

// The store to and from a directory (sim_store_io.cpp).
bool save_store(const SimStore::Impl& s, const std::string& dir, std::string* error);
bool load_store(SimStore::Impl& s, const std::string& dir, std::string* error);

}  // namespace ge::services
