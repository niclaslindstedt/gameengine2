// REPLICATION, THE SERVER'S WORD: the server steps the world and sends each client SNAPSHOTS of
// what it may see; each client predicts its own entity from its own inputs and draws everyone
// else a little in the past, interpolated between snapshots. It is the model of a fast action
// game, a survival sandbox, an open world, and Fall Line's free ride (docs/networking.md).
//
// THE WORLD is entities, each a row of up to kMaxFields numbers the game names in a `Schema`:
// each field quantized to `bits` over [min, max] (an angle wraps), so a snapshot carries bits,
// not doubles. The server keeps every entity's state QUANTIZED after each tick, so a client that
// replays the same inputs from a snapshot reaches the server's own doubles: with no loss, a
// prediction never needs correcting.
//
// SNAPSHOTS go on an unreliable channel every `send_every` ticks, each a DELTA against the newest
// snapshot the client has acked (each field that changed, bit-packed), or whole when that one has
// fallen out of the ring. Each client is sent only the entities INTEREST lets it see (its own
// always; others within `interest_radius` of it on two fields, and as `visible` allows), the
// stalest first when they do not all fit in `snapshot_budget` (a row's priority is how many ticks
// the client's copy of it is behind). A client acks the newest snapshot it decoded on every input.
//
// INPUTS go up an unreliable channel every tick, each carrying the last `redundancy` inputs so a
// lost packet costs nothing; the server applies one a tick in order through the game's `step`
// (the same function the client predicts with) and says which it applied last. A tick with no
// input waiting steps the last one again (the queue then holds one more: a buffer for the link's
// jitter); inputs lost past the redundancy are taken to be the one before them, less those the
// repeats already stood in for, so the server steps a client's entity as often as the client did.
// The client PREDICTS its entity by replaying every input the server has not yet applied over the
// newest snapshot; a correction is shown smoothly (SMOOTHING, M17o): the error drawn out through a
// critically damped spring that carries its own velocity, so the drawn path bends instead of
// kinking, and snapped at once past `snap_distance` (a teleport, a respawn).
//
// OTHERS are drawn a little behind the server (TIME SYNC, M17n), on a cubic curve through the
// snapshots around that moment, its slopes from the snapshots either side (M17o); when the
// snapshots run late they are carried on at their last velocity for up to `extrapolate_s`, then
// held, and blended back onto the curve over `extrapolate_blend_s` once one arrives.
//
// THE TICK RATE is the game's (`tick_hz`: 60, 72, 120 or any other), and every duration below is
// in seconds, turned into ticks at that rate, so the same settings behave the same at any rate.
//
// THE PREDICTED WORLD (M17p): others near the own entity replayed beside it by their last inputs,
// contacts and events predicted and judged (`outcomes`), within a budget and a horizon.
//
// LAG COMPENSATION: each input names the tick the client was drawing, and `replicate_rewind`
// returns any entity's state at that tick from the server's history, so a hit is judged against
// what the player saw.
//
// Like the node, nothing here reads a clock or draws randomness; every call takes its tick, and
// the whole exchange over the simulated network replays to the bit.
//
//   server: replicate_server_init(s, schema, config, node)   spawn, set, despawn entities
//           every tick: replicate_server_receive (after node_update), the game's own moves,
//                       replicate_server_tick (steps the owned entities, records, sends)
//   client: replicate_client_init(c, schema, config, node, server_peer)
//           every tick: replicate_client_receive (after node_update), replicate_client_input
//           drawing: replicate_client_predicted, replicate_client_sample
//
// ge:summary Replication: snapshots and deltas, interest, prediction, interpolation, rewind
// ge:keywords net replicate replication snapshot delta quantize interest prediction reconcile
// ge:keywords interpolation lag-compensation rewind server-authoritative input redundancy
// ge:keywords priority bandwidth entity schema predicted-world contact-events horizon
// ge:skill net
// ge:test tests/net_replicate_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/node.hpp"

namespace ge::net {

constexpr int kMaxFields = 16;
constexpr uint16_t kNoEntity = 0xffff;
constexpr int kMaxInputSize = 32;
constexpr int kNoSlack = 1 << 20;  // no input applied in the window yet

struct FieldSpec {
  double min = 0, max = 1;
  uint8_t bits = 16;        // 1..32
  bool angle = false;       // wraps over [min, max): interpolated the short way round
  bool extrapolate = true;  // drawn ahead of the newest snapshot when late (false: a count, a flag)
};

// The game's step for an owned entity: its state (the schema's fields) moved by one tick's input.
// The server and the predicting client call the same one, so it must be deterministic.
using StepFn = void (*)(void* ctx, double* state, const uint8_t* input);
// Whether `client` (a peer slot) may see entity `index`; null: interest_radius alone decides.
using VisibleFn = bool (*)(void* ctx, int client, int index);
// A contact between entity `a` (moved by inputs) and `b`, resolved in place (a push, a stop);
// true when they touched. The server and the predicting client call the same one.
using InteractFn = bool (*)(void* ctx, double* a, double* b);

struct Schema {
  int fields = 0;
  FieldSpec spec[kMaxFields];
  int input_size = 0;  // bytes an input carries, at most kMaxInputSize
  StepFn step = nullptr;
  InteractFn interact = nullptr;  // null: entities pass through each other
  void* ctx = nullptr;
};

constexpr uint8_t kContactEvent = 0;  // an event kind: a contact began; its payload the other's id
constexpr int kMaxEventPayload = 8;

// confirmed: predicted and the server's agreed; undone: predicted and never came; unpredicted: the
// server's, about an entity the client was predicting, and missed; heard: the server's, about one
// it was not (out of reach or past the horizon), so shown as it arrives with nothing to take back.
enum class EventOutcome : uint8_t { confirmed, undone, unpredicted, heard };

struct EventResult {
  uint8_t kind = 0;
  EventOutcome outcome = EventOutcome::confirmed;
  uint32_t seq = 0;  // the input that caused it
  uint8_t size = 0;
  uint8_t payload[kMaxEventPayload] = {};
};

struct ReplicateConfig {
  int snapshot_channel = 0;  // unreliable
  int input_channel = 1;     // unreliable
  int max_entities = 128;
  int send_every = 2;  // a snapshot every this many ticks
  int ring = 32;       // snapshots remembered per client, and ticks of history for rewind
  int redundancy = 4;  // inputs repeated in every input message
  int interest_x = -1, interest_z = -1;  // the fields interest measures distance on (-1: none)
  double interest_radius = 0;            // 0: everyone sees everything
  VisibleFn visible = nullptr;
  void* visible_ctx = nullptr;
  int interp_ticks = 6;  // how far behind the newest snapshot others are drawn
  // SMOOTHING (M17o). spring: a correction's error drawn out through a critically damped spring
  // whose error halves in about `spring_halflife_s`, its velocity carried across corrections;
  // off: the error scaled by `smoothing` each tick (a kink in the drawn velocity at every one).
  bool spring = true;
  double spring_halflife_s = 1.0 / 15;
  double smoothing = 0.85;     // without the spring: a correction's error left after each tick
  double snap_distance = 0;    // a correction this large on any field is shown at once (0: never)
  bool cubic = true;           // others on a cubic curve (false: straight between snapshots)
  double extrapolate_s = 0.2;  // the most others are carried past the newest snapshot (0: held)
  double extrapolate_blend_s = 0.13;  // a late snapshot is blended in over this long
  // THE PREDICTED WORLD (M17p): entities within this distance of the own one (on interest_x and
  // interest_z) are predicted with it, kept until 1.5 times as far (0: none; needs interact).
  double predict_radius = 0;
  int max_predicted = 8;
  int resim_budget = 0;        // entity-steps a replay may take (0: no cap)
  int event_channel = -1;      // reliable: the server's events to their causes (-1: none)
  bool contact_events = true;  // a contact beginning is an event (kContactEvent)
  double event_wait_s = 0.3;   // after its input is applied, how long a predicted one waits
  double event_slop_s = 0.1;   // a server event matches a predicted one this near in inputs
  // Others are predicted only while the replay is this short: a player holds an input for about
  // a reaction time, so further ahead their last one guesses worse than the curve draws (the
  // joust: wifi's corrections a fifth of interpolated's at 0.1 s, mobile's worse at 0.15 s; 0: no
  // limit).
  double predict_max_s = 0.1;
  size_t snapshot_budget = 0;  // bytes a snapshot may take (0: the largest message)
  // THE RATES, the client's to ask and the server's to cap (a game exposes them as settings).
  // A client's ask rides on its inputs; the server sends it a snapshot every
  // max(send_every, tick_hz / update_rate) ticks, each within rate / its snapshots a second.
  int tick_hz = 60;         // the step's rate, which turns the rates and durations into ticks
  int update_rate = 0;      // client: snapshots a second it asks for (0: every send_every)
  int cmd_rate = 0;         // client: input messages a second it sends (0: one a tick)
  uint32_t rate = 0;        // client: snapshot bytes a second it takes (0: no cap)
  int max_update_rate = 0;  // server: the most snapshots a second anyone is sent (0: no cap)
  uint32_t max_rate = 0;    // server: the most snapshot bytes a second anyone is sent (0: none)
  // TIME SYNC (M17n). The server tells each client its SLACK: the fewest of its inputs left waiting
  // behind the one applied, over the last `slack_window_s` (-1: a tick went without). The
  // client runs its own tick `replicate_client_dilation` times as fast as it otherwise would,
  // within 1 +- `dilate_max`, to hold the slack at `input_slack`: ahead of the server by its round
  // trip and just enough for the link's jitter, never stalling and never queuing more. Its
  // interpolation delay is what the last `interp_window` snapshots' arrivals needed (how far the
  // reckoned server tick had passed the newest snapshot when the next one came), at their
  // `interp_quantile`, instead of a fixed `interp_ticks`; it eases at most `interp_ease` a tick.
  // Off (`sync` false): a fixed `interp_ticks`, and the client's clock is the caller's alone.
  bool sync = true;
  int input_slack = 0;  // inputs to hold in reserve at the worst moment of a window
  double slack_window_s = 0.5;
  double starve_hold_s = 5;    // a window that ran short raises the target slack by one this long
  double dilate_max = 0.03;    // the most the client's clock is sped up or slowed
  double dilate_gain = 0.01;   // the speed change a tick of slack off the target asks for
  int interp_window = 64;      // arrivals
  double interp_quantile = 1;  // the worst of the window
  double interp_ease = 0.05;   // ticks the delay may grow a tick (it shrinks a quarter as fast)
};

struct ReplicateStats {
  uint64_t snapshots = 0, full = 0, bytes = 0;  // sent (server) or decoded (client)
  uint64_t dropped = 0;               // client: snapshots whose baseline was gone, or stale
  uint64_t starved = 0;               // server: entities left out of a snapshot for its budget
  uint64_t inputs = 0, repeated = 0;  // server: inputs applied, ticks that reused the last one
  uint64_t trimmed = 0;               // server: inputs dropped for a queue past its most
  uint64_t corrections = 0;           // client: snapshots that moved its prediction
  uint64_t snaps = 0;                 // client: corrections past snap_distance, shown at once
  uint64_t contacts = 0;              // server: contacts begun; client: predicted contacts begun
  uint64_t events = 0;                // server: events sent
  uint64_t confirmed = 0, undone = 0, unpredicted = 0;     // client: predicted events' outcomes
  uint64_t heard = 0;                                      // client: events it could not predict
  uint64_t resim_steps = 0, resims = 0, resim_capped = 0;  // client: replays' entity-steps
  uint64_t resim_max = 0;                                  // client: the costliest replay
  uint64_t predicted_corrections = 0;                      // client: predicted others moved
};

// One entity's quantized row.
struct Row {
  uint16_t id = kNoEntity;
  uint32_t fresh = 0;  // server: the tick this value was current (a starved row keeps its own)
  uint32_t q[kMaxFields] = {};
};

// A snapshot as a client was sent it (server) or decoded it (client): its tick and its rows.
struct Snap {
  uint32_t tick = 0;
  bool valid = false;
  uint32_t late_x256 =
      0;                  // client: the tick it was drawing when this arrived past the newest, or 0
  std::vector<Row> rows;  // by id, ascending
};

struct ReplicateClientSlot {
  bool active = false;
  uint16_t entity = kNoEntity;  // the entity its inputs move
  std::vector<Snap> sent;       // the ring of what it was sent
  uint32_t acked = 0;           // the newest snapshot it decoded (0: none yet)
  bool has_ack = false;
  std::vector<uint8_t> inputs;  // queued inputs, input_size each, oldest first
  std::vector<uint32_t>
      queue_seq;          // their numbers, ascending (a gap: inputs the repeats stood in for)
  uint32_t next_seq = 1;  // the next input it is owed while the queue is empty
  uint32_t applied = 0;   // the last input applied
  uint32_t repeats = 0;   // ticks that stepped the last input again, not yet set off
  uint8_t last[kMaxInputSize] = {};
  uint32_t view_tick = 0;    // the tick it said it was drawing (x256)
  uint32_t rate = 0;         // the bytes a second it asked for (0: no cap)
  int update_rate = 0;       // the snapshots a second it asked for (0: the server's)
  uint32_t last_sent = 0;    // the tick of the last snapshot sent it
  int slack_now = kNoSlack;  // TIME SYNC: the fewest waiting over this window, and the last
  int slack_before = kNoSlack;
  int slack_ticks = 0;
  std::vector<double> priority;   // per entity index: its place in the last snapshot's order
  std::vector<uint8_t> touching;  // per entity index: in contact with its entity last tick
};

struct ReplicateServer {
  Schema schema;
  ReplicateConfig config;
  Node* node = nullptr;
  uint32_t tick = 0;
  int slack_window = 30;  // slack_window_s in ticks at tick_hz
  uint16_t next_id = 0;
  std::vector<uint16_t> ids;      // per index: the entity's id, kNoEntity when free
  std::vector<int> owner;         // per index: the peer whose inputs move it, -1: the game
  std::vector<int> order;         // scratch: a snapshot's entities by priority
  std::vector<double> state;      // per index: fields doubles (quantized values)
  std::vector<uint32_t> history;  // ring x max_entities x fields, quantized
  std::vector<uint32_t> history_tick;
  std::vector<uint8_t> history_alive;        // ring x max_entities
  std::vector<ReplicateClientSlot> clients;  // per peer slot
  ReplicateStats stats;
};

void replicate_server_init(ReplicateServer& s, const Schema& schema, const ReplicateConfig& config,
                           Node& node);
// A new entity, moved by `owner`'s inputs (a peer slot) or by the game (-1); its id, or
// kNoEntity when the table is full.
uint16_t replicate_spawn(ReplicateServer& s, int owner, const double* values);
void replicate_set(ReplicateServer& s, uint16_t id, const double* values);
const double* replicate_get(const ReplicateServer& s, uint16_t id);
void replicate_despawn(ReplicateServer& s, uint16_t id);
// The client's slack as the server reports it (TIME SYNC).
int replicate_slack(const ReplicateClientSlot& c);
// The ticks between snapshots and the bytes each may take, for a client's asked rates.
int replicate_send_interval(const ReplicateServer& s, int client);
size_t replicate_snapshot_budget(const ReplicateServer& s, int client);
// Read the clients' inputs and acks out of the node's events; a peer gone frees its slot.
void replicate_server_receive(ReplicateServer& s);
// Step every owned entity by its client's next input, record the tick, send what is due.
void replicate_server_tick(ReplicateServer& s);
// An event the game makes on the server, caused by `client`'s input (the one applied this tick):
// sent to that client on the event channel to confirm what it predicted.
void replicate_server_event(ReplicateServer& s, int client, uint8_t kind, const uint8_t* payload,
                            int size);
// Entity `id`'s state at `tick` (fractional, x256) from the history; false when out of the ring.
bool replicate_rewind(const ReplicateServer& s, uint16_t id, uint32_t tick_x256, double* out);

// An entity predicted beside the own one (THE PREDICTED WORLD), or fading back to the curve.
struct Predicted {
  uint16_t id = kNoEntity;
  bool active = false;   // predicted now (false: fading, drawn on the curve plus `error`)
  bool stepped = false;  // another player's, stepped by its last input
  bool touching = false;
  uint8_t input[kMaxInputSize] = {};
  double state[kMaxFields] = {};
  double error[kMaxFields] = {}, error_v[kMaxFields] = {};
  double prior[kMaxFields] = {};  // the prediction before the last snapshot (for corrections)
  bool continuing = false;        // predicted before the last snapshot too
  double dist2 = 0;               // from the own entity, at the last reconcile
};

// An event the client predicted, waiting for the server's word.
struct PredictedEvent {
  EventResult e;
  uint32_t due = 0;  // the input count when the server had applied its input (0: not yet)
};

struct ReplicateClient {
  Schema schema;
  ReplicateConfig config;
  Node* node = nullptr;
  int server = -1;
  std::vector<Snap> ring;  // decoded snapshots, by tick
  uint32_t newest = 0;     // the newest snapshot decoded
  bool has_snapshot = false;
  uint16_t own = kNoEntity;
  uint32_t applied = 0;          // the server's last applied input, as of the newest snapshot
  uint32_t seq = 0;              // the last input made
  std::vector<uint8_t> history;  // inputs by seq, ring x input_size
  double predicted[kMaxFields] = {};
  double error[kMaxFields] = {};          // a correction still being smoothed away
  double error_v[kMaxFields] = {};        // its velocity (the spring), a tick
  double spring_w = 0, spring_decay = 1;  // the spring's rate and its decay a tick
  int starve_hold = 300;                  // the durations in ticks at tick_hz
  double extrapolate_ticks = 12, blend_ticks = 8;
  double last_error = 0;    // the size of the last correction (the schema's units)
  uint32_t local_tick = 0;  // ticks since the newest snapshot arrived
  int unsent = 0;           // inputs made since the last input message (cmd_rate)
  bool predicting = false;  // `predicted` holds a reconciled state to step on from
  // TIME SYNC: the server's last word on this client's slack, the speed it asks of the clock,
  // and the server's tick as the client reckons it (`server_tick`, advanced a tick of the
  // server's each tick of its own and pulled toward each snapshot as it arrives); the snapshots'
  // spacing and their arrivals' mean deviation from the reckoning (ticks); the interpolation
  // delay and the tick drawn (x256, never going back).
  int slack = 0;
  uint32_t starved_at = 0;  // the input number when the server last reported running short + 1
  double dilation = 1;
  double server_tick = 0;
  double spacing = 0, jitter = 0, delay = 0;
  std::vector<double> needs;  // the last interp_window arrivals' needed delay, a ring (ticks)
  uint32_t need_count = 0;
  std::vector<double> sorted;  // scratch for the quantile
  double want = 0;             // the delay the needs ask for
  uint32_t render_x256 = 0;
  uint32_t spacing_from = 0;  // the tick of the snapshot the spacing is measured from
  bool clocked = false;
  std::vector<Row> scratch;             // a snapshot being decoded
  std::vector<Predicted> world;         // THE PREDICTED WORLD: max_predicted slots
  std::vector<uint16_t> others_id;      // the newest snapshot's trailer: players' last inputs
  std::vector<uint8_t> others_input;    // input_size each
  std::vector<PredictedEvent> pending;  // predicted events awaiting the server's word
  std::vector<EventResult> outcomes;    // this tick's outcomes: the caller reads, then clears
  int event_wait = 18;                  // event_wait_s in inputs
  int event_slop = 6;                   // event_slop_s in inputs
  int predict_max = 7;                  // predict_max_s in inputs (0: no limit)
  bool beyond = false;                  // past the horizon: the others are drawn on the curve
  double last_world_error = 0;          // the largest move of a predicted other, last snapshot
  ReplicateStats stats;
};

void replicate_client_init(ReplicateClient& c, const Schema& schema, const ReplicateConfig& config,
                           Node& node, int server_peer);
// Decode the snapshots in the node's events and reconcile the prediction with the newest.
void replicate_client_receive(ReplicateClient& c);
// This tick's input: predicted at once and sent with the last `redundancy` (call each tick);
// ignored until the first snapshot has arrived.
void replicate_client_input(ReplicateClient& c, const uint8_t* input);
// The own entity as drawn: the prediction plus what is left of the last correction.
void replicate_client_predicted(const ReplicateClient& c, double* out);
// How fast the caller should run this client's tick (TIME SYNC): 1 +- dilate_max, 1 with sync off.
inline double replicate_client_dilation(const ReplicateClient& c) { return c.dilation; }
// The tick others are drawn at, x256: with sync, `delay` behind the reckoned server tick; without,
// interp_ticks behind the newest heard of.
uint32_t replicate_client_render_tick(const ReplicateClient& c);
// An event the game predicts on the client, caused by the input just made (call after
// replicate_client_input); its outcome comes back in `outcomes`.
void replicate_client_predict_event(ReplicateClient& c, uint8_t kind, const uint8_t* payload,
                                    int size);
// Whether entity `id` is predicted beside the own one now.
bool replicate_client_predicts(const ReplicateClient& c, uint16_t id);
// Entity `id` as drawn: predicted (THE PREDICTED WORLD) or at the render tick on the curve through
// the snapshots, carried on (capped) past the newest and blended back; false when it is in no
// snapshot around it.
bool replicate_client_sample(const ReplicateClient& c, uint16_t id, double* out);

// The schema's quantization, exposed for tests and tools.
uint32_t quantize(const FieldSpec& f, double v);
double dequantize(const FieldSpec& f, uint32_t q);

}  // namespace ge::net
