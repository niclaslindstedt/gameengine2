---
name: net
description: "Use for engine/net in gameengine2: multiplayer networking. Addresses, the wire, UDP and TCP sockets, the simulated network (stream links too) and its conditions, pcap captures, the packet format, connections (acks, round trip, loss, unreliable/sequenced/reliable channels, big messages in pieces, priorities, the send budget), nodes (host, client, peer; up to 32), sessions (the lobby: seats, teams, chat, kick, start, reconnect by token), LAN discovery, NATs (the simulated kinds, STUN, the rendezvous, hole punching, the relay), opening a port on the router (PCP, NAT-PMP, UPnP IGD, the simulated router), replication (quantized snapshots and deltas, interest, the byte budget, prediction and reconciliation, interpolation, lag compensation by rewind), lockstep and rollback (turns, input delay, prediction and resimulation, step digests that name a desync), fake-traffic sessions, pressure tests (ramps to a breaking point, chaos scripts and reconnects, soaks and memory, the fuzzer), the net-link, net-session, net-reach, net-replicate, net-lockstep, net-stress, net-chaos and net-fuzz labs; and the M17 plan (lobby, chat, NAT, replication, lockstep, voice, accounts)."
---

# Networking

`engine/net/` (`ge_net`, namespace `ge::net`) is how games on the engine play together. The
design and the plan are [`docs/networking.md`](../../../docs/networking.md); the milestone rows
are `docs/goals.md` M17a–M17k. Everything here is generic: peers, channels, messages, never a
skier. A game's half (what it replicates, its message ids, its lobby's settings) lives in
`games/<game>/net/`.

| File | What |
| --- | --- |
| `net/address.hpp` | IPv4, IPv6 and simulated (`sim:<node>:<port>`) addresses; parse, print, hash |
| `net/wire.hpp` | The bounded byte writer and reader, varints, 16-bit sequence arithmetic |
| `net/os_socket.hpp` | The socket shim (POSIX, Winsock): the only file with the OS's socket headers; not installed |
| `net/udp_socket.hpp` | The OS's non-blocking UDP sockets; `os_entropy` |
| `net/tcp_socket.hpp` | The OS's non-blocking TCP sockets: listen, accept, connect, packets as length-prefixed frames |
| `net/conditions.hpp` | A link's conditions and each packet's fate (a `stream` link: nothing lost, a drop a resend late, head-of-line blocking); the presets `lan` … `awful` |
| `net/sim_network.hpp` | The simulated network: conditioned directed links on a virtual clock, seeded |
| `net/capture.hpp` | pcap captures (link type RAW; sim nodes as 198.18.0.0/15), written and read back |
| `net/transport.hpp` | One send/receive over a UDP socket, TCP streams (a host's, or a client's one) or the simulated network, captured |
| `net/packet.hpp` | THE WIRE FORMAT, stated once: packet types, the payload header, messages, `describe_packet` |
| `net/connection.hpp` | One connection: sequences, acks, RTT (RFC 6298), loss, the three channel kinds, big messages in pieces (`largest`), `priority`, `budget_kbps`, `LinkStats` |
| `net/node.hpp` | Host, client or peer: the challenge handshake, up to 32 peers, timeouts, events, flush |
| `net/session.hpp` | Sessions: the lobby on a node's reliable channel; `SessionHost` seats players and holds the rules, `SessionClient` asks and mirrors |
| `net/session_wire.hpp` | The session's messages, stated once for both sides (not installed) |
| `net/discovery.hpp` | LAN discovery: a broadcast probe on a fixed port, each host's advert, the hosts found |
| `net/stun.hpp` | STUN's Binding request and answer: the public address a NAT gave a socket |
| `net/rendezvous.hpp` | The public server: sessions registered, a client introduced to its host with a nonce and a relay port, STUN answered, relays forwarding |
| `net/reach.hpp` | The two ends on the node's socket: the host registers and punches, the client joins, connects, follows a punch's port (`node_retarget`), falls back on the relay; `reach_wire.hpp` the messages (not installed) |
| `net/portmap.hpp` | Opening a port on the router: PCP, then NAT-PMP, then UPnP IGD on a socket of its own; the lease renewed at half its life, closed with a lease of 0; `portmap_wire.hpp` the three protocols' messages (not installed) |
| `net/sim_router.hpp` | The simulated router: PCP, NAT-PMP, SSDP and UPnP's SOAP answered on a NAT's LAN face (`NatConfig::gateway`), each grant a forward on the NAT |
| `net/net_cvars.hpp` | The network's cvars (`sv_fps`, the rates, `cl_timesync`, `cl_smooth`, `cl_interp`, `cl_extrapolate`) bound to a `ReplicateConfig`, the game's values as the defaults |
| `net/replicate.hpp` | Replication, the server's word: the schema's quantized fields, per-client deltas against the acked snapshot, interest, the budget, redundant inputs, time sync (the server's slack in each snapshot, the client's clock dilation, the interpolation delay from the arrivals' need), prediction and smoothing (the spring, the snap distance), the predicted world (others by their last inputs from the snapshot's trailer, `Schema::interact`, contact events and their outcomes, the replay budget, the horizon), interpolation on cubic curves, capped extrapolation and the blend back, `replicate_rewind`; `replicate_server.cpp` and `replicate_client.cpp` its halves, `replicate_wire.hpp` the messages and the bit packing (not installed) |
| `net/replicate_trial.hpp` | One measured replicated world (clients, a crowd, rewinds) over a link: what the `net-replicate` lab and `ge net sweep` run |
| `net/lockstep.hpp` | The same step everywhere: seats in a mesh, inputs in turns with a delay, lockstep's waits or rollback's prediction and resimulation, the inputs message with its acks, step digests compared to name a desync |
| `net/lockstep_trial.hpp` | One measured lockstep session (a strategy game in turns, a fighting game with rollback, a desync planted) over a link: what the `net-lockstep` lab runs |
| `net/tap.hpp` | The live tap: a running node's datagrams mirrored as pcap records to a loopback port for `ge net watch` |
| `net/traffic.hpp` | Fake traffic: a whole session over the simulated network, measured end to end; chaos steps, reconnect tries, a per-tick hook; `sim_held_bytes` and `node_held_bytes` (what a soak holds flat) |
| `net/stress.hpp` | Pressure tests over the traffic session: ramps bisected to a knob's breaking point against `StressBounds`, the named chaos scripts and their endings, the soak, the fuzzer (`stress_fuzz.cpp`) |

## Rules

- **No clock, no OS randomness inside.** Every call takes `now_us` from the caller; salts and
  secrets come off a `NodeConfig::seed` (a real game passes `os_entropy()`). That is what makes a
  session over the simulated network a pure function of its seed: the labs' digests and the
  tests depend on it. `make libm-check` scans `engine/net`.
- **The wire format is `packet.hpp`'s.** A change to a packet's layout changes every capture's
  meaning and `describe_packet`; change both together and say so in the PR.
- **Nothing allocates after `node_init`** except the event buffer's rare growth; every ring is
  sized up front (`kSentRing`, `kReliableInFlight`, `kReliableSpan`).
- **Verify on the simulated network first.** A real socket test (`tests/net_udp_socket_test.cpp`)
  proves the OS path; everything else is proven under the presets, headless.
- **Big messages are a reliable channel's, and configured.** Set `ChannelConfig::largest[ch]`
  (up to `kMaxBigMessage`, 64 KB) on a reliable channel; each connection keeps a buffer that size
  for the channel to join pieces in. Never raise `kMaxMessage` or send pieces by hand. A
  message's pieces take that many reliable slots at once: a 64 KB message needs 56 of the 128.
- **Priorities change the packing only when room is short.** With every priority equal the
  order is the order queued, packet for packet, so adding the fields moved no digest; keep it so.
- **A node polls once a tick.** The round trip it measures includes each side's wait for its
  next update: the `net-link` lab's `want` column states the model. Poll faster (or update
  before and after the step) to shrink it, never subtract it in the stats.

- **A session's rules are the host's.** A client only asks; every rule (seats, team caps, the
  chat bucket, the leader, kicks and bans) is decided in `session_host.cpp` and answered with
  `refused` and why. Add a rule there and its `SessionWhy`, never in the client.
- **A player is a seat, not a peer.** Code that keys anything per player uses the seat
  (`session_seat_of_peer`); a reconnect gives the same seat a new peer slot.
- **A script acts as the leader the links chose.** On a dedicated server the first SEATED
  player leads, and jitter decides who that is: a lab or test asks the clients' mirrors who
  leads (`players[me].leader`) rather than assuming the first to join.
- **A NAT is the simulated network's, a path is reach's.** NAT behaviour (mapping, filtering,
  expiry, forwards) lives in `sim_network.cpp`'s `map_out` / `map_in` and nowhere else; which
  path a pair should take follows from RFC 4787 (a hole opens unless one side is symmetric and
  the other port-restricted or symmetric), and the `net-reach` lab and `net_reach_test` hold every
  pair to it. The rendezvous never answers bigger than it was asked: pad every new ask.
- **A router's protocols are stated once, in `portmap_wire.hpp`,** for the client and the
  simulated router alike; a router's behaviour (what it speaks, the lease cap, a taken port, the
  permanent-only quirk) lives in `sim_router.cpp`. A forwarded socket's datagrams leave from the
  forwarded port (`map_out`), as a real router's static mapping does: a host answering from
  another port would never be reached by its forward.
- **Fake conditions and the tap sit in the transport,** below the node: a fake drop is a datagram
  that never left (the tap and the capture see only what really went), and a held one leaves on
  the next send or receive. Neither may allocate per datagram: the pool is sized in
  `transport_fake`.
- **Replication's state lives on the grid.** The server quantizes every entity after each tick
  and each `replicate_set`, and the client's prediction does the same after each step; that is
  why a lossless link corrects nothing (`net_replicate_test` holds it to zero). A field added
  without bits enough for its step makes every prediction wrong by a quantum. A snapshot's row
  priority is the age of the client's copy in the BASELINE, never a counter reset when a row is
  sent: acks lag, so snapshots chain on two or three baselines at once.

## Labs and tests

- `make lab LAB=net-link` (`--presets=bad,awful --clients=32 --seconds=30`; `--transport=tcp`
  makes every link a stream, `--mix=download` adds 64 KB messages checked whole, `--budget` caps
  every connection): fake traffic under
  each preset; the table (joined, rtt beside the model's range, loss beside the configured,
  reliable events delivered and in order, latencies, delivery of snapshots and inputs, the host's
  bandwidth, resends), the round trip and loss charts, the reliable latency boxes, and one
  preset's session as `previews/net-link/<preset>.pcap` (opens in any packet tool).
- `make lab LAB=net-session` (`--presets=bad,awful --clients=32`): a dedicated host's lobby
  filled and scripted under each preset (joins, teams, three chat scopes and a flood, a kick and
  its ban, a six-second cut and the return to the seat, settings, start, return); the table
  checks every chat copy, every roster, the kick, the ban and the seat, the seats are charted
  and the pings boxed.
- `make lab LAB=net-reach` (`--presets=lan,wifi,bad`): every pair of NAT kinds (none, full cone,
  restricted, port-restricted, symmetric) reaching a host through the rendezvous: the path each
  took against the one predicted, its connect time, a reliable message each way; STUN from
  behind each kind; the connect times as a heatmap; then a symmetric host's router speaking each
  of PCP, NAT-PMP and UPnP: the time to map, the fallbacks, the renewals of a 20 s lease over a
  minute, a client behind another symmetric NAT let straight in, the close.
- `make lab LAB=net-replicate` (`--presets=lan,wifi,bad --clients=32 --npcs=64`; `--budget=600`,
  `--radius=150`): a replicated world under each preset: each client's bandwidth, corrections a
  minute and the worst, snapshots dropped, rows starved, inputs repeated, and how far the
  server's rewind lands from what each client drew; bandwidth charted, rewind gaps boxed.
- `make lab LAB=net-lockstep` (`--presets=lan,wifi,bad --seats=4 --turn-steps=6 --delay=2`;
  `--send-every=3`, `--max-rollback=8`, `--plant=600`): four strategy seats in turns and two
  fighting seats with rollback under each preset: waits, an order's and a pad's latency, bytes,
  rollbacks and their depth, agreement; a planted desync and the step it is named on.
- `make lab LAB=net-stress` (`--knobs=loss,bandwidth --mix=action --soak-seconds=3600`): each
  knob ramped to its breaking point, its latencies charted against the knob, and a soak's held
  memory charted. `make lab LAB=net-chaos` (`--scripts=partition,rebind --clients=16`): every
  client's ending under each script, the connected clients charted. `make lab LAB=net-fuzz`
  (`--datagrams=1000000 --spoof=0`): the honest session beside the fuzz, from strangers and
  spoofed. `ge net stress --chaos=all --soak=3600 --fuzz=1000000` runs all four at once; reach
  for it after any change to the node, the connection or the wire, and put its table in the PR.
- `make lab LAB=net-predict` (`--presets=wifi,bad --skew=0.02 --hz=72,120`): each preset at each
  tick rate, fixed against time sync against smoothing, true clocks and skewed: an input's time
  to the server's step, the repeats, the view delay, the ticks drawn past the newest snapshot,
  the corrections, the drawn paths' jerk and the error when snapshots run late; charts of one
  client's queue, the delay, the jerk and the late error. Run it before and after any change to
  the client's clock, the slack, the smoothing or the interpolation. Every duration in
  `ReplicateConfig` is in seconds at `tick_hz`: a new one is too, or it is wrong at 72 Hz. Its
  joust (`--predict-radius=6 --predict-max=0.1`) pairs the clients charging each other with the
  partner predicted or interpolated: corrections a minute and a contact, the partner's drawn
  error, the events' outcomes and the replay's cost. Compare a joust's rows on a link past the
  horizon over several `--seed`s: there they differ only by the network's draws.
- `ge net sweep --knob=update-rate --values=10,20,30,60 --presets=wifi,bad`: the replication
  trial once a value; reach for it before arguing about a rate. `ge net watch --port=27950`: a
  running node's tap, live; a game opens one with `open_tap` and `transport.tap`.
- `ge net` (`build/debug/ge net ...`; `engine/cli/net.hpp`): `inspect <pcap>` decodes every
  datagram, `stats <pcap>` measures each connection from the capturing node (rtt, loss each way,
  dups, reorders, resends) and charts its kbps, rtt and loss, `sim` runs the same sessions as the
  lab from the command line (`--presets`, `--mix`, `--link=latency=80,loss=3%`, `--capture`,
  `--transport=tcp`, `--budget`).
  Read a lab's capture with `ge net stats previews/net-link/bad.pcap` before guessing.
- Tests: `tests/net_*_test.cpp`, `tests/cli_net_test.cpp`. `net_tcp_socket_test` runs frames
  and two nodes (a 64 KB message) over real loopback TCP. `net_traffic_test` holds the guarantees under every preset;
  `net_connection_test` holds the stream's rules on a scripted wire; `net_node_test` the
  handshake, the 32-peer limit, denial and timeouts; `net_session_test` every session rule;
  `net_discovery_test` probes and adverts on the simulated network and real loopback UDP;
  `net_sim_network_test` each NAT kind's filter, expiry and forwards; `net_stun_test` the RFC's
  layout; `net_reach_test` every pair of kinds, an unknown session and a missing relay;
  `net_portmap_test` each protocol's map, renewal and close on the simulated router, the
  fallbacks, the permanent-only and refused cases, a forwarded symmetric host reached directly,
  the RFC layouts, and NAT-PMP and UPnP over real loopback sockets; `net_replicate_test` no
  correction on a lossless link, corrections that land under loss, deltas against whole
  snapshots, interest, a budget's starvation, a rewind against the drawn, and the bits replayed.
- Before and after a change here: `make lab LAB=net-link ARGS=--tag=before`, then
  `--compare=before`, both in the PR.
