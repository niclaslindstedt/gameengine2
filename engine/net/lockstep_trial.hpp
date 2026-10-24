// A LOCKSTEP TRIAL: a session of peers in a mesh over the simulated network, measured. Two games
// stand for the two kinds that play this way: a STRATEGY game (lockstep in turns: every seat
// moves sixteen units by orders its hand gives a few times a second) and a FIGHTING game
// (rollback, a step a turn: two seats whose pads change every few steps, each predicting the
// other). The trial answers what a link costs the session: how often a peer waits, how long an
// order takes to be felt, the bytes each peer sends, how often and how deep a rollback goes, and
// that every peer's state agrees. A desync can be PLANTED (one seat's state nudged on one step)
// to show it is caught on that step. A pure function of its config: the same trial, the same
// hash.
//
// ge:summary A lockstep trial: a strategy game in turns or a fighting game with rollback, measured
// ge:keywords net lockstep rollback trial strategy fighting orders latency stall desync plant
// ge:keywords bandwidth agree multiplayer measure
// ge:skill net
// ge:test tests/net_lockstep_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "net/conditions.hpp"
#include "net/lockstep.hpp"

namespace ge::net {

enum class LockstepGameKind : uint8_t { strategy, fighting };

struct LockstepTrialConfig {
  LockstepGameKind game = LockstepGameKind::strategy;
  int seats = 4;
  double seconds = 20;
  int tick_hz = 60;
  uint32_t seed = 1;
  LinkConditions link;
  // The mode follows the game (strategy: lockstep; fighting: rollback, turn_steps 1); the rest
  // (turn_steps, delay, max_rollback, send_every) is the caller's.
  LockstepConfig lockstep;
  int plant_step = -1;  // nudge plant_seat's state after this step (-1: no desync)
  int plant_seat = 1;
};

struct LockstepTrial {
  int connected = 0;               // seats whose mesh came up
  double pace = 0;                 // steps run per update, the slowest peer
  double stall_share = 0;          // updates that ran no step, mean over peers
  std::vector<double> latency_ms;  // each order or pad change, given to felt, on its own peer
  double up_kbps = 0;              // every packet a peer sent, headers too, mean
  double payload_kbps = 0;         // the lockstep messages a peer sent, mean
  uint64_t rollbacks = 0, resimulated = 0, mispredicted = 0;
  int deepest = 0;
  std::vector<double> depths;      // each rollback's steps run again
  bool agree = false;              // every peer's digest the same at the last step all ran
  uint32_t common = 0;             // that step
  bool caught = false;             // a desync seen by some peer
  uint32_t caught_step = 0;        // the step it names (the earliest any peer named)
  double caught_after_ms = 0;      // from that step being run to the first peer seeing it
  std::vector<double> t_s, steps;  // seat 0's steps run by each second
  uint64_t hash = 0;
};

LockstepTrial run_lockstep_trial(const LockstepTrialConfig& config);

}  // namespace ge::net
