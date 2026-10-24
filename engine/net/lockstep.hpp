// REPLICATION, THE SAME STEP EVERYWHERE: every peer runs the whole world with the same
// deterministic step, and only the INPUTS cross the network. It is the model of a strategy game
// (thousands of units, a few bytes of orders a second) and, with rollback, of a fighting game or a
// two-player racer (docs/networking.md § Turns and lockstep). The engine's determinism (G1) is
// what makes it cheap: two peers that step the same inputs reach the same bits.
//
// SEATS are the players, 0..seats-1, each a peer of a mesh (every seat a node connected to every
// other). A seat's INPUT for a turn is whatever bytes the game gives it (orders, a pad's state),
// up to kMaxOrder; the game appends to the pending input any time with `lockstep_input`, and the
// pending input is closed as this seat's input for turn T + `delay` when this peer starts turn T.
// The turns below `delay` are empty for every seat, so a session starts without waiting.
//
// TWO MODES:
// - LOCKSTEP: a turn is `turn_steps` steps; a peer starts a turn only once it holds every seat's
//   input for it, so nothing is ever predicted and nothing is undone. A missing input STALLS the
//   peer (the step waits; `lockstep_waiting` names the seats it waits on). An order is felt
//   delay x turn_steps steps after it is given, plus any stall.
// - ROLLBACK (turn_steps 1): a peer steps on at once, PREDICTING a missing input as that seat's
//   newest one; when the real input arrives and differs from what was predicted, the game's
//   state is loaded from before that step and every step since is run again with the inputs now
//   known. The game keeps `max_rollback` + 1 saved states; a peer more than `max_rollback` steps
//   past the last step every seat's input is in for stalls. For few players: each rollback runs
//   up to max_rollback steps again in one frame.
//
// INPUTS go on an unreliable channel, every message carrying every input of this seat some other
// seat has not acked yet (each message also acks, per seat, how many turns of its inputs this
// peer holds), so a lost packet costs nothing. One message goes to every peer each
// `send_every` updates.
//
// DESYNCS: after each step whose inputs are all real (CONFIRMED: every step in lockstep), the
// game's digest of its state is kept and sent the same way, and every peer compares each seat's
// digests with its own, step by step. The first step two seats differ on is the desync, caught on
// the step it happened (`lockstep_desync`), a round trip after it was stepped.
//
// Like the node, nothing here reads a clock or draws randomness; the caller's updates are the
// clock, and a session over the simulated network replays to the bit.
//
//   lockstep_init(ls, config, game, node)
//   every update (the game's fixed step): node_update, lockstep_receive, lockstep_input (any
//   number of times), lockstep_update (runs the steps it may), node_flush
//
// ge:summary Lockstep and rollback: inputs-only turns, input delay, prediction, desync digests
// ge:keywords net lockstep rollback turns input-delay orders deterministic desync digest
// ge:keywords prediction resimulation rts strategy fighting peer-to-peer mesh seats stall
// ge:skill net
// ge:test tests/net_lockstep_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/node.hpp"

namespace ge::net {

constexpr int kMaxSeats = kMaxPeers;  // a mesh: every other seat is one of the node's peers
constexpr int kMaxOrder = 128;        // bytes one seat's input for a turn may carry

enum class LockstepMode : uint8_t { lockstep, rollback };

// The game's half. `step` and `digest` must be deterministic; every peer calls them alike.
struct LockstepGame {
  // One step: inputs[seat] / sizes[seat] are each seat's input for this step (size 0: none). In
  // lockstep a turn's inputs come on its first step; its other steps have none.
  void (*step)(void* ctx, uint32_t step, const uint8_t* const* inputs,
               const uint8_t* sizes) = nullptr;
  uint64_t (*digest)(void* ctx) = nullptr;  // the state's digest, after a step
  // Rollback only: keep the state as it is now in `slot` (0..max_rollback), or bring it back.
  void (*save)(void* ctx, int slot) = nullptr;
  void (*load)(void* ctx, int slot) = nullptr;
  void* ctx = nullptr;
};

struct LockstepConfig {
  LockstepMode mode = LockstepMode::lockstep;
  int seats = 2;         // 1..kMaxSeats
  int seat = 0;          // this peer's
  int channel = 0;       // an unreliable channel of the node's
  int turn_steps = 6;    // steps a turn (lockstep); rollback steps a turn at a time
  int delay = 2;         // turns between an input taken and the turn it is stepped on
  int max_rollback = 8;  // rollback: steps run ahead of the last confirmed one before stalling
  int send_every = 1;    // updates between messages
  int window = 0;        // turns of inputs kept a seat (0: what delay and max_rollback need)
};

struct LockstepStats {
  uint64_t updates = 0;              // lockstep_update calls
  uint64_t steps = 0;                // steps run forward (not counting resimulation)
  uint64_t stalls = 0;               // updates that ran no step
  uint64_t rollbacks = 0;            // loads of an earlier state
  uint64_t resimulated = 0;          // steps run again after a rollback
  int deepest = 0;                   // the most steps one rollback ran again
  uint64_t mispredicted = 0;         // real inputs that differed from the one predicted
  uint64_t messages = 0, bytes = 0;  // sent
  uint64_t received = 0, stale = 0;  // messages read, and inputs in them already held
  uint64_t bad = 0;                  // messages that did not parse, or named a seat out of range
};

struct LockstepDesync {
  bool found = false;
  uint32_t step = 0;  // the first step two seats' states differ after
  int seat = -1;      // the seat whose digest differs from this peer's
  uint64_t mine = 0, theirs = 0;
  uint32_t caught_at = 0;  // the step this peer had reached when it saw it
};

// One seat's input for one turn.
struct TurnInput {
  uint8_t size = 0;
  bool real = false;  // rollback: held, not predicted
  uint8_t bytes[kMaxOrder] = {};
};

struct LockstepSeat {
  int peer = -1;                  // the node's peer it speaks through (-1: not heard from yet)
  bool lost = false;              // its peer disconnected
  uint32_t have = 0;              // turns of its inputs held, in order (the next one missing)
  uint32_t acked = 0;             // turns of this peer's inputs it holds, by its last message
  uint32_t have_digests = 0;      // steps of its digests held, in order
  uint32_t acked_digests = 0;     // steps of this peer's digests it holds
  uint32_t checked = 0;           // steps of its digests compared with this peer's
  std::vector<TurnInput> inputs;  // a ring of `window` turns
  std::vector<uint64_t> digests;  // a ring of `history` steps
  TurnInput newest;               // rollback: its newest real input (what is predicted)
  uint32_t newest_turn = 0;
  bool has_newest = false;
};

struct Lockstep {
  LockstepConfig config;
  LockstepGame game;
  Node* node = nullptr;
  int window = 0, history = 0;
  uint32_t step = 0;         // the next step to run
  uint32_t confirmed = 0;    // rollback: the first step not every seat's input is in for
  uint32_t final = 0;        // steps whose digests are final (kept in this peer's own ring)
  uint32_t closed = 0;       // turns of this seat's own inputs closed
  int64_t rollback_to = -1;  // rollback: the earliest step a late input changed
  TurnInput pending;
  std::vector<LockstepSeat> seats;
  std::vector<std::vector<TurnInput>> used;  // rollback: the inputs each step was last run with
  LockstepDesync desync;
  LockstepStats stats;
  std::vector<uint8_t> scratch;
};

// Size everything for `config` on a node whose channel `config.channel` is unreliable; false
// (and the reason in `why`) when the config cannot work.
bool lockstep_init(Lockstep& ls, const LockstepConfig& config, const LockstepGame& game, Node& node,
                   const char** why = nullptr);
// Read this update's node events: other seats' inputs, acks and digests; a seat's peer lost.
void lockstep_receive(Lockstep& ls);
// Append to this seat's pending input (an order); false when it would pass kMaxOrder. In rollback
// mode `lockstep_set_input` replaces it, as a pad's state does.
bool lockstep_input(Lockstep& ls, const uint8_t* bytes, size_t size);
void lockstep_set_input(Lockstep& ls, const uint8_t* bytes, size_t size);
// Run the steps this peer may (none when it stalls; a rollback's again first), check digests,
// and send this seat's message when one is due. Returns the steps run forward.
int lockstep_update(Lockstep& ls);
// The seats this peer is waiting on for its next step, a bit each (0: not waiting).
uint32_t lockstep_waiting(const Lockstep& ls);
inline uint32_t lockstep_turn(const Lockstep& ls) {
  return ls.step / static_cast<uint32_t>(ls.config.turn_steps);
}

}  // namespace ge::net
