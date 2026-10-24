---
name: ge
description: "Use whenever you build, test, play, run, grow or check this game: ge is the one command line for all of it (find, explain, new, use, add, build, test, check, precheck, ci, digest, golden, lab, graph, net, cvar, cfg, shot, record, play, sim, replay, state, diverge, why, bench, profile, mem, leaks, doctor, upgrade, skills, lessons). Its commands, the --json answer every one gives, where each piece it writes goes, and the loop to work in."
---

# ge, the game's command line

Everything this repo does goes through `ge`: there is no editor and nothing to click. Each
command answers in one shape, and every problem it reports says what failed, where, and the
next action. `ge help` lists the commands; `ge help --json` is the whole catalog (every
command's arguments, flags, an example and its skill), generated from the table the parser
uses, so it is always true for the `ge` you run.

**Which ge.** The pinned release builds its own `ge` into `build/<preset>/ge`. Before the first
build, use a `ge` from a release package (or `make ge-build`); a `ge` of another release run in
this repo hands the command to the pinned one once it is built, so the rules you are held to
are always the pinned release's.

## The loop

```sh
ge find <words>                  # what exists: this repo's and the engine's files, skills, lessons
ge goals free [<row>] --fetch    # the roadmap numbers main and every branch already use, and the next free one
ge explain <file|name>           # one of them: its header comment, declaration, skill, test
ge add system <name>             # sim/<name>.hpp, .cpp and its replay test, to start from
ge add lab <name> --kind=<k>     # a lab (sim, scenario, picture, bench), in the smoke pass
ge add test <name>               # a test file on the harness
ge use <plugin>                  # link racing, mountain or skier, and install its skill
ge build                         # compiler and CMake errors come back as findings
ge test [--filter=<regex>]       # every test and lab smoke; each failure a finding
ge test --affected [--base=<ref>] [--list]  # only the tests the change reaches (the include graph)
ge lab <id> [--tag=before | --compare=before] [-- <its flags>]   # measure and LOOK
ge digest [<lab>...] [--write]   # the labs' digests beside tests/labs_digests.txt; --write moves their rows
ge digest --affected [--list]    # only the labs the change since --base reaches
ge digest --diff                 # every lab, printing only the digests off the baseline
ge golden diff [<test>] [--base=<ref>]   # what moved in each golden file, column by column
ge golden write <test>           # a test's goldens rewritten (GE_GOLDEN_WRITE=1), held, diffed
ge sim [--seeds=1,2]             # the game's pilot plays each seed; input logs in runs/
ge sim --human                   # ... and how human each run looks, warned where a person's band is left
ge play [--seed=1]               # you at the controls: one JSON line each way
ge replay <log>                  # a run played back to its digest
ge state <log> [--step=N]        # everything that decides the next step
ge diverge <log-a> <log-b>       # where two runs part, field by field (skill: play-test)
ge shot [<log>] [--step=N] [--view=<v>]   # a captioned still of any step, drawn headless
ge why <log> [--step=N] [--flips]  # why a bot did it: what it saw, weighed and chose (play-test)
ge record [<log>] [--view=<v>] [--to=30s]  # the run as a video (mp4, webm, gif) and a contact sheet
ge record --gpu --mode=<lit|wireframe|depth|overlay>  # the same through the game's GPU renderer
ge bench [--tag=before | --compare=before]   # each step's time against its budget (skill: perf)
ge profile [--render]            # where the step's (and the frame's) time goes, zone by zone
ge mem | ge leaks                # what the run allocates; what it never frees
ge graph <file>... [--kind=<k>]  # chart traces, benches, profiles: PNG + SVG, outliers as notes
ge net inspect <pcap>            # a network capture, datagram by datagram (skill: multiplayer)
ge net stats <pcap>              # per connection: kbps, round trip, loss, resends; charted
ge net sim [--presets=wifi,bad]  # fake clients and a host over bad links, no game needed
ge cvar ls [<prefix>]            # the game's settings: value, default, range, flags, help
ge cvar get <name>...            # the named ones (an unknown name: the nearest known)
ge cfg check <file>... [--server]   # a cfg's problems at file:line, and names nothing reads
ge cfg exec <file>... [--cmd=...]   # what a cfg changed and printed; ge cfg dump: a cfg to start from
ge asset build [<name>]          # a model built in headless Blender off the game's data (skill: assets)
ge texture kinds                 # every procedural texture layer kind and key (skill: textures)
ge texture check <file.tex>...   # a texture recipe's mistakes, each at its line
ge texture bake <file.tex> --ref="seed=12 wear=0.8"   # one variant to PNG maps in build/textures
ge check                         # the whole gate: format, doctor, build, tests
ge lint [--base=<ref>] [<file>...]  # clang-tidy's bug-finding checks over what the change reaches
ge precheck [--preset=<p>]       # what CI runs, both presets: ge check, the pilot, the replays
ge ci [<pr>] [--log=<file>]      # a red CI job's log as findings (through gh; --log: a saved one)
ge ci <pr> --merge               # squash-merge once every check passed and it merges cleanly (on a merge-queue base it arms auto-merge, and the queue merges it)
ge ci <pr> --wait [--merge]      # block (in the background) until it settles: green, red or conflicted
```

Around it: `ge new <dir>` scaffolds a game repo (`--game=racer` starts it as a racer: a car
round a loop of gates, laps, an autopilot); `ge doctor` is the rules alone; `ge upgrade`,
run with a newer release's ge, moves the pin onto it and syncs its skills (skill: engine);
`ge skills sync` rewrites the engine's skills; `ge lessons <skill>` reads a skill's
lessons before you start. The `debug` skill says how to switch on the log channels for a run
(`-- --log=<spec>`) and how to read what `ge graph` rings.

Build, test and check write the tools' full output to `build/<preset>/ge-*.log` and answer
with findings instead, so a clean run is one line. Read the log only when a finding says to.

## The answer

Every command takes `--json` and then prints exactly one object:

```json
{"command": "ge test", "ok": false, "exit": 1,
 "findings": [{"severity": "error", "what": "level 4.1 after 20 s, want inside (-1, 1)",
               "where": "tests/sim_wind_test.cpp:36", "next": "fix the code or the test at ..."}],
 "written": [], "result": {"preset": "debug", "total": 4, "failed": 1, "log": "..."},
 "summary": "...", "next": ""}
```

- **`exit`**: 0 done, 1 the work found a problem (some finding is an error), 2 a usage error
  (an unknown flag, a missing argument: `ge help <command>` says what it takes).
- **`findings`**: act on each error's `next`. A `note` is information, a `warn` worth a look.
- **`written`**: every file the command changed, so you know what to review and commit.
- **`result`**: the command's own answer (a lab's whole `report.json`, the files `ge find`
  ranked, the catalog for `ge help`).
- **`next`**: on success, the command most likely wanted after this one.

## Rules

- **Use the command, not the tool under it.** `ge test`, not `ctest`; `ge lab`, not the
  `labs` executable; `ge use`, not an edit to `target_link_libraries`. The command knows the
  repo's layout and turns failures into findings.
- **Start from `ge add`.** A template is a small working example with its `ge:` tags filled,
  so the repo builds and passes `ge check` the moment it lands. Rewrite its summary, keywords
  and question for what you are making.
- **`ge check` is the gate.** Green before the first edit (skill: `start-work`) and before
  the commit (skill: `commit`); `ge precheck` before the push is CI's whole job run here, and
  `ge ci <pr>` reads a red job back as findings instead of a scroll through its log.
- **A search that misses is a tag to add.** When `ge find` does not find what you were looking
  for, add the word you tried to the right file's `ge:keywords` (skill: `skill-reflection`).
