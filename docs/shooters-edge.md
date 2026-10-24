# Shooter's Edge

The plan for the engine's second game: **Shooter's Edge**, a fast multiplayer arena shooter in
modern C++ on this engine, compatible with the mods, maps, servers and clients of the classic
multiplayer shooter whose formats and protocol it speaks. It starts once Fall Line is on the
engine (`docs/goals.md` M18 follows M10–M13 and M17k). Until a row lands, this page is the plan;
change it in the same PR that changes the plan.

The game lives in `games/shooters_edge/`. Outside the `porting/` working area nothing names the
game it is compatible with or its implementations: the formats are named by their file
extensions and versions, the language its mods are written in is QC, and the wire format is the
classic protocol (version 28). `make agents-check` holds it.

## What it is

- **Functionality, never code.** The game does what the classic game does (the movement a player's
  hands know, the QC a mod runs, the packets a server sends), written fresh in this engine's
  C++ from the file formats, the protocol and the language's reference, and from measured
  behaviour. No existing implementation's source is copied, translated or paraphrased
  (§ Licensing and the clean room).
- **Compatible where it counts.** Existing content runs unchanged: pak and pk3 archives, a game
  directory per mod, BSP maps, the model, sprite, texture and sound formats, QC bytecode (the multiplayer and single-player `progs`), config files, and recorded demos. Our client joins the servers
  people run today and their clients join ours (§ The network).
- **Better where it can be.** Everything the engine already does well applies: a modern GPU
  frame over Dawn, the synth's mixer, deterministic simulation with labs and goldens, the bots'
  decision records, the `ge` CLI. The modern features players expect from today's clients are
  in the plan (§ Modern features), not bolted on later.
- **Reusable first.** Anything a first-person shooter, or any game on the same map format, would
  use goes in `engine/` or a plugin; only what makes Shooter's Edge itself stays in
  `games/shooters_edge/` (§ Where the code goes).
- **A test of the `ge` CLI.** The port is also the proof that `ge` is complete enough for an
  agent to build a second, very different game with it. Every command the port finds missing is
  added to `ge` when it is found (§ The `ge` tooling it needs).

## Compatibility contract

What existing content needs from the game, and therefore what "compatible" means. A row is held
by a lab or a test over free content (§ Content and test data).

| Content | Formats | What compatible means |
| --- | --- | --- |
| Archives and search paths | `pak` (a directory of files in one blob), `pk3` (a zip, stored or deflated, with its own directory tree), a `.pk3dir` directory standing in for an unpacked pk3 while a mod is developed; loose files over them | The base directory, then a game directory per mod (`-game`, the server's `*gamedir`), archives in numeric then name order, later ones overriding earlier; the same file names a mod expects |
| Maps | BSP version 29, BSP2 (the larger-limits variant), the `.lit` coloured lightmaps beside them, BSPX lumps (extra data in the same file) | Every lump read: planes, nodes, leaves, the visibility (PVS, run-length coded), faces, lightmaps with up to four styles a face, textures (with animated and random sequences, liquids, sky), clip hulls (point, player, large), brush models, the entity lump |
| Models | MDL (vertex-animated, frame groups, skin groups, 8-bit packed vertices and normals from a fixed table), SPR (sprites with orientation types) | Every frame and skin a mod names; the player model's colours (top and bottom) remapped at draw time; frame interpolation as a modern client does it |
| Images | The 256-colour palette, the colour map (light levels to palette), LMP pictures, WAD2 (the 2D graphics: the status bar, the font) | Drawn as the classic game draws them, and also as true colour (§ The picture) |
| Sound | WAV (8 and 16 bit PCM, mono, any rate, the loop cue) | Played at their own rates, looped where the cue says, spatialized, with ambient sounds by leaf |
| Game logic | QC bytecode: the multiplayer `progs` and the single-player one | Every opcode, the entity field layout the progs declares, the globals, the string table, the builtins by number, the server's entry points (`StartFrame`, `PlayerPreThink`, `ClientConnect`…), the engine-side fields the server reads (`origin`, `velocity`, `movetype`, `solid`, `effects`, `modelindex`…) |
| Extended logic | The extension builtins and fields modern servers and clients share, asked for by name (`checkextension`) | The common set first (§ Modern features), each one a row of a table the VM answers from |
| Config and console | `.cfg` scripts, `exec`, `alias`, `bind`, cvars with server- and user-info, `wait`; the advanced scripting today's clients added (§ Configs) | A player's existing config runs, however advanced; a server's `server.cfg` runs; a cvar the game does not know is kept and written back, never an error |
| Demos | Client demos (one client's view), MVD (every player, written by the server) | Played back, seeked, and written; an MVD from another server plays here |
| The network | The classic protocol (version 28) and its common extensions | § The network |

## Configs

Players carry configs that grew for years in today's clients, and a competitive player will not
rewrite theirs. `engine/console` therefore runs the console language at its full modern extent,
specified in our own words in `porting/shooters-edge/configs.md` and held by a corpus of config
fixtures we write (`tests/golden/console_*.txt`):

- **The classic core**: `exec`, `alias`, `bind` and `unbind` (with the `+`/`-` pairs a held key
  sends), `set`, `seta` (archived), `setinfo` and `serverinfo`, `wait`, `echo`, `toggle`, the
  command buffer's order, quoting and `;` splitting, `//` comments.
- **Variables in commands**: `$cvar` expansion in any command line, with escaping; alias
  arguments (`$1`, `$2`, `$*`).
- **Conditionals and arithmetic**: `if` / `else` blocks over numbers and strings, `inc` and
  `dec`, `cycle` through values, calculation into a cvar (`set_calc`-style operators: arithmetic,
  comparisons, string length, substrings, search), temporary aliases.
- **Bind modes and modifiers**: separate bind tables for the game, the console, menus and demo
  playback; modifier keys in a bind; key names for every key and mouse button and the wheel.
- **Team messaging**: message macros expanded at send time (`$health`, `$armor`, `$ammo`, the
  weapon, `$location` from a map's location file), message triggers that run an alias when a
  matching line is printed, and the teamplay filters.
- **Config files as data**: `cfg_save` writing every archived cvar, bind and alias in a stable
  order, `cfg_load`, per-mod configs in the game directory, `autoexec.cfg`; HUD layouts as
  cvars, so a HUD config is a config.
- **Safety**: a server can stuff only what a client allows (the cheat and server-locked flags),
  a runaway alias loop stops at a depth with a message naming the alias, and a script's errors
  name the file and line.

## Licensing and the clean room

**The engine.** gameengine2 is under the PolyForm Noncommercial License 1.0.0 (`LICENSE`): open to
read, change and share for noncommercial use, with commercial use reserved to its copyright
holder. That is exactly why no GPL code may enter it: the GPL forbids adding restrictions such as
"noncommercial only", so a derived work of GPL code could not carry this licence. A clean-room
rewrite is not a derived work: copyright protects code, not functionality, file formats or
protocols, and reimplementing them for compatibility is lawful.

**The existing implementations.** The original game's source, the community clients and servers
and their mods are released under the GNU GPL. Copying, translating or closely paraphrasing any
of it would make this tree a derived work bound by the GPL, which this tree's licence cannot be. The plan avoids that by building
from specifications, not source:

1. **The specification is written in our own words.** Every format, message and behaviour the
   game needs is described in a working note under `porting/shooters-edge/` before it is built:
   from the public descriptions of the file formats and the protocol, the QC language's
   reference, and measurements of running programs. A note states facts (a lump's offset, a
   message's bytes, a builtin's number and what it returns, a measured acceleration curve);
   it never quotes source.
2. **The builder reads the note, not the source.** An agent writing engine or game code works
   from the note and from the black-box measurements (step 3). It does not open GPL source while
   building that subsystem. Research that needs to read source to answer a question writes the
   answer as a fact in the note, in a separate session, and the building session reads only
   the note.
3. **Behaviour is matched by measurement.** Where the feel must match (the player's movement,
   the timing of a weapon, what a server sends after a given input), a lab runs an existing
   server or client as a separate program, never linked and never committed (fetched and built
   in the lab's own cache), feeds it scripted inputs, and records what comes out: positions,
   velocities, packets. Those recordings are facts about a program's output, and our code is
   held to them by a lab with a tolerance (`se-parity`). Running a GPL program as a tool is not
   distributing it.
4. **Nothing GPL enters the tree.** No source, no patch, no compiled `progs` built from
   GPL QC, no shader or table copied out of one. Tables a format defines (the packed vertex
   normals, the default palette's role) are data the format specifies; where a table's values
   come only from GPL code, the note derives or measures them instead, and says how.

**Mods.** A mod a player loads keeps its own licence, GPL included: the VM executes its bytecode as
data, which makes neither work a derivative of the other. The game never bundles a GPL mod.

**Content.** The classic game's own data is not redistributable. The game never ships or
fetches it; a player points the game at their own copy. Tests and labs run on
free content (§ Content and test data).

## Content and test data

- **Free game data**: a BSD-3-Clause replacement for the classic game's data (maps, models,
  textures, sounds; named in `porting/shooters-edge/survey.md`), fetched at a pinned release into the labs' cache and
  checked by hash, never committed. Its maps and models are the labs' reference scenes.
- **Free QC**: the labs need a multiplayer `progs` that is not GPL. The plan is our own QC
  compiler (`ge qc`, M18d), written from the language reference, and a small deathmatch mod of
  our own in QC, so every server test runs on bytecode built in the tree. Until then the
  VM's tests run bytecode written by a tiny assembler in the tests.
- **Fixtures of our own**: the tests write their own tiny archives, maps (a box, two rooms and a
  door, a ramp, water) and models with writers in the test support code, so a unit test needs
  no download.
- **A player's own data**: the client reads the base game directory and any mod from a directory the player
  chooses; nothing in a test depends on it.

## Where the code goes

Decided with the `plugins` skill's questions; dependencies run game → plugin → engine.

| Home | What | Why there |
| --- | --- | --- |
| `engine/vfs` (new) | The virtual file system: archives (a directory, a pack, a zip), search paths in order, file lookup without allocation on the hot path, reading a file whole or as a stream | Any game with mods or downloadable content wants it; Fall Line's assets could live in one |
| `engine/core/inflate` (moved from `engine/lab`) | Deflate decoding for zip archives | The labs' inflater becomes shipped code; it gets a golden and a fuzz pass |
| `engine/audio` | Sampled voices beside the synth's: PCM buffers at their own rates, resampled to 48 kHz, looped, attenuated by distance and panned by the listener; a channel limit with stealing | Any game with recorded sounds; the synth stays the only voice Fall Line uses |
| `engine/platform` | Relative (captured) mouse with raw deltas, every key and mouse button named, the wheel, text input for a console, gamepad | Any first-person game; text input any game with a console |
| `engine/console` (new) | Cvars (typed, flags: archived, server info, user info, cheat, latched), commands, aliases with arguments, binds in modes, `exec` of scripts, the command buffer and `wait`, `$cvar` expansion, conditionals and arithmetic, triggers, `cfg_save`, completion (§ Configs) | A console is generic: Fall Line's developer page can use it, and a mod's config needs it |
| `engine/ui` | Bitmap fonts from a sheet, 2D pictures from palette images, the scaled HUD, a drop-down console's text view | The 2D layer M10 builds, given the pieces a classic HUD draws |
| `engine/render` | A static-geometry pass with lightmap atlases and animated light styles, vertex-animated (morph) meshes with frame interpolation, sprites, a palette-and-colour-map look as a post option, dynamic point lights, scrolling and warped materials, sky layers and boxes, portal-free PVS culling as a visibility input to the existing instance culling | Each is a mechanism; the BSP specifics that feed it are the `brush` plugin's |
| `engine/net` | The replication layer (M17f) used by the native protocol; a channel on the classic protocol needs only the sockets, the simulated network and the capture | The native protocol is the engine's own; the compatible one is the game's |
| `engine/play` | The play contract grown for a first-person game: an angle axis kind on the protocol's grid, a step whose length the input sets (a usercmd's milliseconds), several players a run | Any first-person game an agent plays through `ge` |
| `plugins/brush` (new) | The brush world: BSP29 and BSP2 loading, the PVS and PHS, clip hulls and the trace through them, brush models, the entity lump's parser, the lightmap and texture data with animation sequences; `app/`: feeding the world pass | Any game on this map format (single-player, multiplayer, a mod's own game) |
| `plugins/shooter` (new) | The first-person shooter's movement model (ground and air acceleration, friction, stepping, ducking, water, jumping, the strafe and bunny-hop behaviour players rely on), prediction's contract with a server, hitscan and projectile helpers over a trace callback, the view's bob and roll, the bot's aim and movement hands on M16 | Any FPS on the engine, whatever its map format |
| `plugins/progs` (new) | The QC VM: the progs loader and checker, the interpreter, entity and global memory, strings, the builtin table a game fills, errors with a QC stack trace, the profiler, the extension registry; the compiler's front end for `ge qc` | Any game that runs QC bytecode (a single-player game, a multiplayer one, a mod's standalone game) |
| `games/shooters_edge/` | The server (its frame, the multiplayer builtins, the clients), the client (prediction, interpolation, the HUD, menus, the console's commands), the protocol (native and compatible), demos, its labs, its play contract, the dedicated server executable | What makes Shooter's Edge Shooter's Edge |

**The one exception to C++ only.** QC is executed, never written into the engine: the VM
runs existing mods' bytecode for compatibility, and the engine, the plugins and the game stay
C++. The tree's own QC is limited to the test mod the labs run (`games/shooters_edge/qc/`).
AGENTS.md and the non-goals in `docs/goals.md` state this exception.

## What the engine has, and what it lacks

| Need | The engine today | Gap, and the row that closes it |
| --- | --- | --- |
| Fixed-step simulation, deterministic | `loop/run_clock` at 120 Hz; `core/libm`; goldens; labs as determinism tests | The server's frame runs on the run clock at its own rate (77 Hz, the classic tick); each player's move runs on its usercmd's milliseconds, as the protocol defines it. QC floats are 32-bit and its math builtins call `core/libm`, so a server replays to the bit from its input log (M18c, M18f) |
| Files and archives | Embedded blobs; the labs' inflater | `engine/vfs`, inflate shipped (M18a) |
| A map format with visibility and collision | A heightfield world (`plugins/mountain`) | `plugins/brush` (reading: M32b; collision: M18b) |
| Scripting for mods | None, by design | `plugins/progs`, the stated exception (M18c, M18d) |
| Player movement and weapons | `engine/move` (M28): the mover for any genre, the classic arena row among its presets, strafe jumping charted (`docs/movement.md`) | The classic row checked against recordings, the clip hulls answering the mover's trace, the weapons: `plugins/shooter` (M18e) |
| A server with many clients | `engine/net` links, channels, sessions, NAT, the simulated network, captures | The server's frame and clients (M18f); the compatible protocol (M18g); the native one on M17f's replication (M18h) |
| Client prediction and interpolation | Planned in M17f | M17f lands the generic layer; the game's own prediction over `plugins/shooter` (M18h) |
| Demos | The input log (`play/inputs`), recordings (`ge record`) | Client demos and MVD read and written, seek by snapshot (M18h) |
| Rendering | Dawn, the pass table, instances with Hi-Z culling, shadows, TAA-U, bloom, post, skinning; lavapipe in the container | The static-geometry pass with lightmaps and light styles, morph meshes, sprites, sky layers, warps, the classic look, dynamic lights, PVS culling (M18i) |
| Sound | The synth, the meter, WAV writing | Sampled voices, spatial sound, ambient sounds by leaf (M18j) |
| Input | Keyboard, pointer, focus | Captured mouse with raw deltas, every key, the wheel, binds, text input (M18k) |
| 2D and text | Planned in M10 | Bitmap fonts, palette pictures, the console's view, the HUD and menus (M18k, after M10) |
| A console and settings | Fall Line's settings | `engine/console` (M18k) |
| Bots | `engine/ai`: decision records, hands, paths, sight | A deathmatch bot as a server-side client: navigation over the brush world (a waypoint graph generated from the map on `ai/path`), aim hands, item timing, its decisions traced (M18f) |
| Labs | Sim, scenario, picture, drive and bench labs; digests; `ge lab diff`, `ge graph` | Each row's labs (§ Labs) |

## The network

The two protocols the owner chose: both.

**The compatible protocol** (`games/shooters_edge/net/classic_*`) speaks the classic protocol,
version 28, and the extensions today's servers and clients negotiate, over `engine/net`'s UDP sockets,
simulated network and pcap capture (but not its packet format, channels or handshake, which are
the engine's own):

- The connectionless messages: status queries (what a server browser asks), the challenge, the
  connect with its user info, and the heartbeats a server may send to public master servers
  (off unless the server's owner configures a master).
- The net channel: sequence and acknowledgement with the reliable bit, the client's port number
  for NAT rebinding, the reliable message resent until acknowledged.
- Server to client: the reliable messages (print, sound, the stats, the serverdata, the model
  and sound lists, spawning static entities, lightstyles, the intermission), and every frame the
  player states and the entities as deltas against the last frame the client acknowledged.
- Client to server: the last three usercmds in every packet (angles, the movement axes,
  buttons, impulse, milliseconds), each delta coded against the one before, so a lost packet
  costs no input; string commands on the reliable stream.
- The extensions in a first tier: float coordinates (maps bigger than the 16-bit range),
  transparency, more than 64 visible entities, the larger model and sound lists, and the MVD
  stream's own extension flags. The negotiation (the extension bits in the challenge and the
  connect) is a table, so the next extension is a row.
- Downloads of maps, models and sounds a client lacks, from the server, in chunks.

**The native protocol** is the engine's own (`docs/networking.md`), used between our client and
our server when both ends are ours: the M17 handshake and AEAD (M17i), the snapshot ring and
deltas with interest (M17f), prediction of the local player and of projectiles, lag
compensation by rewind, voice (M17h), the session layer's lobby and chat (M17d), and NAT
traversal (M17e). Gameplay is identical under both: the server's frame and QC do not know
which protocol a client speaks, and one server serves both kinds of client at once.

**Held by labs on the simulated network**: a compatible client and our server, our client and
a compatible server, under every condition preset; and, in `se-interop`, our client against an
existing server program and an existing client against ours, run as black boxes on loopback
(§ Licensing and the clean room, step 3).

## The picture

Built on the existing frame (`docs/rendering.md`), measured in the container on lavapipe like
every pass, and judged by captioned screenshots:

- **The world**: one static vertex buffer for the map's faces, drawn by material in a few
  calls, culled by the PVS of the camera's leaf and then by the existing Hi-Z instance culling.
  Lightmaps packed into atlases; up to four light styles a face, each style's brightness a
  uniform the frame updates (the flicker and pulse strings mods set), so animated light costs
  no re-upload. Coloured light from `.lit` files when present.
- **Materials**: animated texture sequences, warped liquids with optional transparency, the
  two-layer scrolling sky or a skybox, fullbright palette colours as emissive.
- **Models and sprites**: MDL frames as morph targets in one buffer, interpolated on the GPU;
  the player's colours remapped in the shader; sprites as instanced quads by orientation type.
- **Effects**: the classic particle effects as the engine's compute particles; dynamic lights
  from rockets, muzzle flashes and powerups as a small light list; the screen blends (damage,
  powerups, liquids).
- **Two looks, one renderer**: the classic look (nearest filtering, the palette and the colour
  map applied as a post pass, so it looks like the 8-bit software renderer) and the modern
  look (filtered true colour, the engine's shadows, bloom and TAA-U, optional higher-resolution
  replacement textures). A lab pictures every scene in both.
- **Headless**: a CPU raster view of any map from above (the leaves, the PVS of a point, the
  hulls) for the labs, like Fall Line's map views, with no GPU at all.

## Modern features

Today's community clients and servers added much since the original release. Grouped by what a
player of today expects, then what makes it better, then what can wait. The research behind the
list is in `porting/shooters-edge/survey.md`.

| Tier | Feature | Row |
| --- | --- | --- |
| Expected | Rendering at the display's rate independent of the network and physics rate | M18h |
| Expected | Smooth interpolation of other players and projectiles; prediction of the local player | M18h |
| Expected | A configurable HUD built from elements (each a position, a size, a style), a scoreboard, frags and teamplay readouts | M18k |
| Expected | MVD demos recorded by the server, demo playback with seeking, rewinding and speed control, a demo browser | M18h |
| Expected | Spectating and following a player; a server-side record of every match | M18h |
| Expected | Raw mouse input sampled at its own rate, independent of the frame | M18k |
| Expected | Lag compensation on the server for hitscan weapons (rewinding the other players) | M18g |
| Expected | Larger maps and limits (BSP2, float coordinates, more entities, more models and sounds) | M18b, M18g |
| Expected | A server browser over status queries and the public masters | M18k |
| Better | Coloured lightmaps, replacement textures, skyboxes, fog, transparency | M18i |
| Better | Real-time shadows from the engine's shadow passes on models | M18i |
| Better | Voice chat on the native protocol (M17h) | M18l |
| Better | Skeletal models (IQM, glTF) beside MDL, on the engine's skinning | M18l |
| Better | A data-driven particle system for mods (effects named in a script) | M18l |
| Better | The common QC extension builtins and fields (string handling, tracing, the extra entity fields), registered by name | M18c, M18l |
| Later | Client-side QC (a mod drawing its own HUD and predicting its own entities) | M18m |
| Later | Single-player campaigns and co-op on the single-player `progs` | M18m |
| Later | Splitscreen; touch controls for phones and tablets | M18m |

## Labs

Every row lands its labs (G9), and every lab runs headless in the container:

| Lab | Kind | What it shows |
| --- | --- | --- |
| `pak` | sim | Archives mounted in order, every file's winner, lookup cost; digest of the file table |
| `bsp` | picture | A map from above: leaves, the PVS of a point, hulls, entities; lump sizes and limits |
| `qc` | sim | A progs run: per-function time and calls, builtins hit, errors with QC stack traces |
| `pmove` | scenario | Scripted movement on fixture maps: ground acceleration, air control, strafe jumping, bunny hopping, stairs, ramps, water; trajectories charted |
| `se-parity` | scenario | The same scripted inputs through our movement and an existing server's, measured against each other within a tolerance (needs the reference program; skipped with a reason when its cache is absent) |
| `se-server` | drive | Eight bots deathmatch on a free map for a minute: frags, items, the server's frame cost, its digest |
| `se-net` | sim | Compatible and native clients on the simulated network under every preset: bandwidth, prediction errors, corrections |
| `se-interop` | drive | Our client and server against existing programs on loopback (cache-gated like `se-parity`) |
| `se-world` | picture | Named views of free maps in both looks, with captions |
| `se-audio` | sim | Sounds of a scripted fight rendered, metered and written as WAV |
| `se-demo` | sim | A demo played back, seeked and re-recorded; the digest of its frames |

## The `ge` tooling it needs

The port is also a test of `ge` (G6). These are the commands an agent will need to iterate on
it quickly; each is added to `ge` as the row that needs it starts, and any further gap found
during the work is added when it is found and recorded in `docs/goals/ge-backlog.md`.

| Command | What it answers |
| --- | --- |
| `ge pak ls/cat/extract/build <archive or dir>` | What a search path holds, which archive wins a file, a file's bytes; a fixture pack built from a directory |
| `ge bsp info/ents/shot <map>` | A map's lumps and limits, its entity lump as text, a top-down picture of leaves, PVS from a point and hulls, all on the CPU |
| `ge qc build/disasm/run/trace/profile` | Compile QC, disassemble a progs, run a function headless, trace a step's QC calls with arguments and results, time every function |
| `ge qc diff <progs-a> <progs-b>` | Which functions and fields changed between two builds of a mod |
| `ge server --map=<m> --bots=<n> --for=<s>` | A headless dedicated server running bots, its input log and digest, for `ge replay`, `ge state` and `ge diverge` |
| `ge demo info/frames/shot/diff <demo>` | A demo decoded to text frame by frame, a frame pictured, two demos compared to their first difference |
| `ge net inspect --protocol=classic <pcap>` | A capture decoded as the classic protocol's messages (the net channel, every server and client message, the deltas) |
| `ge pmove <scenario>` | A movement scenario's trajectory as a table and a chart, against the parity recording when one exists |
| `ge console <server> "<command>"` | A command or cvar run in a headless server or client, its output returned |
| `ge play` with angle axes | An agent at the controls of a first-person game: look angles on the protocol's grid, the usercmd's milliseconds, several players a run |

## Rows

The roadmap rows are in `docs/goals.md` (M18 and its lettered rows), after Fall Line. The order:
content (M18a) and the map (M18b) first, then QC (M18c, M18d), movement (M18e), the server
with bots (M18f), the compatible protocol (M18g), the client (M18h), the picture (M18i), sound
(M18j), input and the 2D layer (M18k), the modern features that need all of it (M18l) and what
can wait (M18m).
