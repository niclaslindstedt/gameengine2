---
name: multiplayer
description: "Use when this game talks over a network: the engine's ge::net (a node for a host, a client or a peer of up to 32, its channels, its events), a lobby (players by seat, teams, chat, kick, start, reconnect by token, LAN discovery), reaching a host behind a home router (STUN, a rendezvous server, hole punching, the relay, opening a port), replicating a world from a server (snapshots, prediction, interpolation, interest, lag compensation), lockstep and rollback (inputs only, turns, input delay, desync digests), testing a session on the simulated network under bad links and pushing it until it breaks (ge net stress: ramps, chaos, soak, fuzz), capturing a live node's traffic, and the ge net tools that read it (ge net inspect, ge net stats, ge net sim)."
---

# Multiplayer on ge::net

The engine's network is `ge::net` (link `ge::net`; headers `net/*.hpp`). Its own UDP protocol
runs on the caller's clock and a seeded stream, so a session on the simulated network is
deterministic: the same seed and the same sends give the same packets, losses and arrivals.
Real sockets (IPv4 and IPv6, on every desktop and mobile platform) and the simulated network
share one `Transport`, so code tested on the second runs unchanged on the first.

## A node, once a frame

```cpp
#include "net/node.hpp"
#include "net/packet.hpp"
namespace net = ge::net;

net::NodeConfig cfg;
cfg.protocol = net::protocol_id("my-game", 3);   // builds that cannot play together never talk
cfg.max_peers = 8;                               // the game's maximum, 1..32
cfg.accept = true;                               // a host; a client leaves it false
cfg.channels.count = 3;
cfg.channels.delivery[0] = net::Delivery::sequenced;   // inputs: the newest wins
cfg.channels.delivery[1] = net::Delivery::unreliable;  // snapshots
cfg.channels.delivery[2] = net::Delivery::reliable;    // events and chat: once, in order
cfg.channels.largest[2] = 64 * 1024;                   // and messages up to 64 KB, in pieces
cfg.channels.priority[0] = 2;                          // inputs first when room is short
cfg.channels.budget_kbps = 256;                        // each connection's most, kbit/s (0: none)
cfg.seed = net::os_entropy();                    // a test passes a fixed seed instead

net::Transport t;
std::string err;
net::Address bind;
net::parse_address("0.0.0.0:27500", bind);
if (!net::open_udp_transport(t, bind, &err)) { /* say err: the port, the next action */ }
net::Node node;
net::node_init(node, cfg, t);

// each frame, now_us from the game's own clock:
net::node_update(node, now_us);
for (const net::Event& e : node.events) {
  // connected, disconnected (e.reason), message (net::event_data(node, e), e.size), connect_failed
}
net::node_send(node, peer, 2, bytes, size);   // or node_broadcast
net::node_flush(node, now_us);
```

A client calls `node_connect(node, host_address, now_us)` and waits for `connected` or
`connect_failed` (full, closed, timed out). A message is at most `net::kMaxMessage` bytes, or
the channel's `largest` on a reliable channel (up to 64 KB, sent in pieces and delivered whole).
A reliable channel refuses a message when its pieces do not fit beside the ones still
unacknowledged (128 at most), so check `node_send`'s answer and try again next frame.

**TCP.** For a stream instead of datagrams (a turn-based game, a network that blocks UDP), open
the transport with `net::open_tcp_host_transport(t, bind, &err)` on the host and
`net::open_tcp_client_transport(t, host_address, &err)` on a client; the node code is the same.

## A lobby: sessions

`net/session.hpp` runs a lobby on one reliable channel of the node's (give it its own; the game
skips that channel when it reads the node's events). The host's process is the authority; a
client asks and mirrors.

```cpp
#include "net/session.hpp"
net::SessionConfig sc;
sc.channel = 2;              // reliable, the session's alone
sc.max_players = 8;          // seats, 1..32
sc.team_count = 2;           // 0: no teams
sc.host_plays = false;       // true: a listen server, the host is seat 0 and leads
sc.seed = net::os_entropy(); // the tokens
net::SessionHost host;
net::session_host_init(host, sc, node, now_us);
// each frame, after node_update:
net::session_host_update(host, now_us);
for (const net::SessionEvent& e : host.events) { /* joined, left (e.why), chat, ... */ }

net::SessionClient client;                       // on a client's node
net::session_client_init(client, node, 2);
net::session_client_join(client, host_address, "name", saved_token, now_us);  // 0: new
// each frame, after node_update:
net::session_client_update(client, now_us);
// client.players[] is the player list (name, team, ready, leader, ping_ms); client.me is
// this player's seat; client.token brings it back to the seat after a crash or a drop.
net::session_client_chat(client, net::ChatScope::team, net::kNoPlayer, text, size);
// Proximity chat: the host asks the game who is in earshot (it knows where everyone is).
// sc.hears = [](void* game, int speaker, int listener) { return in_earshot(game, speaker, listener); };
net::session_client_chat(client, net::ChatScope::nearby, net::kNoPlayer, text, size);
```

Asks (`session_client_ready`, `_team`, `_chat`, `_kick`, `_start`, `_return`, `_settings`) come
back as events; a `refused` event names the ask and why (`why_name`). The game's own lobby
settings (map, mode) are its bytes in `session_client_settings`, kept and sent by the host.
`net/discovery.hpp` finds hosts on the LAN: the host answers probes on a fixed port
(`lan_answer` with `advert_of(host, ...)`), a client broadcasts `lan_probe` and reads
`lan_collect`. Run a session's script on the simulated network in a test, as the engine's
`net-session` lab does, and keep a hash of every end's events.

## Reaching a host behind a router

Players at home sit behind NATs. Run a `net::Rendezvous` on a public machine (its own UDP
transport; `rendezvous_update` every tick), set `NodeConfig::keep_foreign` on both nodes, and
drive `net/reach.hpp` after each `node_update`:

```cpp
net::ReachConfig rc;
rc.rendezvous = server_address;
rc.session = session_id;                       // the game's id for this host's session
net::ReachHost rh;  net::reach_host_init(rh, rc, host_node);      // then reach_host_update
net::ReachClient c; net::reach_client_init(c, rc, client_node);
net::reach_client_start(c, now_us);                                // then reach_client_update
// c.state: connected (c.path direct or relay, c.peer the host) or failed (c.why)
```

The client connects straight through a punched hole where the two NATs allow one and through
the server's relay where they do not; either way it is an ordinary peer of the node after.
`stun_request` / `stun_read_answer` ask any STUN server for the public address. Test it with
`sim_add_nat` (full cone, restricted, port-restricted, symmetric) on the simulated network.

A host whose router will open a port for it is reached straight by everyone. Ask with
`net/portmap.hpp` on a second socket (PCP, then NAT-PMP, then UPnP IGD), and register the port
it gets with the rendezvous:

```cpp
net::PortmapConfig pc;
pc.gateway = router_address;          // the LAN's gateway, port 5351 (PCP and NAT-PMP)
pc.local = this_machine;              // as the router sees it
pc.internal_port = game_port;         // the node's socket
net::Portmap pm; net::portmap_init(pm, pc, second_transport);
net::portmap_start(pm, now_us);                                    // then portmap_update
if (pm.state == net::PortmapState::mapped) net::reach_host_forward(rh, pm.external.port, now_us);
// done: portmap_close(pm, now_us), updated until pm.state is closed
```

It renews the lease itself. Test it against `SimRouter` (`net/sim_router.hpp`): give the NAT a
`gateway` node and choose what the router speaks.

## Replicating a world: the server's word

`net/replicate.hpp` sends each client snapshots of what it may see and lets it predict its own
entity. Name the fields and the step; the server and the client call the same step:

```cpp
net::Schema sc;
sc.fields = 3;
sc.spec[0] = {-256, 256, 20, false};   // x: 20 bits over 512 m
sc.spec[1] = {-256, 256, 20, false};   // z
sc.spec[2] = {0, 6.283185307179586, 12, true};  // heading: wraps
sc.input_size = 3;                     // bytes a tick's input carries
sc.step = my_step;                     // deterministic: void(void* ctx, double* state, const uint8_t* input)
net::ReplicateConfig rc;               // channel 0 snapshots, 1 inputs, both unreliable
// server, each tick after node_update:
net::replicate_server_receive(server); /* the game's own moves: replicate_set */ net::replicate_server_tick(server);
// client, each tick after node_update:
net::replicate_client_receive(client); net::replicate_client_input(client, input);
net::replicate_client_predicted(client, own);              // the own entity, smoothed
net::replicate_client_sample(client, id, other);           // anyone else, interpolated
```

Run the client's fixed step `net::replicate_client_dilation(client)` times as fast as the frame
clock says (multiply the frame's seconds by it before they feed the accumulator, engine/loop's
run clock). That is TIME SYNC (on by default, `rc.sync`): the server reports how many of the
client's inputs wait in its queue, and the client speeds up or slows down by at most 3% to keep
none waiting but none late, whatever its clock's drift. The others are then drawn behind the
server by what the link's jitter needs, not a fixed six ticks. SMOOTHING (on by default,
`rc.spring`, `rc.cubic`, `rc.extrapolate_s`) draws a correction out through a spring that keeps
the velocity, draws the others on curves, and carries them on for up to 0.2 s when a snapshot is
late. Set `rc.tick_hz` to the game's own step rate (72 for a shooter, 120 for a racer): every other
duration in the config is in seconds and follows it. Mark a field that must never be guessed (a
count, a flag) `extrapolate = false` in its `FieldSpec`.

THE PREDICTED WORLD (off by default) lets a player who bumps another see it at once: give the
schema `interact` (a function that resolves a contact between two entities' states, the same one
the server runs, returning whether they touch), set `rc.predict_radius` (metres; `interest_x`
and `interest_z` name the position fields) and `rc.event_channel` (a reliable channel). The
others within the radius are stepped beside the own entity by their last inputs; each contact is
an event (`kContactEvent`) whose outcome, `confirmed`, `undone`, `unpredicted` or `heard`, is in
`client.outcomes`: play a bump's sound when it is predicted, and take it back when it is undone.
A game's own events (a shot, a pickup) go the same way: `replicate_client_predict_event` on the
client, `replicate_server_event` on the server. Past `rc.predict_max_s` (0.1 s) of replay the
others fall back to their curves, so on a slow link it costs nothing; `rc.resim_budget` caps a
replay's entity-steps on a slow machine.

Spawn a player's entity with `replicate_spawn(server, peer, values)` when its peer connects. A
hit is judged with `replicate_rewind(server, id, client_view_tick, out)`. Set
`interest_radius` (with `interest_x`, `interest_z`) for an open world and `snapshot_budget` to cap
a client's bandwidth. Run it on the simulated network under every preset and count
`client.stats.corrections`: with no loss it must be zero.

## Lockstep and rollback: the same step everywhere

When every peer can run the whole world (a strategy game, a fighting game, a two-player race),
send only inputs with `net/lockstep.hpp`. Connect every seat to every other (a mesh, one
unreliable channel), and give the game's step and digest; rollback also needs save and load:

```cpp
net::LockstepGame g;
g.step = my_step;      // void(void* ctx, uint32_t step, const uint8_t* const* inputs, const uint8_t* sizes)
g.digest = my_digest;  // uint64_t(void* ctx): the whole state's hash after a step
g.save = my_save;      // rollback: void(void* ctx, int slot), slots 0..max_rollback
g.load = my_load;
g.ctx = &world;
net::LockstepConfig lc;   // mode lockstep: turn_steps 6, delay 2; rollback: turn_steps 1, delay 1
lc.seats = 4;
lc.seat = my_seat;
net::lockstep_init(ls, lc, g, node);
// each fixed step, after node_update:
net::lockstep_receive(ls);
net::lockstep_input(ls, order, n);          // orders, appended (rollback: lockstep_set_input, a pad)
net::lockstep_update(ls);                   // runs the steps it may; a missing input waits
if (ls.desync.found) { /* ls.desync.step: the first step two seats differ on */ }
```

Show "waiting for" with `lockstep_waiting(ls)` (a bit a seat). The step must be deterministic to
the bit: no `<cmath>`, randomness only from the game's seeded stream. Measure a session's waits and
latencies under every preset before choosing `delay` and `turn_steps`.

## Tune the rates, and watch a live game

A client's `rate`, `update_rate`, `cmd_rate`, `redundancy` and `interp_ticks` and the server's
`max_rate`, `max_update_rate` and `tick_hz` are `ReplicateConfig` fields. Bind them to the console
with `net::NetCvars nc{&rc}; net::add_net_cvars(console.cvars(), nc);` (the config's values are
the defaults; call `net::net_cvars_apply(nc)` after the cfg runs): `sv_fps` (latched),
`sv_maxupdaterate`, `sv_maxrate`, `cl_updaterate`, `cl_cmdrate`, `rate`, `cl_timesync`,
`cl_smooth`, `cl_interp`, `cl_extrapolate`. Pick them
with `ge net sweep --knob=update-rate --values=10,20,30,60 --presets=wifi,bad`, which names the
cheapest value within reach of the best. To see a running game or server's packets, open a tap
on its transport (`net::Tap tap; net::open_tap(tap, 27950, &err); transport.tap = &tap;`) and run
`ge net watch --port=27950`. To play it over a bad link on a LAN, `net::transport_fake(transport,
&fake, conditions, seed)`.

## Test it on bad links

Use the simulated network in tests and labs, never sleeps or real sockets:

```cpp
net::SimNetwork sim = net::make_sim_network(seed, conditions);   // net/sim_network.hpp
net::LinkConditions bad;
net::conditions_preset("bad", bad);       // lan, broadband, wifi, mobile, bad, awful
net::sim_set_link(sim, 0, 3, bad);       // node 0 to node 3 only; sim_set_path sets both ways
// bad.stream = true: the link behaves as TCP (nothing lost; a drop arrives a resend late)
net::open_sim_transport(t, sim, net::sim_address(1, 1));
// each tick: sim_advance(sim, now_us), then every node's update and flush
```

A bot can play the game over a simulated link: give it a node like a player's and step the
network between the frames. Hold the game to what the player must never see, such as a
reliable event lost or an input applied twice, under every preset, and keep the digest of the
session in a golden.

Before shipping a change to how the game uses the network, push the engine's session until it
breaks: `ge net stress --mix=<the game's shape> --chaos=all --soak=3600 --fuzz=1000000` names the
loss, burst, latency, jitter and bandwidth each mix breaks at, checks that every client ends a
partition, a lost or restarted host, a rebound port and skewed clocks reconnected or cleanly timed
out, soaks an hour of virtual play for flat memory, and fuzzes the wire beside an honest session.
A game's own session can take the same chaos: put `net::ChaosStep`s and `reconnect_tries` on a
`net::TrafficPlan` (net/traffic.hpp), or drive its own nodes and change the links with
`sim_set_path` at the script's times.

## Capture and read the traffic

- **A capture.** Point `transport.capture` at a `net::Capture` (`net/capture.hpp`). On a
  live build, give it a `limit_bytes`. Then `net::save_capture(c, "runs/session.pcap", &err)`.
  The file is a normal pcap and Wireshark opens it.
- **`ge net inspect <pcap> [--peer=<addr>] [--type=payload]`**: every datagram on one line.
  It shows the time, the addresses, the type, the sequence and acks, and each message's
  channel, id and size.
- **`ge net stats <pcap>`**: one row per connection, as seen by the node that took the
  capture.
  - Columns: packets and kbit/s each way, round trip p50/p95, loss each way, duplicates,
    reorders and resends.
  - It also writes a series file and charts of kbps, rtt and loss over time; read them with
    `ge graph`.
  - A warning means a link lost more than 5%.
- **`ge net sim [--presets=wifi,bad] [--mix=action|download] [--link=latency=80,loss=3%] [--transport=tcp] [--budget=256] [--capture=FILE]`**:
  fake clients and a host, with no game.
  - It prints delivery, round trip, loss, bandwidth and a digest per session.
  - Use it to see what a link does before blaming the game, and to reproduce a player's link
    from the numbers `ge net stats` gave.

**The tick is part of the round trip.** A node reads its socket once a frame, so each way
takes its latency rounded up to whole frames. At 60 Hz, a 0.25 ms LAN measures 33 ms. A
round trip far beyond `2 × ceil(latency / frame)` is a queue: look at `kbps out` against the
link's bandwidth.
