# The `ge` backlog

What agents working on the engine and its games said `ge` lacked (AGENTS.md § SAY WHAT `ge`
LACKED), one wish a line under its group: the command, what it would answer, and `done:` with
what was built once it lands. Add a wish as a new line under the group it fits; git merges
this file with its union driver (`.gitattributes`), so threads adding lines side by side never
conflict. Milestone M12d in [`../goals.md`](../goals.md) takes these by group.

## Digests and goldens (done)

- `ge digest <lab> [--write]` (one lab's baseline, not the whole smoke run)
- `ge digest <a> <b> --write` (done: several labs' rows checked or rewritten in one call; the labs executable refuses a second name rather than running only the first)
- `ge golden diff` (which columns of a golden's rows moved) and `ge golden write`
- `ge digest --diff` (done: every lab's digest against the baseline in one command, nothing written, only the labs that moved printed)
- `ge golden diff` pairing rows by their key when the count changes (done: a row inserted mid-table is one row added)
- `ge golden write` listing every golden file it rewrote, and the engine's own tree pointed at it (`build/<preset>/ge golden write <test>`) rather than setting `GE_GOLDEN_WRITE` by hand
- `ge digest-diff --base=<ref> --versions --regions` (which generator versions and regions a change moved)

## Before and after across refs

- `ge bench ab --base=<ref>` (or `--ab <binA> <binB>`: two builds interleaved N times with statistics, since a container's timings wander by about 5%; for a bench lab too: `ge ab <lab> --before=<binary> --runs=3`, the median of each metric) and `ge compare <ref>` (both builds, one table)
- `ge bench --ab=<option>` (one frame option against another in the same build, interleaved rounds, the least median kept: a quality ladder priced without a bespoke loop)
- `ge profile --compare` (per zone and per pass, GPU and CPU deltas, outliers flagged)
- `ge lab bench <lab> --runs=N --base=main` (each metric's median and spread over N runs, this tree beside the base)
- `ge digest <lab> --why` (which table row and which columns moved when a digest did; for a lab that hashes the command catalog or another input, name that input: "cli-loop moved because a command or flag was added")
- `ge lab diff` for a lab's tables and `--stash`
- `ge lab sheet <lab> --grid=2x2 [shots]` (shots on one image) and `--watch`
- `ge profile --pass=<name>` (one pass's median and p95, before and after) and `--cost <shader-term>`
- `ge lab <id> --at-ref=<git-ref>` (the "before" shot from a worktree build of that ref, without stashing, rebuilding and restoring this tree)
- `ge bench --pass=<name>` (one pass timed alone over many draws: the software rasterizer's timestamps misattribute cost between passes)
- `ge lab diff <lab> --with=<flag>` (the same shots with and without one pass or option, the share of pixels it changes per distance band: the water labs each hand-roll it)
- `ge lab diff <lab>` (built: every shot against the kept run, PSNR, SSIM and the share changed per distance band off the frame's depth, each band held to its floor, a four-panel sheet with the change heatmap and the bands' contours)

## The CI loop

- `ge precheck` (done: the repo's CI gates in both presets) and `ge lint --like-ci` (CI's exact gates and file set)
- `ge lint` (done: clang-tidy's bug-finding checks over what the change since `--base` reaches, the changed .cpp files and every .cpp including a changed header, or the files named, each finding at its line with its check's page; a few files in seconds)
- a teleport detector for the labs (done: `engine/lab/teleport.hpp`, every step of a track that moves farther than its speed allows, reported under every scenario lab's `--trace`)
- `ge test --affected` (done: the tests the change since `--base` reaches through the include graph, a target reached when its objects include a changed file or a header in a reached library's directory; only their targets built, `--list` names them; precheck takes its tests from it)
- precheck's lab digests following the same reach (done: `ge digest --affected [--list]`, each lab by its own source's object, so a plugin change reaches the game's labs through the game library including its headers; `tools/labs-reached.sh` retired; a changed committed asset reaches the labs under its plugin or game and those naming it)
- the labs' digest baseline merging without conflicts (done: `merge=union`, a lab's doubled row failed by name)
- `ge digest`'s smoke run writing its previews apart from a full lab run's (`previews/<id>/smoke/`), so a digest check never overwrites the pictures a full run left
- `ge digest --dedupe` (the duplicate rows a union merge leaves in the baseline dropped, keeping the row the current build writes)
- `docs/goals.md` merging without conflicts (done: each row's status in `docs/goals/<row>.md`, this backlog one wish a line, union-merged, held by `tools/check-goals.mjs`)
- `ge ci <pr>` (done: each failed job's log as findings, over `gh api`; `--merge` squash-merges a green, clean one at its head, and a green PR GitHub blocks is answered with the required check or review it waits on)
- `ge test --filter` building only the matching tests' targets (done, and `make test T=<regex>` in the engine)
- lint that also covers the files whose compile command changed (`compile_commands.json` against the base)
- precheck running the smoke digest of every lab the change reaches (done in the engine's `make precheck`; `ge precheck` already runs every lab)
- `labs --all --write-digests` keeping the rows of labs the build does not register (done)
- `ge ci run --row=macos -- <cmd> --repeat=N` (a command on one CI row through a dispatched workflow, no temporary workflow step committed: a Metal flake hunt, the needs-mac bench)
- `ge ci <pr> --rebase-digests` (after merging the base, the digests both sides moved rewritten and named)
- `ge goals next` / `ge goals free-rows` (the next open roadmap row with its dependencies, and which rows open pull requests already claim; the second done: `ge goals free [<row>] [--fetch]`, the rows main and every remote branch hold and the next free number or letter)
- `ge ci <pr> --wait` (block until the checks finish or the PR conflicts, then answer as `ge ci` does: one call where a thread now schedules check-ins every 15 minutes)
- `ge lab sweep <id> --set=<const>=a,b,c` (done: one constexpr rewritten to each value, the labs rebuilt and run, every metric side by side, the source put back; next, a scenario row's own numbers, such as a scripted hand's timings, swept the same way, and `--draws=N` over a range `a..b`, a Monte Carlo of one constant with each metric's spread; and a range form, `--set=<const>=a..b/n`, charting each metric against the knob)
- `ge ci <pr> --wait` (block until the checks finish or the PR conflicts, then answer as `ge ci` does: one call where a thread now schedules check-ins every 15 minutes)
- `ge ci <pr> --wait[=<minutes>]` (done: once a minute until a check fails, the PR conflicts or every check finished, at most an hour; with `--merge` the green answer is merged)
- `ge lab sweep <id> --set=<const>=a,b,c` (done: one constexpr rewritten to each value, the labs rebuilt and run, every metric side by side, the source put back; next, a scenario row's own numbers, such as a scripted hand's timings, swept the same way, and `--draws=N` over a range `a..b`, a Monte Carlo of one constant with each metric's spread)
- `ge lab sweep <id> --flag=<flag>=a,b,c` (done: one of the lab's own flags at each value, the labs built once, the same table; the hours of a day over a picture lab)
- `ge lab --split=N`
- `ge job run\|status\|log` (a long run in the background)
- `ge merge-main`

## Inspecting a run

- `ge record` (done: a run as an MP4, WebM or GIF and a contact sheet of timestamped stills, its resolution, frame rate, speed, range and encoder quality; and `--gpu`, the game's registered renderer drawing each frame headless in a mode, Fall Line's `lit`, `wireframe`, `depth` and `overlay` from a chase camera, a high chase or the named views; `ge shot --gpu` the same still)
- `ge state --entities=<kind> --at=<t>`
- `ge sim --human --compare=<tag>` (the humanness report beside a kept run's, each metric's move)
- `ge sim find <seed> --when "<predicate>"`
- `ge sim --stalls`
- `ge sim --why` (each run's bot decision timeline beside its input log)
- `ge why --compare=<log>` (two logs' bot decisions side by side where they part)
- every scenario lab's per-step trace as a series file `ge graph` charts (done: `--trace` on the `ride`, `sled`, `heli` and `para` labs writes each scenario's every step as `<id>.trace.tsv` and charts it with its events marked and its flights shaded, `engine/lab/series_charts.hpp` the one helper the `trace` lab shares)
- the same scenarios through `ge why --scenario=<lab>:<id> --step=<n>` once a machine pilot records its decisions (a wing's fold, bite and slack)
- `ge sim --outing=<machine> --seed=<n>` with `--trace=<series>` and `--shot` (one free-ride outing traced or pictured without the whole free lab)
- `ge map <seed>` (a seed's weather, fall, sun and day without a lab)
- `--sky=<kind>` on every picture lab (the map's weather overridden)
- `ge log --step --channel`
- `ge probe`
- `ge lab <id> --sunlit` (a warning when a look's pixels sit in the horizon map's shadow, so a shadow change that moves no pixel is caught)
- `ge lab scene <lab> <scene> --plan` (one scene's moment from above without running the whole lab)
- `ge why --agent=1000..1003 --decision=landing` (the decision trace filtered by agent range and decision name)
- `ge lab pick <lab> <shot> --pixel=x,y` (a picture's value at a pixel before the tonemap, the HDR colour and its depth)
- `ge gpu dump <target> --frame=N --decode=<scheme>`
- `ge asset inspect`
- `ge net portmap [--protocol=pcp|natpmp|upnp]` (ask the real home router this machine sits behind to open a port and print each step's answer, then close it: the check the simulated router cannot make)
- `ge net reach --nat=<kind>,<kind>` (one host and client pair through the simulated NATs, the path each attempt took and why, without the whole `net-reach` lab)
- `ge lab refs` (a reference photo fetched to a temp directory, never committed, JPEG turned to PNG, a refs row written; the model-views lab reads only 8-bit PNG)
- `ge lab view <glb>` (the model-views lab on any model, in one command)
- `ge lab table <lab> --where col=value` (one slice of a lab's table, a weapon at one range, read without re-running the lab or opening the whole report)
- `ge replay <log> --seek=<s>[,<s>...]` (any game's replay sought off its keyframes: each seek's digest against the straight run, the steps and ms it took, the keyframes' bytes; the `replay` lab's check for every game repo, so a game's `Run::clone` is proven without writing a lab)
- `ge lab publish <id> [--before=<tag>]` (the run's pictures resized and quantized onto one self-contained HTML page, captions and metrics beside them, before and after side by side, ready to publish as the report's picture page: every thread now owes one, and the water rows each built it by hand)
- `ge lab <id> --show` (the run's pictures shown inline beside its metrics: an unmoved digest can hide a visual bug)
- `ge lab pixels <lab> <shot> --line=x0,y0,x1,y1 | --histogram` (a picture's values along a line or its colour histogram, for banding and colour checks without decoding the PNG)
- `ge lab strip <scenario>` (a side-view stick filmstrip of a body over time, frame by frame, without a hand-written dumper and image script)
- `ge lab trace-points <scenario> --stage` (each point of a body's velocity after every solver stage, for finding an energy pump without instrumenting a copy of the solver; generalizes to `ge trace --actor=<id> --fields=<a,b>`, and to `ge trace-view <lab> <scenario> --around=<step>`: the steps round one as a table of position, velocity, grounded and events)

## Measuring

- `ge profile --wall` (a sampling CPU profiler: wall time by inlined frame, the main thread apart from the workers, where instruction counts hide a slow call)
- `ge profile --callgrind <lab>`
- `ge lab <id> --toggle=<feature>` (a lab's passes timed with the feature on and off, one table)
- `ge issue needs-mac --bench=<invocation>` (the needs-mac issue written with the exact GPU bench command and what to paste back, after trying CI's macOS runner on Metal first, since software-rendered timings say little about real cost)
- `ge mem --where` (the allocating call stack)
- `ge build --flags`
- `ge spans <lab>` (a lab's spans file read back as a table)
- `ge sanitize thread <tests>` (a ThreadSanitizer build of the tests that run threads)
- `ge lab time <id> [--smoke]` (a lab's sections timed, and its smoke run, so a lab that slows CI is found by name)
- `ge bench --preset=release <lab>` (only the labs target built in release, for a lab's release-build cost without a whole release build)
- a bench lab warns when the machine is loaded (load average over the cores: a concurrent ctest made thread starts look ten times slower)
- `ge tsan <test>` (one test and its sources built with ThreadSanitizer, without a whole sanitizer preset)
- `ge load --tag=<name>` / `--compare=<name>` (two load runs' efficiency, lanes' shares and the median frame's bound side by side, so a threading change proves what it spread)
- `ge load --frame=<n>` (the lanes chart zoomed round one frame, the slowest by default, with its critical path as a table)
- `ge load --live` (the load monitor over the game's own window as a player drives it, once the desktop game steps by hand: M19c)

## Hygiene

- `ge build` and `ge lab` warning when the labs binary is older than a library it links
- `ge deps fetch`
- `ge lint --explain`
- the software Vulkan driver's leaks suppressed under `ge leaks`
- GPU labs on no adapter reported as skipped
- `ge add lab --kind=overlay`
- `ge doctor --tools` / `ge env` (which tools, versions and drivers a gate needs, and which are missing)

(`ge shot` is M12b's; `ge anim` is M14b's.)

## Shooter's Edge, the FPS port (M18: the ge tooling it needs)

- `ge pak ls|cat|extract|build` (search paths, which archive wins a file, fixture packs: pak, pk3, .pk3dir)
- `ge bsp info|ents|shot` (lumps and limits, the entity lump as text, a top-down CPU picture of the leaves, the PVS from a point, the hulls)
- `ge progs build|disasm|run|trace|profile|diff` (the engine's own compiler for the game's VM language, and the `progs` VM inspected)
- `ge server --map --bots --for` (a headless dedicated server with bots, its input log written for replay, `ge state` and diverge)
- `ge demo info|frames|shot|diff` (the classic demo formats decoded, a frame pictured, two demos diffed to their first difference)
- `ge net inspect --protocol=<name>` (pluggable protocol decoders over a capture: `--protocol=classic`)
- `ge pmove <scenario>` (movement trajectories as tables or charts against a recording)
- `ge console <target> "<cmd>"` and `--pid=<p>` (console commands and cvars on a headless server or client, or a running game, the output read back)
- the play contract's gaps in `engine/play`: an angle axis kind on a finer grid (16-bit angles), a step whose length the input sets (a usercmd's msec), several players a Run

## Networking (M17)

- `ge net portmap open|renew|close` (a real port mapping from the command line, the OS's gateway and LAN address found: a real-router check on the Mac in one line)
- `ge net reach --host --client --preset --forward` (one pair of peers' reach timeline)
## Shooter's Edge (M18, docs/shooters-edge.md § The `ge` tooling it needs)

- `ge pak ls/cat/extract/build` (a search path's files, which archive wins one, a fixture pack built from a directory)
- `ge bsp info/ents/shot` (a map's lumps and limits, its entity lump, a CPU picture of leaves, the PVS from a point and the hulls)
- `ge qc build/disasm/run/trace/profile/diff` (our QC compiler; a progs disassembled, run, traced a step at a time, timed by function, diffed against another build)
- `ge server --map --bots --for` (a headless dedicated server with bots, writing an input log for `ge replay`, `ge state` and `ge diverge`)
- `ge demo info/frames/shot/diff` (a client or MVD demo decoded frame by frame, a frame pictured, two demos to their first difference)
- `ge net inspect --protocol=<name>` (pluggable decoders, so a capture of the classic protocol reads as its messages)
- `ge pmove <scenario>` (a movement scenario's trajectory as a table and a chart, against its parity recording)
- `ge console <target> "<command>"` (a console command or cvar run in a headless server or client, its output returned)
- `ge play` for first-person games (an angle axis on the protocol's grid, a step whose length the input sets, several players a run)

## Store services

- `ge services check` (a game's achievements and stats catalog checked: ids unique, every stat an achievement reads defined; later diffed against a store's exported partner config)

## Input devices

- `ge input replay <events-log> --bindings=<file>` (each action's value frame by frame, from a recorded event log under a binding set)
- `ge input bindings check <file>` (a bindings file parsed against the game's actions: conflicts and unbound actions listed)
