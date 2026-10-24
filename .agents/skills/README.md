# Agent skills

Each skill is a `SKILL.md` playbook under `.agents/skills/<name>/`, with `name` and
`description` front matter and a `.lessons/` directory that `skill-reflection` fills: one
`<unix-timestamp>-<slug>.md` fragment per lesson, read back with `make lessons` and checked by
`make agents-check`. `make find Q="..."` searches skills, lessons, the code's `ge:` tags and the porting map at once.
`.claude/skills` and `.gemini/skills` are symlinks to this directory. `AGENTS.md` is the
router that says which skill to load. `tools/check-agents.sh` checks this list, the
router's Skills table and the directories against each other.

## Session workflow

| Skill | One line |
| --- | --- |
| `start-work` | The preflight: clean tree, sync with main, name the milestone row |
| `write-code` | File shape, naming, the `ge:` discovery tags, data-first design, comments, the edit loop |
| `commit` | Gates, the public-repo check, commit, PR, green then merge |
| `skill-reflection` | Read lessons first; record, fix, prune, merge, promote and re-key at the end |

## Craft

| Skill | One line |
| --- | --- |
| `determinism` | Bits, not closeness: FP flags, the libm rule, number semantics, golden failures |
| `port` | WORKING skill (deleted with `porting/`): carrying a module over from its reference, bottom-up, bit-exact |
| `testing` | The header harness, golden tables, CTest |
| `build` | CMake, presets, pinned dependencies, the CI matrix, workflow hygiene |
| `release` | Changeset fragments, the installed package, cutting a release to GitHub Releases + Packages |
| `sim` | Fall Line's headless simulation: mapgen, the step, the bot, the run clock |
| `platform` | SDL3: window, input, gamepad, audio device, files, focus |
| `move` | Character movement: the knobs and presets, the step, strafe jumping, steps, slopes, crouch, the trace a world answers, the `move-*` labs |
| `input` | Input devices: keyboards, raw mice, gamepads, wheels and pedals, sticks, touch; actions, bindings, rebinding, feedback, the event log |
| `camera` | The camera: poses, followers, the rigs (first person, boom, mounted, orbit, planted, free), shake and comfort, transitions, the stack, the camera labs |
| `gpu` | The native GPU device over Dawn: device, feature tiers, resources, the pipeline manifest, WGSL |
| `render` | The pass table, a pass's two halves, the rules that keep a frame fast, captures and prices |
| `assets` | Authored models: the glTF loader, posing and skinning, the `models` lab, Blender exports |
| `audio` | The synth's DSP graph, the beds, the audio thread, checking a sound by meter |
| `ui` | The 2D layer and text, the bitmap fonts (engine/charset), the HUD and cards, the strings table, the dev UI |
| `labs` | The lab contract: write, run and read a lab; captions, digests, before-and-after |
| `debug` | Log channels and dumps, series traces, the outlier rule, the chart kit, `ge graph` |
| `perf` | Budgets, measuring, the proofs a speed-up owes, the levers |
| `agent-interface` | Diagnostics as prompts, consumer skills, the `ge` CLI |
| `water` | Water and wind: the wind's field and sources, bodies of water, waves, floating, swimming, colour, flow, and their labs |
| `bots` | What a bot decides: `engine/ai`, the decision record, `ge why`, making a bot more human |
| `console` | Cvars and their tiers, cfg files, the console language, binds, the drop-down console, `ge cvar` and `ge cfg` |
| `replay` | Watching a run again: a game's clone, the keyframes, the player's seek, the transport; later the replay file, the director, spectating and the video export |
| `net` | Multiplayer networking: sockets, the simulated network, captures, the wire, channels, nodes, sessions and the lobby, LAN discovery, NAT traversal, port mapping, replication, fake traffic |
| `plugins` | Engine, plugin or game: where code lives, adding a plugin, moving code between homes |

## Plugins

One skill per plugin, named after it (`docs/architecture.md` § Plugins).

| Skill | One line |
| --- | --- |
| `racing` | `plugins/racing`: the control tape, the record book's policy, the standings |
| `mountain` | `plugins/mountain`: the generated ski mountain, its generator and its rule book |
| `skier` | `plugins/skier`: the skier on the snow, its limits, the body and its injuries, the hand-built maps and the ride scenarios |
| `snowmobile` | `plugins/snowmobile`: the snowmobile on the snow, what throws its rider, the bot's hands on it and the sled scenarios |
| `helicopter` | `plugins/helicopter`: the helicopter in the air and on the snow, the downwash, the bot's hands on it and the heli scenarios |
| `paramotor` | `plugins/paramotor`: the powered speed wing a skier flies, the air it flies in, the bot's hands on it and the para scenarios |
