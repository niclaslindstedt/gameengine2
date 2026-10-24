# Networking

How games on this engine play together over a network: the design `engine/net` is built to,
the decisions behind it, and the order it lands in (`docs/goals.md` M17). Where a part is not
built yet, this page is the plan. Change it in the same PR that changes the plan.

## What a game asks of the network

The engine serves every kind of multiplayer game a game on it may be, so the design starts from
the shapes those games take, not from one of them:

| Shape | Typical game | Players | Transport | Who decides | What crosses the wire |
| --- | --- | --- | --- | --- | --- |
| **Fast action** | an arena shooter, a survival sandbox, an open-world crime game | 2–32 a server | UDP | the server | the inputs up, snapshots (deltas) down; the client predicts its own body and interpolates everyone else |
| **Racing** | a circuit racer, a downhill race, a free ride on one mountain | 2–32 | UDP | the server (or the host) | the same as fast action; the field's standings are the server's |
| **Action RPG** | a dungeon crawler with a party | 2–8 a game | TCP or UDP | the server | commands up, world events down, reliable and ordered; little prediction |
| **Persistent world** | an MMO zone, a survival server that runs for weeks | up to the game's max a session, many sessions | TCP or UDP | a dedicated server | commands up, what is near you down (interest), the world saved to disk |
| **Turns and lockstep** | a 4X strategy game, an RTS | 2–8 | TCP or UDP | nobody: every peer runs the same deterministic step | inputs (orders) only, a turn at a time; a digest to catch a desync |
| **Rollback** | a short race or a fight between few players | 2–4 | UDP | every peer, predicted and corrected | inputs only; a peer rolls back and resimulates when a late input arrives |

What every one of them needs, and so what the engine owns:

- **A link**: send datagrams to an address and receive them, over a real UDP socket, a TCP stream
  or a simulated network, behind one interface.
- **Connections**: a handshake that a spoofed address cannot complete, keepalives, a timeout,
  a clean disconnect, and a game-set maximum of peers (at most 32).
- **Channels**: unreliable (a snapshot, a voice frame), sequenced (only the newest counts: an
  input), reliable and ordered (chat, a lobby change, an order in a turn).
- **Numbers**: round-trip time, jitter, packet loss, bandwidth each way, resends; on every link,
  every second, for the HUD's ping and for the tools.
- **Sessions round the game**: a lobby (players, ready, teams, the game's settings), chat (to
  all, to a team, privately), a scoreboard's pings, kicking, reconnecting.
- **Reaching a host**: open a port on the router (UPnP, NAT-PMP/PCP), learn the public address
  (STUN), punch through two NATs, relay when nothing else works, find a game on the LAN.
- **Replication**: the server-authoritative model (snapshots, deltas, prediction,
  interpolation, lag compensation, interest) and the lockstep model (inputs, turns, digests,
  rollback). The engine's determinism (G1) is what makes lockstep and rollback cheap here: two
  machines running the same step on the same inputs get the same bits.
- **Voice**: capture, encode (Opus), send unreliably, a jitter buffer, loss concealment, mixed
  into the synth; to all, a team, one player, or by distance.
- **Accounts**: sign up and log in (a password hashed with Argon2id, or a store's sign-in linked
  to it), a session token the game's hosts trust, a friends list with requests, presence (online,
  in a lobby, in a game) and invites to a lobby. They need a service that keeps them: the
  engine ships it as an executable built from this tree (`ge-service`: accounts, friends,
  presence, the matchmaker that signs connect tokens, the rendezvous and the relay), which a
  game's owner runs on a server of their own; nothing is hosted for them, and its keys come
  from the environment, never the tree (M17j).
- **Tools**: everything above is verified headless, on a GPU-less Linux machine, by a simulated
  network and by labs and `ge` commands that read what crossed the wire.

## The layers

```
games/<game>/net          what this game replicates, its messages, its lobby's settings
        │
engine/net/replicate      snapshots and deltas, prediction, interpolation, interest, rewind;
engine/net/lockstep       turns, input delay, desync digests, rollback (M17g)
        │
engine/net/voice          Opus frames, the jitter buffer, voice routing (M17h)
engine/net/session        lobby, teams, chat, private messages, the player list, kicks,
                          reconnecting by token; discovery: finding a game on the LAN (M17d)
engine/net/reach          STUN, the rendezvous, hole punching, the relay (M17e1);
engine/net/portmap        a port opened on the router: PCP, NAT-PMP, UPnP IGD (M17e2)
engine/net/guard          input checks, interest, replay audits, cheat flags, reports (M17l)
engine/net/stress         load, soak, chaos scripts, ramps to the breaking point, fuzzing (M17m)
        │
engine/net  node          connections: handshake, keepalive, timeout, peers up to 32
            channel       unreliable, sequenced, reliable-ordered messages; acks; resends;
                          big messages in pieces, priorities, the send budget
            packet        the wire format: header, acks, messages; decoded for the tools
            transport     one interface over a UDP socket, a TCP stream, a simulated network
            sim_network   the simulated network: every packet's fate off a seeded stream
            conditions    latency, jitter, loss and its bursts, duplicates, reorder, bandwidth
            capture       pcap files any packet tool opens, read back by `ge net`
            udp_socket    the OS's UDP sockets (POSIX and Winsock)
            tcp_socket    the OS's TCP sockets, carrying the packets as length-prefixed frames
            os_socket     the socket shim both share: the only file that includes the OS's headers
            address, wire IPv4, IPv6 and simulated addresses; the byte writer and reader
        │
engine/core               the seeded stream, output
```

Everything in `engine/net` is generic: it speaks of peers, channels, messages, teams and
snapshots, never of a skier. A game's half (what it replicates, its message ids, its lobby's
settings) lives in `games/<game>/net/`. A genre's shared half (a racing field's standings over
the wire) would be a plugin's.

## Decisions

| Part | Decision | Why |
| --- | --- | --- |
| Own protocol or a library | Our own, small, over UDP | The pieces are small and well understood (one header of acks, three channel kinds, a challenge handshake); a library (ENet, GameNetworkingSockets, yojimbo) brings its own allocator, threads, clock and logging, none of which a deterministic simulated network can drive. Ours runs on a virtual clock, so the same seed is the same packets, and every lab is a determinism test |
| UDP and TCP | UDP is the default; a TCP transport carries the same packets as frames (a 16-bit length, then the bytes) on one stream a peer (M17c) | A fast game cannot wait for a lost packet's resend; a turn-based game, a chat-heavy world or a network that blocks UDP can. The layers above the transport do not care which: over a stream the acks and channels still run, and simply never see a loss, only the late packets head-of-line blocking makes |
| The clock | The caller passes `now_us` into every call; nothing in `engine/net` reads a clock | The simulated network runs on a virtual clock and a real game on the frame's; the same code, and a test is exact |
| Randomness | Salts and the simulated network's fates come from `core::Rng` streams the caller seeds; a real game seeds them from the OS's entropy (`os_entropy`) | A simulated session replays to the bit; a real one is not guessable |
| Allocation | Fixed-capacity rings sized at create time (peers, the sent-packet ring, the reliable windows) | Nothing allocates in `node_update` once a node is made (G2) |
| Packet size | At most 1200 bytes on the wire (`kMaxPacket`) | Fits the smallest MTU on the paths games use (IPv6's 1280 less its headers) with no fragmentation by IP; bigger messages are sent in pieces by the engine (M17c) |
| Big messages | Only on a reliable channel configured for them (`ChannelConfig::largest`, up to 64 KB): pieces of one packet each, reliable messages of their own with a "more follows" bit, joined in order and delivered once; queued all together or refused | The reliable channel already delivers once and in order, so joining is appending, with no fragment ids or timers of its own. An unreliable message bigger than a packet would be lost whenever any piece was: a snapshot that big is a delta's job (M17f) |
| Priority and budget | Each channel has a priority; a connection may have a send budget (kbit/s, a token bucket of 50 ms). Higher priorities ride first; with the bucket empty only acks and keepalives go, reliable messages wait and loose ones are dropped at the flush's end | A map download must not starve the inputs, and a server with 32 clients must keep each inside what its link carries. With every priority equal the order is the order queued |
| Acks | Every payload carries its sequence, the newest sequence received and 32 bits of the ones before it | One packet's loss is told by the next few, with no ack packets of their own; the round-trip time comes off the same acks |
| Reliability | Per message, not per packet: a reliable message rides in packets until one carrying it is acked, resent once a round trip has passed | The newest data goes in every packet; nothing waits behind a lost packet but the reliable messages it carried |
| Handshake | Request → challenge (a cookie only the host can make) → response → accept, all packets padded to the request's size | A spoofed address never gets past the challenge, the host keeps no state for it, and a reply is never bigger than the request (no amplification) |
| Peers | A node is both host and client: it may accept and it may connect (`NodeConfig`) | Client-server, a listen server and a peer-to-peer mesh (lockstep, rollback) are the same code; the game sets the maximum, 1 to 32 |
| Encryption | AEAD over every payload with keys from a connect token a matchmaker signs (M17i); off until then | A LAN game and the labs need none; an internet game does. Keys come from the environment or GitHub secrets, never the tree |
| Capture format | pcap (libpcap's format) with IPv4/UDP headers made up for simulated addresses | Every packet tool opens it, and `ge net` decodes our protocol inside it |
| Accounts | Our own service executable, self-hosted, over the same transport (reliable channel over TCP or UDP); passwords stored only as Argon2id hashes; a login returns a signed session token a game host checks without asking the service | One account and one friends list across every store and desktop; testable headless in the container against the simulated network |
| Sessions | A layer over a node's reliable channel (`session.hpp`): the host's process seats players and holds every rule, a client asks and mirrors; a player is a SEAT, not a peer slot, with a 64-bit token to come back to it | A dropped player keeps their seat, name, team and readiness for `reconnect_us`, and the roster survives a new peer slot; the host being the one authority is what makes the rules (rate limits, team caps, kicks) hold, and what an anti-cheat later trusts (M17l) |
| Who leads | The host's own player on a listen server; otherwise the first seated, then the longest-seated present player | A dedicated server has no player of its own, and a lobby still needs someone to pick the map and start |
| LAN discovery | Its own UDP socket on a port the game fixes: a broadcast probe (64 bytes), each host's advert (never bigger than the probe) | A node's port need not be known in advance, a probe never reaches a node, and discovery cannot amplify traffic at a third party |
| Voice codec | Opus (BSD-licensed), 48 kHz like the synth, 20 ms frames, in-band FEC | The standard for voice at 16–32 kbit/s; loss concealment built in |
| NAT | UPnP IGD and NAT-PMP/PCP to open a port, STUN to learn the public address, hole punching through a rendezvous, a relay last | No hosted service: the rendezvous and the relay are executables built from this tree that a game's owner runs (G5: GitHub hosts only code and releases) |

## The wire (M17a)

Every packet starts with the game's 32-bit protocol id (its name and version hashed: two builds
that cannot play together drop each other's packets) and a type byte. All integers are little
endian; `engine/net/wire.hpp` writes and reads them.

| Type | Body | Sent by |
| --- | --- | --- |
| `request` | the client's salt, then zeros to 1200 bytes | a node connecting |
| `challenge` | the client's salt, the host's cookie (a keyed hash of the client's address, salt and the host's secret) | the host, stateless |
| `response` | the client's salt, the cookie, zeros to 1200 bytes | the node connecting |
| `accept` | the client's salt, the session key (the two salts mixed), the slot, the maximum | the host |
| `deny` | the client's salt, the reason (`full`, `closed`) | the host |
| `payload` | the session key, sequence (16 bits), ack, ack bits (32), then messages | both, once connected |
| `disconnect` | the session key | either, three times |

A message inside a payload is its channel byte (the channel in the low bits; the top bit set
when a 16-bit id follows, as on a sequenced or reliable channel; the next bit set on a big
message's piece when more pieces follow), the id, its length (a varint) and its bytes. A payload with no messages is a
keepalive. `describe_packet` decodes any packet into one line for the tools.

The round-trip time is smoothed as TCP does (RFC 6298: SRTT and RTTVAR, gains 1/8 and 1/4) off
each acked packet's send time; loss is the share of the packets sent over the last window that
were never acked once they were older than a round trip and its spread.

## Sessions (M17d)

A session runs on one reliable channel the game gives it (`SessionConfig::channel`); the game
reads the node's events after the session and skips that channel. Its messages are stated once
in `engine/net/session_wire.hpp`: a client's `hello` (its token, its name), `ready`, `team`,
`chat`, `kick`, `start`, `back` and `settings`, and the host's `welcome` (the seat, the token,
the lobby's size, the phase), `player` (a seat's state and why it is sent: joined, rejoined,
away, changed, or a snapshot for a new client), `gone`, `pings`, `chat`, `settings`, `phase`,
`kicked` and `refused` (what was asked, and why not).

| Rule | What the host does |
| --- | --- |
| Seats | A hello without a token takes the lowest free seat and a fresh token; one with a token comes back to its seat. A full lobby, a started game (unless `join_in_progress`) and a banned token or address are refused, and the peer is let go once the refusal is acked |
| Away | A peer that times out leaves its seat away (unready) for `reconnect_us`, then frees it; a peer that disconnects frees it at once |
| Teams | `team_count` teams of at most `team_size`; a new player joins the smallest; a move into a full team, or once the game has started, is refused |
| Start and return | Only the leader; start needs `min_players` present and, with `need_ready`, everyone ready; return makes everyone unready |
| Chat | Control bytes become spaces; a line is cut at `chat_max` bytes on a UTF-8 boundary; each player's bucket holds `chat_burst` lines and gains one every `chat_refill_us`; a team line reaches the team, a direct line its target, a nearby line (proximity chat) whoever the game's `SessionConfig::hears` says is in earshot of the sender (refused `no_nearby` in a session with none), and every line comes back to its sender |
| Kick | Only the leader, never the host's own player; the kicked player is told, the seat freed at once, and the token and the address refused for the rest of the session |
| Pings | Every `roster_us` the host sends each seat's round trip as it measures it |

A client whose host times out tries again every `retry_us` with its token until `reconnect_us`
has passed, then reports the session `lost`.

**Discovery** (`discovery.hpp`): a client broadcasts a probe (the protocol, a nonce) to the
game's discovery port; each host listening answers with an advert (its session's port, seated,
maximum, phase, name). Over UDP the probe goes to a broadcast address; on the simulated network
to `sim_broadcast(port)`, which reaches every socket bound on that port.

## Reaching a host (M17e1)

Most players sit behind a home router's NAT, which lets a datagram in only on a mapping one of
its own made going out. Three pieces get a client to a host anyway:

| Piece | What it does |
| --- | --- |
| STUN (`stun.hpp`) | A 20-byte Binding request; the answer's XOR-MAPPED-ADDRESS is where the server saw it come from: the node's public face. The rendezvous answers it on its own port, and so does any public STUN server |
| The rendezvous (`rendezvous.hpp`) | A small public server. A host registers its session every `register_us` (the server learns its public address and says so); a client joins by the session's id, and the server sends each the other's public address with the client's nonce and a relay port with its key. Every ask is padded to the biggest answer, so the server never amplifies a flood |
| Reach (`reach.hpp`) | The two ends, on the node's own socket (its `keep_foreign` hands them the rendezvous's datagrams). The host punches: datagrams carrying the nonce to the client's public address, which open its own NAT to it, and it binds the relay. The client connects its node to the host's public address; a punch with its nonce from another port (a symmetric NAT's port for this pair) moves the connect there (`node_retarget`). After `direct_us` without an accept it binds the relay and connects through it |

Which path a pair takes follows from RFC 4787's behaviours, and the simulated NATs
(`sim_add_nat`) have them all: a full cone lets anyone in on a mapping; a restricted NAT only a
node the inside has sent to; a port-restricted NAT only that node's port; a symmetric NAT makes
a mapping per destination and lets only that destination answer. A hole opens unless one side is
symmetric and the other port-restricted or symmetric: the symmetric side's port for the pair is
one the other side never sent to. Those pairs go through the relay, which forwards between the
two addresses that bound it with the key and nothing else. A port forward
(`sim_nat_forward`) is what a router opens when it is asked (below).

The `net-reach` lab runs all 25 pairs of the five kinds (none and the four) under each preset:
direct pairs connect in a few round trips, relayed ones a little over `direct_us`.

## Opening a port (M17e2)

A host whose router forwards it a port is reached straight, whatever either side's NAT: no punch
and no relay. `portmap.hpp` asks the router for one on a socket of its own, trying three
protocols in turn (`portmap_wire.hpp` holds their messages):

| Protocol | How | |
| --- | --- | --- |
| PCP (RFC 6887) | A 60-byte MAP request to the gateway's port 5351, with a nonce and the client's own address | A NAT-PMP-only router answers it with version 0 and UNSUPP_VERSION, which moves the client on at once |
| NAT-PMP (RFC 6886) | The external address and a UDP map, to the same port | |
| UPnP IGD | SSDP's M-SEARCH to 239.255.255.250:1900; the router's description over HTTP; SOAP `AddPortMapping` and `GetExternalIPAddress` on its WANIPConnection service | A router that gives only permanent leases (error 725) is asked again with a lease of 0 |

An unanswered ask is sent again after 250 ms, then doubling, four times, before the next protocol
is tried. A mapping is renewed at half its lease, and a renewal keeps asking until the lease runs
out (a burst of loss over a few sparse asks would otherwise cost a lease with half its life
left). `portmap_close` takes it down with a lease of 0 (UPnP: `DeletePortMapping`). The host then
tells the rendezvous (`reach_host_forward`): its register names the port, and clients are
introduced by it. A router's forward is a static mapping: everything the forwarded socket sends
leaves from that port, so the host's answers come from the address the client connected to.

Over a real UDP socket UPnP's HTTP runs on a TCP stream (`tcp_send_bytes`); on the simulated
network it is one datagram each way, asked again on a timeout as TCP would resend. The real
gateway's address and the machine's own LAN address are the caller's (`PortmapConfig::gateway`,
`local`). `SimRouter` (`sim_router.hpp`) is the router on the simulated network: it answers any
mix of the three on its NAT's LAN face (`NatConfig::gateway`), cuts a lease to its
`max_lifetime_s`, gives a taken port elsewhere (PCP, NAT-PMP) or refuses it (UPnP's 718), and
only answers its own LAN. The tests also run NAT-PMP over real loopback UDP and UPnP over real
loopback UDP and TCP. The `net-reach` lab's second table runs, under each preset, a host behind
a symmetric NAT whose router speaks one protocol, and a client behind another symmetric NAT: it
maps, renews a 20 s lease for a minute, lets the client straight in and closes.

## Replication (M17f)

The server-authoritative model, for fast action, racing and the persistent world alike:
`replicate.hpp` over a node, two unreliable channels (snapshots down, inputs up), and a game
that names its entities' fields and its step.

| Piece | What it does | Why |
| --- | --- | --- |
| The schema | Up to 16 fields an entity, each quantized to its bits over [min, max]; an angle wraps | A snapshot carries bits, not doubles: a position to half a millimetre over 512 m is 20 bits |
| Quantized state | The server keeps every entity's state on the grid after each tick and each `replicate_set` | A client that replays the same inputs from a snapshot through the same step reaches the server's own doubles: on a link that loses nothing a prediction is never corrected (the tests hold it to zero) |
| Deltas | Each client's snapshot is written against the newest one it acked, still in the server's ring: each row's changed fields under a mask, a new row whole, a gone row by its id; whole when no acked one is left | A world standing still costs a header; a moving one only what moved |
| Interest | A client is sent its own entity and those within `interest_radius` on two named fields, and as `visible` allows; a row out of sight is sent gone | The persistent world's and the open world's "what is near you", and nothing hidden is sent (M17l) |
| The budget | `snapshot_budget` caps a snapshot's bytes; rows are written stalest first, a row's priority being how many ticks the client's copy (the baseline's) is behind; a row left out keeps the baseline's value | Deltas against acks that lag form chains of baselines: a priority reset when a row is sent would starve it in the other chain for ever, so the age is read off the baseline itself |
| Inputs | Each tick's input goes up with the last `redundancy` (4) beside it; the server queues them by number and applies one a tick | One lost packet costs nothing. A tick with none waiting steps the last again and the queue grows by one, a buffer the link's jitter set; inputs lost past the redundancy are taken to be the one before them, less those the repeats stood in for, so the entity is stepped as often as the client stepped it |
| Prediction | The client steps its own entity at once, and on each snapshot replays every input the server had not applied over the server's state; a difference is kept as an error drawn out through a critically damped spring (half-life `spring_halflife_s`, 1/15 s) that carries its own velocity, or shown at once past `snap_distance` (a teleport, a respawn); `spring` off, the error decays by `smoothing` a tick | A decay jumps the drawn velocity at every correction, a kink the eye catches; the spring starts from the velocity the path already had, so it bends instead |
| Time sync (M17n) | Each snapshot carries the client's SLACK: the fewest of its inputs left waiting on the server over the last half second (-1: a tick went without). The client runs its tick `replicate_client_dilation` times as fast (within 3%) to hold the slack at zero, one in reserve for a few seconds after a window ran short | A clock a little fast piles inputs up on the server (a third of a second of lag within a minute at 2%); a little slow starves it (a repeated step, a correction). Held at zero, an input arrives just in time whatever the drift |
| Interpolation | With time sync, others are drawn behind the client's reckoning of the server's tick by what the last 64 snapshots' arrivals needed (how far the reckoning had run past the newest snapshot when the next came), eased up quickly and down slowly; with sync off, `interp_ticks` (6) behind the newest heard of. Between snapshots, a cubic Hermite curve whose slopes come from the snapshots either side (`cubic`); past the newest, carried on at the last velocity for up to `extrapolate_s` (0.2 s, then held; a field with `extrapolate` false is held at once) and blended back over `extrapolate_blend_s` once the late one arrives | On a clean link the others are drawn 38 ms behind instead of 100; on a jittery one the delay grows until they rarely stand still. Straight lines turn a corner at every snapshot, and a held entity stops and starts |
| The tick rate | `tick_hz` is the game's (`sv_fps`): every duration above is set in seconds and turned into ticks at it | A shooter at 72 Hz and a racer at 120 behave alike with the same settings; the `net-predict` lab runs both |
| Lag compensation | Each input names the tick the client was drawing; `replicate_rewind` returns any entity at that tick (fractional) from the server's quantized history | A hit is judged against what the player saw |

Nothing reads a clock or draws randomness: the exchange over the simulated network replays to
the bit, and the `net-replicate` lab's hash says so. In that lab 32 clients and 64 movers the
game moves cost about 190 kbit/s a client with no budget; `--budget=600` holds every client
near 140 kbit/s on every preset, the stalest rows going first. With no loss nothing is
corrected; over the `bad` preset (90 ms, 5% loss in bursts) a prediction is corrected about once
a second, by a few metres at worst after a long burst; the server's rewinds land on what the
client drew (a median gap of zero).

## Prediction, state of the art (M17n–M17q)

M17f predicts the client's own entity, replays the inputs the server has not applied over each
snapshot, decays a correction's error, interpolates the others a fixed six ticks back and rewinds
for hits. Four rows take it the rest of the way, each measured by the `net-predict` lab under every
preset (the prediction's error against the server, how often and how far it corrects, the drawn
path's jerk, the view delay):

| Row | What | Why |
| --- | --- | --- |
| M17n (built) | Time sync: the client's tick locked to the server's; the client ahead by just enough that its input lands a target buffer early; the server reports each client's input queue and the client dilates its tick a few percent to hold it; the interpolation delay set by the jitter measured | A fixed lead stalls on a bad link and wastes delay on a good one; dilation keeps the queue at its target with no stall, no repeat and no drop, and the view as fresh as the jitter allows |
| M17o (built) | Corrections drawn through a critically damped spring, velocity continuous, snapping only past a distance; others on cubic curves through their velocities; a capped extrapolation by the game's own step when snapshots are late | A decay alone leaves a kink in the velocity the eye catches; a late snapshot should not freeze the others |
| M17p (built) | The predicted world: what the client's input touches resimulated with it, others' inputs taken as their last; events predicted under a client's id, matched or undone; a resimulation budget | A player who bumps another or fires sees it at once, not a round trip later |
| M17q | Sub-tick inputs applied at their fraction of a tick; the rewind capped (the shooter favoured up to a limit) and the hit confirmed; a finish line's order at sub-tick resolution | A tick is 16 ms: a press, a shot or a finish should not be rounded to it |

M17n is built; the `net-predict` lab measures it. With 16 clients whose clocks run 2% fast or
slow, an input's p95 from the hand to the server's step falls from 367 ms to 50 on `lan` and from
467 to 250 on `bad`, and the corrections a minute fall by up to seven times. With true clocks the
view delay falls from 100 ms to 38 on `lan`, 106 to 61 on `broadband` and 120 to 88 on `wifi`,
drawing past the newest snapshot under 1% of the time. On `awful` it grows from 317 ms to 444 and
stands still 3% of the time instead of 8%: there the delay buys smoothness, and M17o's
extrapolation will take some of it back.

M17o is built on top, measured by the same lab at 72 Hz and 120 Hz in three modes (fixed, time
sync, smooth). Against time sync alone, at 72 Hz the others' drawn jerk at the 99th percentile
falls from 12,900 m/s³ to 4,600 on `broadband`, 25,300 to 7,600 on `wifi` and 83,400 to 59,700 on
`awful`. The own entity's jerk, less the ticks where its own input changed, falls from 26,200 to
6,100 on `bad` and from 37,900 to 7,600 on `awful` (at 120 Hz, 43,900 to 3,900 and 85,600 to
9,200). Drawn past the newest snapshot, the others land a mean 0.28 m from the server's truth when
held and 0.01 m when carried on (`bad`, 72 Hz). At 72 Hz on `lan` an input reaches the server's
step in 14 ms and the others are drawn 31 ms behind; at 120 Hz, 8 ms and 19.

M17p is built on top: the other players within `predict_radius` of the own entity (at most
`max_predicted`, kept out to 1.5 radii) are stepped beside it by their last inputs, which the
server sends in a trailer on the snapshot (kind 3: each nearby player's id and newest input), and
a game's `Schema::interact` resolves each contact on both ends with the same step. A contact
beginning is an event (`kContactEvent`, the other's id): the client predicts it under the input
that caused it, the server sends its own over the reliable `event_channel`, and each one ends
`confirmed` (matched within `event_slop_s`), `undone` (never came within `event_wait_s`),
`unpredicted` (the server's, about an entity the client was predicting) or `heard` (about one it
was not). A replay costs an entity-step per input per predicted entity; past `resim_budget` the
farthest leave it. Past `predict_max_s` (0.1 s) of replay a stale input guesses worse than the
curve draws, so the others fade back to the curve through the spring and come back at three
quarters of it. The lab's joust (16 clients in pairs, each charging its partner for a second and
backing off the next, at 72 Hz) measures it: corrections a minute fall from 7.9 to 0 on `lan`
(every contact confirmed), 13.1 to 5.8 on `broadband` (96%) and 92 to 14 on `wifi`; on `mobile`,
`bad` and `awful` the replay is past the horizon and the partner is drawn as interpolated (their
rows differ from the interpolated ones only by the network's draws, which the trailer's bytes
move: other seeds swap which is lower).

Which of them a kind of game uses: a shooter all four; a racer its own vehicle predicted (M17n,
M17o), rivals extrapolated a little so they are drawn where they are, contacts on the server
(M17p); a fighting game rollback (M17g) with M17n's sync; a strategy game lockstep; a persistent
world M17n and M17o for its own movement only. Character movement (M28d) reconciles an
`engine/move` body on top of them.

## Rates and live tools (M17f2)

A client asks for its own rates and the server caps them, as the settings a player tunes in a cfg
(the console's names in brackets). `add_net_cvars` (`engine/net/net_cvars.hpp`) binds every one of
them to the `ReplicateConfig` a game makes, the game's own values as the defaults, so a shooter
can start at 72 Hz and a racer at 120; `net_cvars_apply` turns `cl_interp`'s seconds into ticks at
`sv_fps`:

| Setting | Side | What it does |
| --- | --- | --- |
| `ReplicateConfig::rate` (`rate`) | client | snapshot bytes a second it takes; each snapshot gets rate / its snapshots a second, at least 64 bytes, the stalest rows first |
| `update_rate` (`cl_updaterate`) | client | snapshots a second it asks for; sent every max(`send_every`, tick_hz / update_rate) ticks |
| `cmd_rate` (`cl_cmdrate`) | client | input messages a second; each carries every input since the last and `redundancy` before them |
| `redundancy` (`cl_cmdbackup`) | client | inputs repeated in every message |
| `interp_ticks` (`cl_interp`, s) | client | how far behind the newest snapshot others are drawn without time sync |
| `sync` (`cl_timesync`) | client | time sync (M17n) on or off |
| `spring`, `cubic` (`cl_smooth`) | client | smoothing (M17o) on or off |
| `extrapolate_s` (`cl_extrapolate`) | client | the most the others are carried past the newest snapshot |
| `predict_radius` (`cl_predictworld`, m), `predict_max_s` (`cl_predictmax`, s) | client | the predicted world (M17p): the other players this near predicted beside the own entity, while the replay is no longer than the horizon |
| `max_rate` (`sv_maxrate`), `max_update_rate` (`sv_maxupdaterate`) | server | the most any client is sent, whatever it asks |
| `tick_hz` (`sv_fps`, latched) | both | the step's rate, 10 to 240 Hz, which turns the rates and durations into ticks; a change waits for a new map or a restart |

The client's ask rides on every input message (`rate` u32 and `update_rate` u16), so a change
takes effect on the next one. Finding the right numbers is an experiment, and three tools run it:

- **`ge net sweep`** runs a replicated world (`replicate_trial.hpp`: clients steering movers, a
  crowd the server moves, prediction, interpolation and rewinds, the same world the
  `net-replicate` lab measures) once a value of one knob: a rate, `interp`, `backup`, the tick, or
  any link condition. It tables the bytes each way on the wire, the view delay, the corrections,
  the snapshots lost and the rewind gaps, and names the cheapest value within reach of the best
  corrections and delay, a preset at a time.
- **Drop patterns** (`LinkConditions::drop_every`, `drop_run`; `--link=drop-every=2` on `ge net
  sim` and `ge net sweep`) lose the same packets of every period, drawing nothing: "every other
  packet" as an experiment rather than a weather.
- **Fake conditions on a live transport** (`transport_fake`, `FakeLink`) put a real game's sends
  through the same fates (delay, jitter, loss, a pattern), held in a fixed pool and sent on the
  transport's next send or receive: a LAN game plays as a bad link would (`net_fakelag`,
  `net_fakeloss`, `net_fakejitter`, behind `sv_cheats`).
- **The live tap** (`tap.hpp`) mirrors every datagram a running node sends and receives, as one
  pcap record behind a mark, to a loopback port; **`ge net watch`** listens there and prints each
  connection's packets, kbps, round trip and loss every second, then saves what it saw and
  measures it as `ge net stats` does. Nothing in the game stops or changes: the tap costs a
  loopback send a datagram.

## Lockstep and rollback (M17g)

The model where only inputs cross the wire, for a strategy game (thousands of units, a few bytes
of orders a second) and, with rollback, a fighting game or a two-player racer: `lockstep.hpp`
over a node in a mesh (every seat connected to every other), one unreliable channel, and a game
that gives its step, its digest and, for rollback, a save and a load of its state.

| Piece | What it does | Why |
| --- | --- | --- |
| Seats and turns | Each player is a seat; a seat's input for a turn is whatever bytes the game gives it (orders appended with `lockstep_input`, or a pad's state set with `lockstep_set_input`), up to 128 | The engine names no unit or order: a turn's input is opaque bytes every peer hands the same step |
| Input delay | The pending input is closed as the seat's input for turn T + `delay` when the peer starts turn T; the turns below the delay are empty for every seat | An order has `delay` turns to reach every peer before it is due, so on a good link nobody waits; a session starts without waiting |
| Lockstep | A turn is `turn_steps` steps; a peer starts a turn only with every seat's input for it, or STALLS (`lockstep_waiting` names the seats) | Nothing is predicted and nothing undone: the strategy game's model, whatever its world's size |
| Rollback | A step a turn; a missing input is predicted as the seat's newest; when the real one arrives and differs, the state is loaded from before that step and every step since run again; a peer more than `max_rollback` steps past the last step every input is in for stalls | The local player's input is felt after the delay alone (a step on every preset but the worst); the game keeps `max_rollback` + 1 states |
| The message | Every send carries each of this seat's inputs some seat has not acked, how many turns of every seat's inputs this peer holds (the acks), and this seat's final step digests not yet acked; one message to every peer each `send_every` updates | One lost packet costs nothing; acks trim what is resent |
| Digests | After every step whose inputs are all real, the game's digest is kept and sent; every peer compares each seat's with its own, step by step | A desync is named by the step it happened on (`lockstep_desync`), a round trip after it was stepped: the cheat check M17l builds on |

Nothing reads a clock or draws randomness: the `net-lockstep` lab's hash says so. In that lab
four strategy seats in turns of 6 steps (100 ms) with 2 turns of delay never wait on `lan`
through `mobile` (an order is felt 233 ms after it is given at the median: the delay plus the
rest of the turn it was given in), wait 2% of the time on `bad` and 36% on `awful`; two fighting
seats with a step of delay feel their own pad a step later on every preset but `awful`, rolling
back about 13 times a second, by 1 step on `broadband`, 3 on `mobile` and 6 on `bad`. A desync
planted on one step of one seat is named on that very step on every preset, seen 17 ms after it
was stepped on a LAN and 233 ms on `awful`. Sending a message every update costs about 90
kbit/s a peer on a LAN, mostly headers; every third update cuts the inputs' bytes by a third and raises
`bad`'s waits from 2% to 7% (`--send-every`).

## The simulated network (M17a)

`SimNetwork` is a network of nodes with simulated addresses, on a virtual clock in
microseconds. Every directed link has its `LinkConditions`:

| Field | What |
| --- | --- |
| `latency_ms` | one way |
| `jitter_ms` | added to each packet, uniform in [0, jitter] |
| `loss` | the long-run share of packets dropped |
| `burst` | the mean length of a run of drops, packets (1 = independent drops; more = a Gilbert–Elliott chain with the same long-run loss) |
| `duplicate` | the share of packets delivered twice |
| `reorder` | the share of packets held back `reorder_ms` so later ones overtake them; with none, jitter never reorders (a link is a queue) |
| `bandwidth_kbps` | the link's rate (0 = unlimited): a packet waits for the ones before it to be clocked out |
| `queue_ms` | how long the queue may grow before a packet is dropped at its tail |
| `stream` | the link is a stream (TCP): nothing is lost, doubled or reordered; a packet the line drops arrives a resend timeout later (three latencies and two jitters, 200 ms at least) and every packet behind it waits (head-of-line blocking); a full queue makes the sender wait instead of dropping |

Every fate (dropped, delayed, duplicated) is drawn from the network's seeded stream in the
order the packets were sent, so the same seed and the same sends give the same deliveries, and
a session over it is a golden. Named presets (`lan`, `broadband`, `wifi`, `mobile`, `bad`,
`awful`) are the conditions the labs sweep.

**NATs** (`sim_add_nat`): a node can sit behind a NAT of any RFC 4787 kind, its datagrams leaving
from the NAT's public node on a mapped port and arriving only through the NAT's filter; a mapping
lives `mapping_us` after its last datagram out, and a port forward lets anyone in to one inside
address. What a NAT refuses is counted in `nat_dropped`.

**Fake traffic** is a node that sends a mix of messages at a rate (`TrafficMix`: so many bytes on
so many channels so often), so a link can be loaded to its edge and the numbers read.

**Bots over the network** (M17k): a game's bot is a pilot; over the simulated network it plays
as a remote client (its inputs up the link, the snapshots down), so the prediction, the
corrections and the bot's own decisions (`ge::ai::Why` records, M16) are measured under each
preset, headless and deterministic.

## Anti-cheat (M17l)

The engine's answer to cheating is that a client is never trusted with anything it could
lie about, and every claim it does make is checked against what the deterministic step allows.
It is built from the layers above, not bolted on:

- **The server's word.** In a server game a client sends inputs and the host steps the world
  (M17f); a client claims only what its game names (in the free ride, nothing but its controls).
  A speed hack or a teleport on the client changes only its own prediction, which the next
  snapshot corrects.
- **Inputs checked on arrival.** Every axis in range and on the control tape's grid, at most one
  input a step, none from a step the host has not reached, and a client sending faster than
  its tick is rate-limited and then dropped (the flood guards of M17i).
- **Nothing hidden is sent.** Interest management (M17f) decides what each client receives, so
  a wall hack or a map hack has nothing to read: what a player must not see never leaves the
  host.
- **The same step everywhere.** In lockstep and peer games the step's digest is the check
  (M17g): a peer whose state differs is out on the step it differs. Where peers own their own
  bodies, a position or a speed beyond what the step can reach from the last one is refused.
- **Replays as audits.** A score, a ghost or a record is accepted only after the server replays
  its input tape and reaches the same digest, which is what the engine's determinism (G1)
  buys: a forged score cannot also forge the run that would have earned it.
- **Bot-likeness.** M16's humanness numbers (reaction times, input noise, the shape of a turn)
  score how machine-like a player's inputs are; a flag goes for review, not an automatic ban.
- **Integrity and identity.** Packets are sealed with AEAD under the connect token's keys
  (M17i), so a forged or replayed packet is dropped; reports and bans are kept on the account
  in `ge-service` (M17j), not on a machine id.

Not in scope, by design: kernel drivers, scanning the player's machine, or obfuscation. They
are an arms race the engine does not enter, and the checks above hold without them.

## Pressure tests (M17m)

The simulated network runs on virtual time, so an hour of play takes seconds and every run is
seeded and replayable. `engine/net/stress.hpp` and `ge net stress` push the fake-traffic session
(`traffic.hpp`) until something breaks:

- **Ramps.** One knob of the links (loss, the burst a loss comes in, latency, jitter, or the
  bandwidth taken down) is raised step by step until the session breaks a bound, then bisected
  between the last step that held and the first that broke. The bounds (`StressBounds`): every
  client connected and none dropped, the inputs' one-way p95 within 250 ms, at least 85% of the
  snapshots delivered, every reliable message delivered in order by the end of the drain, the
  reliable messages' p95 within 1500 ms. The racing mix of eight clients from broadband breaks
  at 14% loss (snapshots), bursts of 56 drops at 5% loss (a reliable message, as a burst nears
  the five-second timeout), 250 ms of latency or jitter (inputs late), and 54 kbit/s a link
  (snapshots).
- **Chaos.** A script (`ChaosStep`s on the traffic plan) changes the session as it runs: a link's
  conditions changed and restored, the host's process lost and started again, a client's port
  rebound as a NAT does, a client's clock run fast or slow. Clients that lose their connection
  wait a second and try again, three times. Eight scripts (flap, partition, host-lost,
  host-restart, rebind, skew, burst, collapse) each name how they must end: every client
  connected (a flap, a burst and skewed clocks without even a drop) or, the host gone for good,
  every client timed out; never stuck connecting, and never a host slot kept for a client that
  is gone. A rebound port costs a reconnect today: a node knows its peers by address, so the
  host hears the client's new port as a stranger until the old connection times out.
- **Soak.** A session runs for hours of virtual time with the latencies summed instead of kept,
  and what the simulated network and every node hold (their buffers' capacities, `sim_held_bytes`
  and `node_held_bytes`) is sampled: the second half's peak must be the first's. An hour of 32
  clients runs in twelve seconds; the host's packet sequence wraps twice and the reliable
  message ids (events at 40 Hz) twice, every reliable message whole.
- **Fuzzing.** Beside an honest session, datagrams made or mutated each tick go on the wire
  (copies of packets in flight with bits flipped, cut short, run long or spliced; the game's own
  header over garbage; a flood of requests and responses with made-up cookies; payloads with
  guessed keys and messages of any shape), and every one is also run through the parser on its
  own. From strangers' addresses the honest session never notices: a million datagrams in
  2.5 s, the made-up cookies refused, no slot given away. Spoofed from a peer's own address, a
  mutated copy of its real packet carries its real key, and some break the connection: payloads
  are not yet authenticated (M17i adds it), and the `net-fuzz` lab's spoofed row counts it.

Not yet built (the rest of M17m): load as many sessions in one host process, timed; and a
game's bots playing as clients across many simulated servers, so its own traffic is the load.

## The tools

| Tool | Lands | Answers |
| --- | --- | --- |
| the `net-link` lab | M17a | Under each condition preset: what round trip, jitter and loss did the links measure, did every reliable message arrive once and in order, how late, at what bandwidth and how many resends; a chart of the round trip and the loss over time, and the capture attached |
| capture (`capture.hpp`) | M17a | Every packet a node sent and received, with its time, as a pcap file |
| `ge net inspect <pcap>` | M17b | Every packet decoded: type, sequence, acks, each message's channel and size |
| `ge net stats <pcap>` | M17b | Per peer: packets and bytes each way, round trip, loss, resends, bandwidth over time, charted |
| `ge net sim` | M17b | A session of fake-traffic peers over chosen conditions, its numbers and its capture, no game needed |
| traffic mixes (`traffic.hpp`) | M17b | The fake traffic shaped like a kind of game: racing, action (fast inputs, big snapshots), rpg, world (a busy shared world), lockstep (inputs and turns, no snapshots); `--mix` on the lab and on `ge net sim` |
| `net-link --transport=tcp`, `--mix=download`, `--budget` | M17c | The same sessions with every link a stream (what head-of-line blocking does to the round trip and the resends), a 64 KB message to every client every two seconds checked whole byte for byte, and every connection held to a send budget; `ge net sim` takes the same flags |
| a live node's capture | M17b | A game points its transport at a `Capture` with a size limit and saves it (`save_capture`) when a session ends or the player asks; the tools read it like a lab's |
| the `net-session` lab | M17d | A 32-player lobby on a dedicated host under each preset: everyone joins and picks a team, chats to all, to the team and to one player, one floods; the leader kicks one (whose return is refused), another is cut for six seconds and comes back to its seat, then the settings, the start and the return. The table checks every chat copy delivered, every player list the host's, the kick, the ban and the seat kept; the seats are charted and the pings drawn as boxes |
| the `net-reach` lab | M17e1, M17e2 | Every pair of NAT kinds (none, full cone, restricted, port-restricted, symmetric) under each preset: the path each pair took (direct or relayed, against the one RFC 4787 predicts), how long it took, a reliable message each way; STUN from behind each kind; the connect times as a heatmap; a router speaking each of PCP, NAT-PMP and UPnP opening, renewing and closing a port that lets a client straight in |
| the `net-replicate` lab | M17f | 32 clients each steering a mover and a crowd the game moves, under each preset: the bytes each client is sent a second, how often and how far a prediction is corrected, the snapshots lost or left without a baseline, the rows a budget starved, the inputs repeated, and how far the server's rewind lands from what each client drew; the bandwidth charted over the run, the rewind gaps as boxes |
| the `net-lockstep` lab | M17g | Four strategy seats in lockstep turns and two fighting seats with rollback, under each preset: how often a peer waits, an order's and a pad's time to be felt, the bytes a peer sends, rollbacks a second and their depth, agreement; a desync planted on one step named on that step; progress charted, latencies and depths boxed |
| `ge net sweep` | M17f2 | A replicated world run once a value of one knob (a rate, the interpolation, the input backup, the tick, a link condition, a drop pattern): bytes each way, view delay, corrections, dropped snapshots, starved rows, rewind gaps, and the cheapest value within reach of the best |
| the live tap and `ge net watch` | M17f2 | A RUNNING game's or server's datagrams mirrored to a loopback port: each connection every second, then saved as a capture and measured as `ge net stats` does |
| fake conditions (`transport_fake`) | M17f2 | A live game's sends delayed, jittered and dropped (or dropped in a pattern) as a bad link would, for playing an experiment by hand |
| the `net-predict` lab | M17n–M17q | Clients predicting over each preset: the prediction's error against the server, corrections a minute and their size, the drawn path's jerk, the view delay, the server's input queue against its target, charted over the run |
| the `voice` lab | M17h | A voice through each preset, metered against the clean one, the WAV attached |
| the `accounts` lab | M17j | Sign-ups, logins (a wrong password refused, a brute force throttled), friend requests, presence and an invite, against the service on the simulated network |
| the `cheats` lab | M17l | Each scripted cheat (speed hack, teleport, input flood, off-grid or future inputs, forged and replayed packets, fishing for hidden state, a planted desync, a forged score or ghost) caught or refused; honest bots under every preset raising no flag |
| `ge net stress` and the `net-stress` lab | M17m | Each link knob ramped to its breaking point for a traffic mix, the bound it broke and the latencies charted against the knob; a soak's memory charted over an hour of virtual time; `ge net stress --chaos=all --soak=3600 --fuzz=1000000` runs every pressure test at once |
| the `net-chaos` lab | M17m | Each chaos script over a session: how every client ended (connected, timed out, stuck), the drops, the reconnects and how long they took, the host's left-over slots; the clients connected charted through each script |
| the `net-fuzz` lab | M17m | A million fuzzed datagrams beside an honest session, from strangers and then spoofed from the peers: how many parsed, what the host refused and why, whether the session noticed; its round trip charted with the fuzz and without |
| the `net-play` lab | M17k | The game's bots as remote players under each preset: the corrections, the divergence, the bandwidth |

## Fall Line

The demo game is the pilot, once the engine's rows are done (M17k): its desktop version's free
ride played together over a dedicated server, with a lobby before the session where each
player picks the map and their skier, and the host starts it. Races are not part of it. What it
uses, so G4 holds: the free ride (up to 32 skiers on one mountain, the server authoritative,
each client predicting its own skier and interpolating the others), the lobby,
teams for the hunt, chat, private messages and voice, and an account with friends to invite. Its message ids, its snapshot's fields and
its lobby settings live in `games/fall_line/net/` (M17k).
