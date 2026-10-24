// The catalog's rows, the command-line split, and both forms of the help.
//
// ge:summary The ge catalog's rows and the parser and help printers that read them
// ge:keywords cli ge commands catalog rows parse_args check_args help_text help_json flags
// ge:skill agent-interface
// ge:test tests/cli_commands_test.cpp
#include "cli/commands.hpp"

#include <algorithm>

#include "cli/embedded.hpp"
#include "cli/json.hpp"

namespace ge::cli {

namespace {

// A flag's value as help prints it: "=<dir>", "[=<minutes>]" for an optional one ("[<minutes>]"),
// or nothing for a switch.
std::string flag_value_text(std::string_view value) {
  if (value.empty()) return "";
  if (value[0] == '[') return "[=" + std::string(value.substr(1));
  return "=" + std::string(value);
}

constexpr FlagSpec kRepo{"repo", "<dir>", "the game repo (default: the current directory)"};
constexpr FlagSpec kEngine{"engine", "<dir>",
                           "the engine's source tree (default: the pinned release's, once "
                           "built, or the tree this ge was built from)"};
constexpr FlagSpec kPreset{"preset", "<debug|release>",
                           "the CMake preset (default debug: sanitizers on)"};
constexpr FlagSpec kDryRun{"dry-run", "", "say what would be written, write nothing"};

constexpr FlagSpec kNewFlags[] = {
    {"name", "<kebab>", "the game's name (default: the directory's)"},
    {"engine-sha256", "<hex>", "the release's -src.tar.gz checksum, pinned with it"},
    {"game", "<demo|racer>",
     "the game it starts as: demo (a body on a field) or racer (a car round a loop of gates)"},
};
constexpr FlagSpec kRepoOnly[] = {kRepo};
constexpr FlagSpec kUpgradeFlags[] = {
    {"engine-sha256", "<hex>", "the new release's -src.tar.gz checksum, pinned with it"}, kRepo};
constexpr FlagSpec kSyncFlags[] = {{"force", "", "rewrite engine skills edited by hand"}, kRepo};
constexpr FlagSpec kLessonsFlags[] = {
    {"scope", "<path>", "only lessons whose scope overlaps this path"}, kRepo};
constexpr FlagSpec kFindFlags[] = {
    kRepo, kEngine, {"limit", "<n>", "the most files to list (default 15)"}};
constexpr FlagSpec kExplainFlags[] = {kRepo, kEngine};
constexpr FlagSpec kAddFlags[] = {kRepo, kDryRun};
constexpr FlagSpec kAddLabFlags[] = {
    {"kind", "<sim|scenario|picture|bench>", "the lab's kind (default sim; skill: labs)"},
    kRepo,
    kDryRun};
constexpr FlagSpec kBuildFlags[] = {kPreset, kEngine, kRepo};
constexpr FlagSpec kTestFlags[] = {
    kPreset,
    {"filter", "<regex>", "only the tests whose names match, and only their targets built"},
    {"affected", "", "only the tests the change since --base reaches, and only their targets"},
    {"base", "<ref>", "with --affected, the ref the change is since (default origin/main)"},
    {"list", "", "with --affected, name the tests and run nothing"},
    kEngine,
    kRepo};
constexpr FlagSpec kLabFlags[] = {
    kPreset,
    {"smoke", "", "run at the lab's smoke flags"},
    {"tag", "<name>", "keep this run under previews/<id>@<name>/ for a later --compare"},
    {"compare", "<name>", "set every metric beside the run tagged <name>"},
    kEngine,
    kRepo};

constexpr FlagSpec kLabSweepFlags[] = {
    kPreset,
    {"set", "<name>=<a>,<b>,...",
     "the constexpr constant to sweep (kSlope, or spec::kSlope) and the values it takes"},
    {"in", "<file>", "the source file it is defined in, when its name is in several"},
    {"flag", "<flag>=<a>,<b>,...", "sweep one of the lab's own flags instead (no rebuild)"},
    {"metrics", "<word,...>", "list only the metrics whose keys hold one of these words"},
    kEngine,
    kRepo};

constexpr FlagSpec kLabDiffFlags[] = {
    kPreset,
    {"against", "<name>", "the kept run to diff against (default before: --tag=before)"},
    {"run", "", "run the lab first (its own flags after --), then diff; else diff its last run"},
    {"bands", "<m,m,...>", "the distance bands' edges (default 25,100,400)"},
    {"psnr", "<dB,dB,...>",
     "each band's floor, nearest first and the sky last (default "
     "40,36,32,28,28)"},
    kEngine,
    kRepo};
constexpr FlagSpec kPrecheckFlags[] = {
    {"preset", "<debug|release>", "only this preset (default: both, as CI runs them)"},
    kEngine,
    kRepo};
constexpr FlagSpec kCiFlags[] = {
    {"log", "<file>", "read this saved job log instead of asking GitHub"},
    {"merge", "", "squash-merge the pull request when every check passed and it merges cleanly"},
    {"wait", "[<minutes>]",
     "block until the checks finish, one fails or it conflicts (at most 60 minutes), then answer"},
    kRepo};
constexpr FlagSpec kDigestFlags[] = {
    {"write", "", "rewrite the lab's row of tests/labs_digests.txt (every row with no lab)"},
    {"diff", "", "print only the labs whose digest is off the baseline; write nothing"},
    {"affected", "", "the labs the change since --base reaches, each by its own source's object"},
    {"base", "<ref>", "with --affected: the change since this ref (default origin/main)"},
    {"list", "", "with --affected: name the labs it picks and run nothing"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kLintFlags[] = {
    {"base", "<ref>", "lint what the change since this ref reaches (default origin/main)"},
    {"all", "", "lint every .cpp"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kGoalsFlags[] = {
    {"base", "<ref>", "the roadmap rows are already taken on (default origin/main)"},
    {"fetch", "", "git fetch origin first, so every branch is current"},
    kRepo};
constexpr FlagSpec kGoldenDiffFlags[] = {
    {"base", "<ref>", "the git ref to compare with (default HEAD)"}, kRepo};
constexpr FlagSpec kGoldenWriteFlags[] = {
    {"preset", "<debug|release>",
     "the CMake preset (default release: a golden's full run is the release build's)"},
    kEngine,
    kRepo};

constexpr FlagSpec kPlayFlags[] = {
    {"seed", "<n>", "the run's seed (default 1)"}, kPreset, kEngine, kRepo};
constexpr FlagSpec kServerFlag{"server", "",
                               "a dedicated server's console: the server's cvars may be set"};
constexpr FlagSpec kCvarFlags[] = {kServerFlag, kPreset, kEngine, kRepo};
constexpr FlagSpec kCfgFlags[] = {
    kServerFlag,
    {"cmd", "<commands>", "exec: more commands run after the files (`r_bloom 0; echo $r_bloom`)"},
    {"out", "<file>", "dump: write the cfg here instead of printing it"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kSimFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot runs (default 1)"},
    {"human", "",
     "measure how human each run's controls and decisions look, and warn on every metric "
     "outside a person's band (skill: bots)"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kFast{
    "preset", "<debug|release>",
    "the CMake preset (default release: a sanitizer's time is not the game's)"};
constexpr FlagSpec kSteps{"steps", "<n>", "only the first this many steps of each run"};
constexpr FlagSpec kBenchFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot plays (default 1)"},
    {"repeats", "<n>", "times each run is replayed and timed (default 5)"},
    kSteps,
    {"tag", "<name>", "keep the answer as runs/bench@<name>.json for a later --compare"},
    {"compare", "<name>", "set every number beside the run tagged <name>; 10% slower warns"},
    kFast,
    kEngine,
    kRepo};
constexpr FlagSpec kProfileFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot plays (default 1)"},
    {"repeats", "<n>", "times each run is replayed and timed (default 5)"},
    kSteps,
    {"render", "", "also run the game's `profile` lab: CPU and GPU ms per render pass"},
    kFast,
    kEngine,
    kRepo};
constexpr FlagSpec kMemFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot plays (default 1)"}, kSteps, kFast, kEngine, kRepo};
constexpr FlagSpec kLoadFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot plays (default 1)"},
    {"steps", "<n>", "only the first this many steps of each run (default 1200)"},
    {"paced", "<0|1>", "run each step at its slot on the wall clock, as a game's loop does (1)"},
    {"bin-ms", "<ms>", "the busy chart's bin (default 10)"},
    {"render", "", "also run the game's `gpu-load` lab: the frame loop's threads and the GPU"},
    kFast,
    kEngine,
    kRepo};
constexpr FlagSpec kLeakFlags[] = {
    {"seeds", "<a,b,...>", "the seeds the pilot plays (default 1)"},
    {"valgrind", "", "run under valgrind's memcheck instead of the sanitizers"},
    {"preset", "<debug|release>", "the CMake preset (default debug: its sanitizers find leaks)"},
    kEngine,
    kRepo};
constexpr FlagSpec kAssetBuildFlags[] = {
    {"blender", "<cmd>",
     "the Blender, or a Python with bpy (default: GE_BLENDER, `blender`, a python3 with bpy)"},
    {"force", "", "with no names, rebuild every asset, fresh or not"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kStateFlags[] = {
    {"step", "<n>", "after this many steps (default: the log's end)"}, kPreset, kEngine, kRepo};
constexpr FlagSpec kWhyFlags[] = {
    {"agent", "<n>",
     "the bot to explain: 0 the player's pilot, then the game's others (default 0)"},
    {"step", "<n>", "the step to explain (default: the log's last)"},
    {"around", "<k>", "and the k steps either side of it (default 0)"},
    {"decision", "<name>", "only this decision (default: every one)"},
    {"flips", "", "every step the bot changed its mind over the whole run"},
    {"close", "<x>", "with --flips, also the close calls: margins under x"},
    {"timeline", "", "write its decisions as a series file in runs/ for ge graph"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kShotFlags[] = {
    {"seed", "<n>", "without a log: the pilot plays this seed first (default 1)"},
    {"step", "<n>", "after this many steps (default: the run's end)"},
    {"view", "<name>", "which picture (default: the game's first; ge shot lists them)"},
    {"gpu", "", "the game's renderer draws the view on the GPU (ge shot --gpu lists its views)"},
    {"mode", "<lit|wireframe|depth|overlay>",
     "with --gpu: how the view is drawn (default: the renderer's first, lit)"},
    {"size", "<WxH>", "with --gpu: the picture's size (default: the renderer's own)"},
    kPreset,
    kEngine,
    kRepo};
constexpr FlagSpec kRecordFlags[] = {
    {"seed", "<n>", "without a log: the pilot plays this seed first (default 1)"},
    {"view", "<name>", "which picture (default: the game's first; ge shot lists them)"},
    {"gpu", "", "the game's renderer draws the view on the GPU (ge shot --gpu lists its views)"},
    {"mode", "<lit|wireframe|depth|overlay>",
     "with --gpu: how the view is drawn (default: the renderer's first, lit)"},
    {"from", "<step|Ns>", "the first frame: a step, or seconds with an s (default 0)"},
    {"to", "<step|Ns>", "the last frame (default: the run's end)"},
    {"fps", "<n>", "frames a second of video (default 30)"},
    {"speed", "<x>", "the run's pace on screen: 2 twice as fast, 0.25 slow motion (default 1)"},
    {"size", "<WxH>", "the frame size (default: the view's own)"},
    {"format", "<mp4|webm|gif|png>", "H.264 MP4, VP9 WebM, a GIF, or PNG frames (default mp4)"},
    {"codec", "<name>", "an ffmpeg encoder in place of the format's own (libx264, libvpx-vp9)"},
    {"crf", "<n>", "quality: lower is better and larger (default 23 for mp4, 32 for webm)"},
    {"bitrate", "<rate>", "a bit rate such as 4M in place of --crf"},
    {"encoder-preset", "<name>",
     "H.264's speed against size: ultrafast .. veryslow (default medium)"},
    {"thumbs", "<n>", "the contact sheet's stills, 0 for none (default 24)"},
    {"columns", "<n>", "the contact sheet's columns (default 6)"},
    {"band", "<0|1>", "the time, step and view burned into each frame (default 1)"},
    {"preset", "<debug|release>",
     "the build to record with (default release: a frame is drawn on the CPU)"},
    kEngine,
    kRepo};
constexpr FlagSpec kGraphFlags[] = {
    {"kind", "<auto|line|scatter|hist|box|heat|spans>",
     "the chart (default auto: spans for a spans file, line for the rest)"},
    {"columns", "<a,b>", "only these columns (default: every numeric column but x)"},
    {"x", "<column>", "the x column (default: the file's `# x:` or its first numeric column)"},
    {"out", "<dir>", "where the PNGs and SVGs go (default: graphs/ beside the first file)"},
    {"title", "<text>", "a title before each chart's own"},
    {"log", "", "a log scale for the y axis (line and scatter)"}};

constexpr FlagSpec kNetInspectFlags[] = {
    {"peer", "<address>", "only the datagrams to or from it, as the lines print it (sim:3:1)"},
    {"type", "<request|challenge|response|accept|deny|payload|disconnect>",
     "only packets of this type"},
    {"skip", "<n>", "skip the first n that match"},
    {"limit", "<n>", "show at most n (default 200; 0: every one)"}};
constexpr FlagSpec kNetStatsFlags[] = {
    {"bucket", "<ms>", "the series' step (default 250)"},
    {"out", "<dir>", "where the series and its charts go (default: <capture>-net/ beside it)"},
    {"no-charts", "", "write the series file, draw no charts"}};
constexpr FlagSpec kNetSimFlags[] = {
    {"presets", "<a,b>",
     "the link conditions, one session each (default every preset; none: a "
     "perfect link)"},
    {"mix", "<racing|action|rpg|world|lockstep|download>", "the traffic's shape (default racing)"},
    {"transport", "<udp|tcp>", "datagrams, or every link a stream (default udp)"},
    {"budget", "<kbit/s>", "cap what every connection sends (default none)"},
    {"link", "<k=v,...>",
     "override conditions on every preset: latency, jitter (ms), loss, duplicate, reorder "
     "(shares or %), burst, reorder-ms, bandwidth (kbit/s), queue (ms), drop-every and "
     "drop-run (a pattern: drop-every=2 loses every other packet)"},
    {"clients", "<n>", "fake clients (default 8)"},
    {"max-peers", "<n>", "the host's slots, 1..32 (default 32); clients past it are denied"},
    {"seconds", "<s>", "how long they send (default 10)"},
    {"drain", "<s>", "then how long the links drain (default 4)"},
    {"tick-hz", "<hz>", "how often every node updates (default 60)"},
    {"seed", "<n>", "the network's and the nodes' seed (default 1)"},
    {"capture", "<file.pcap>", "write the host's capture (one a preset, named after it)"}};

constexpr FlagSpec kNetWatchFlags[] = {
    {"port", "<n>", "the loopback port the game's tap sends to (default 27950)"},
    {"seconds", "<s>", "how long to watch (default 10; 0: until --frames)"},
    {"every", "<s>", "a live line a connection on stderr this often (default 1)"},
    {"frames", "<n>", "stop after this many datagrams (default no limit)"},
    {"save", "<file.pcap>", "where what was seen is saved (default runs/net-watch.pcap)"}};

constexpr FlagSpec kNetSweepFlags[] = {
    {"knob", "<name>",
     "what to sweep: update-rate, cmd-rate, rate (bytes/s), interp (ticks), backup (inputs "
     "repeated), tick-hz, or a link key (latency, jitter, loss, burst, reorder, bandwidth, "
     "drop-every, drop-run)"},
    {"values", "<a,b,...>", "the knob's values, one trial each (a share may be a percent)"},
    {"presets", "<a,b>", "the link conditions (default wifi; none: a perfect link)"},
    {"link", "<k=v,...>", "override conditions on every preset (as ge net sim)"},
    {"clients", "<n>", "clients, each steering its own mover, 1..32 (default 16)"},
    {"npcs", "<n>", "movers the server moves (default 32)"},
    {"seconds", "<s>", "how long each trial runs (default 10)"},
    {"tick-hz", "<hz>", "the server's and clients' tick (default 60)"},
    {"update-rate", "<n>", "snapshots a second each client asks for (default every other tick)"},
    {"cmd-rate", "<n>", "input messages a second each client sends (default one a tick)"},
    {"rate", "<bytes/s>", "snapshot bytes a second each client takes (default no cap)"},
    {"interp", "<ticks>", "how far behind the newest snapshot others are drawn (default 6)"},
    {"backup", "<n>", "inputs repeated in every input message (default 4)"},
    {"seed", "<n>", "the network's, the hands' and the crowd's seed (default 1)"}};

const CommandSpec kCommands[] = {
    {"help",
     "[<command>]",
     0,
     -1,
     "learn",
     "the command catalog, or one command's help",
     {},
     "ge help add lab --json",
     "ge"},
    {"version",
     "",
     0,
     0,
     "learn",
     "the engine release this ge belongs to",
     {},
     "ge version",
     "engine"},
    {"find", "<word>...", 1, -1, "learn",
     "the files, skills and lessons that match any word, ranked: the repo's and the pinned "
     "engine's",
     kFindFlags, "ge find heightfield bilinear", "ge"},
    {"explain", "<file|symbol|word>", 1, 1, "learn",
     "what a file or a function is for: its header comment, its declaration, its skill and test",
     kExplainFlags, "ge explain core/prng.hpp", "ge"},
    {"lessons", "[<skill>]", 0, 1, "learn",
     "the skills' lessons: the inventory, or one skill's in full", kLessonsFlags,
     "ge lessons labs --scope=sim/", "skill-reflection"},
    {"goals free", "[<row>]", 0, 1, "learn",
     "the roadmap rows taken on the base and on every remote branch, and the next free number "
     "(or, for a row such as M17, the next free letter under it)",
     kGoalsFlags, "ge goals free --fetch", "commit"},
    {"new", "<dir>", 1, 1, "create",
     "a game repo against this engine release: the simulation, a lab, a test, the skills",
     kNewFlags, "ge new snow-racer --game=racer", "ge"},
    {"use", "<plugin>", 1, 1, "create",
     "link one of the engine's plugins into the game and install its skill", kRepoOnly,
     "ge use racing", "engine"},
    {"add system", "<name>", 1, 1, "create",
     "a simulation system (sim/<name>.hpp, .cpp) with a test that holds its bits", kAddFlags,
     "ge add system wind", "sim-system"},
    {"add lab", "<name>", 1, 1, "create", "a lab of one kind, registered and in the smoke pass",
     kAddLabFlags, "ge add lab wind --kind=sim", "labs"},
    {"add test", "<name>", 1, 1, "create", "a test file on the repo's harness", kAddFlags,
     "ge add test wind", "sim-system"},
    {"build", "", 0, 0, "build",
     "configure and build; compiler and CMake errors come back as findings", kBuildFlags,
     "ge build", "ge"},
    {"test", "", 0, 0, "build",
     "build, then run the tests and every lab's smoke pass; each failure is a finding", kTestFlags,
     "ge test --filter=sim", "ge"},
    {"check", "", 0, 0, "build",
     "the whole gate in one call: format, doctor, build, tests and labs", kBuildFlags,
     "ge check --json", "commit"},
    {"lint", "[<file>...]", 0, -1, "build",
     "clang-tidy's bug-finding checks over what the change since --base reaches (or the files "
     "named, or --all), each finding at its file:line with its check",
     kLintFlags, "ge lint --base=origin/main", "commit"},
    {"precheck", "", 0, 0, "build",
     "what CI runs, here, before the push: ge check, then the pilot's seeds and their replays, "
     "on each preset",
     kPrecheckFlags, "ge precheck --preset=release", "commit"},
    {"ci", "[<pr>]", 0, 1, "build",
     "a pull request's state: whether it conflicts with its base, and each failed job's log "
     "read back as findings (through gh, or a saved log); --merge squash-merges it once green",
     kCiFlags, "ge ci 12", "commit"},
    {"digest", "[<lab>...]", 0, -1, "build",
     "the labs named (or every lab) at their smoke flags, twice: each digest beside the "
     "committed baseline; --write moves only those labs' rows; --diff prints only the labs "
     "that moved",
     kDigestFlags, "ge digest wind-sweep level --write", "labs"},
    {"golden diff", "[<golden|test>...]", 0, -1, "build",
     "each golden file against git's --base: the rows that changed and, column by column, what "
     "moved in them",
     kGoldenDiffFlags, "ge golden diff sim_game_test --base=origin/main", "sim-system"},
    {"golden write", "<test>", 1, 1, "build",
     "rewrite a test's golden files (the test run with GE_GOLDEN_WRITE=1), hold them, and show "
     "what moved",
     kGoldenWriteFlags, "ge golden write sim_wind_test", "sim-system"},
    {"lab", "[<id>]", 0, 1, "see",
     "list the labs, or run one: its metrics, tables, captioned pictures and digest; its own "
     "flags go after --",
     kLabFlags, "ge lab run --json -- --seeds=1,2", "labs"},
    {"lab sweep", "<id>", 1, 1, "see",
     "one constexpr constant of the code rewritten to each value, the labs rebuilt and the lab "
     "run at each, every metric side by side; the source is put back after",
     kLabSweepFlags, "ge lab sweep para --set=kFlight=0.5,0.7,0.9 -- --scenario=launch", "labs"},
    {"lab diff", "<id>", 1, 1, "see",
     "diff every picture of a lab's last run against a kept run: PSNR, SSIM and the share "
     "changed per distance band, each band held to its floor, a four-panel sheet a shot",
     kLabDiffFlags, "ge lab diff world --run -- --views=start,vista", "labs"},
    {"graph", "<file>...", 1, -1, "see",
     "chart series or spans files (a trace, a bench, a profile) to PNG and SVG; several files "
     "overlay; every outlier comes back as a note",
     kGraphFlags, "ge graph runs/before.tsv runs/after.tsv --columns=step_ms", "debug"},
    {"net inspect", "<pcap>", 1, 1, "see",
     "a capture decoded datagram by datagram: time, addresses, packet type, sequence, acks, "
     "each message's channel, id and size",
     kNetInspectFlags, "ge net inspect runs/net.pcap --peer=sim:3:1 --type=payload", "multiplayer"},
    {"net stats", "<pcap>", 1, 1, "see",
     "a capture measured per connection from the capturing node: packets and kbps each way, "
     "round trip, loss each way, duplicates, reorders, resends; kbps, rtt and loss over time "
     "charted",
     kNetStatsFlags, "ge net stats previews/net-link/bad.pcap", "multiplayer"},
    {"shot", "[<log>]", 0, 1, "see",
     "a still of a run at a step, drawn headless and captioned (seed, options, step, digest): "
     "a log played to --step, or the pilot's run of --seed; the game's options go after --",
     kShotFlags, "ge shot runs/fall_line-38.inputs --step=1200 --view=near", "play-test"},
    {"record", "[<log>]", 0, 1, "see",
     "a run as a video and a contact sheet: a log (or the pilot's run of --seed) played back, a "
     "view drawn at each frame, encoded by ffmpeg (mp4, webm, gif) or kept as PNG frames, and a "
     "grid of stills each with its time and step; the game's options go after --",
     kRecordFlags, "ge record runs/fall_line-38.inputs --view=near --to=30s --size=720x720",
     "play-test"},
    {"play", "", 0, 0, "play",
     "play the game over the line protocol on this terminal (observe, act, pilot, state, "
     "inputs, end); the game's options go after --",
     kPlayFlags, "ge play --seed=38 -- --rivals=3", "play-test"},
    {"sim", "", 0, 0, "play",
     "the game's pilot over each seed: each run's report and digest, its input log in runs/; "
     "--human measures how human it looks; the game's options go after --",
     kSimFlags, "ge sim --seeds=38,7", "play-test"},
    {"replay", "<log>", 1, 1, "play",
     "an input log played back; a digest it misses is a determinism break", kBuildFlags,
     "ge replay runs/fall_line-38.inputs", "play-test"},
    {"state", "<log>", 1, 1, "play",
     "everything that decides the next step, after a number of a log's steps", kStateFlags,
     "ge state runs/fall_line-38.inputs --step=600", "play-test"},
    {"diverge", "<log-a> <log-b>", 2, 2, "play",
     "where two runs part: the first input that differs, the first step after which the states "
     "differ, and the fields that do",
     kBuildFlags, "ge diverge runs/a.inputs runs/b.inputs", "play-test"},
    {"why", "<log>", 1, 1, "play",
     "why a bot did what it did: its decisions round a step replayed from the log (what it saw, "
     "the options it weighed and their scores, the one it took and by what margin); --flips for "
     "every change of mind, --timeline for a chart",
     kWhyFlags, "ge why runs/fall_line-38.inputs --step=1200 --around=6", "play-test"},
    {"cvar ls", "[<prefix>]", 0, 1, "see",
     "the game's cvars (the settings a cfg may change): value, default, range, flags (A "
     "archived, S server info, V the server's, C cheat, L latched) and what each does",
     kCvarFlags, "ge cvar ls r_", "play-test"},
    {"cvar get", "<name>...", 1, -1, "see",
     "the named cvars, each with its default, range, flags and help; an unknown name gets the "
     "nearest one",
     kCvarFlags, "ge cvar get r_bloom sv_seed", "play-test"},
    {"cfg check", "<file>...", 1, -1, "build",
     "cfg files run in a fresh console of the game: every unknown command, refused set and name "
     "nothing reads a finding at its file:line",
     kCfgFlags, "ge cfg check autoexec.cfg", "play-test"},
    {"cfg exec", "<file>...", 0, -1, "see",
     "cfg files run in a fresh console of the game: what they changed, what they printed, and "
     "their problems; --cmd runs more after them",
     kCfgFlags, "ge cfg exec autoexec.cfg --cmd=\"cvarlist r_\"", "play-test"},
    {"cfg dump", "", 0, 0, "create",
     "every setting a player may change, at its default, each commented with what it does: "
     "the cfg to start one's own from",
     kCfgFlags, "ge cfg dump --out=my.cfg", "play-test"},
    {"net sim", "", 0, 0, "measure",
     "fake-traffic clients and a host over chosen link conditions, no game needed: delivery, "
     "round trip, loss, bandwidth and a digest a session; --capture for the pcap",
     kNetSimFlags, "ge net sim --presets=wifi,bad --mix=action --clients=16", "multiplayer"},
    {"net watch", "", 0, 0, "see",
     "a running game's or server's datagrams from its live tap (net/tap.hpp) on a loopback "
     "port: each connection's packets, kbps, round trip and loss every second, then the whole "
     "watch saved as a capture and measured as ge net stats measures it",
     kNetWatchFlags, "ge net watch --port=27950 --seconds=30", "multiplayer"},
    {"net sweep", "", 0, 0, "measure",
     "a replicated world run once a value of one knob (a rate, the interpolation, a link "
     "condition or a drop pattern): bandwidth each way, view delay, corrections, rewind gaps, "
     "and the cheapest value within reach of the best",
     kNetSweepFlags, "ge net sweep --knob=update-rate --values=10,20,30,60 --presets=wifi,bad",
     "multiplayer"},
    {"bench", "", 0, 0, "measure",
     "each step's time against its budget (median, p95, p99, worst), steps a second and the "
     "run's open, over the pilot's runs replayed; --tag and --compare for a before and after",
     kBenchFlags, "ge bench --tag=before -- --rivals=3", "perf"},
    {"profile", "", 0, 0, "measure",
     "where the step's time goes: each timing zone's calls, time and share, the time in no zone, "
     "the slowest steps; a timeline round the slowest; --render adds the frame's passes",
     kProfileFlags, "ge profile --seeds=38 --render", "perf"},
    {"mem", "", 0, 0, "measure",
     "what the run allocates: opening it, each step, the steps after the first second that "
     "still allocate and the zone they do it in; the peak resident set",
     kMemFlags, "ge mem --seeds=38", "perf"},
    {"load", "", 0, 0, "measure",
     "how well the run uses the machine: every thread's work, spin and sleep, every OS thread's "
     "and core's CPU share, threads working at once, each step's critical path; charts and a "
     "lanes file; --render adds the frame loop and the GPU",
     kLoadFlags, "ge load --seeds=38 --render", "perf"},
    {"leaks", "", 0, 0, "measure",
     "the pilot's run under the leak checker (the debug build's sanitizers, or valgrind); each "
     "block never freed a finding at its first frame in the repo",
     kLeakFlags, "ge leaks --seeds=38", "perf"},
    {"asset list", "", 0, 0, "see",
     "the repo's built assets (each assets/<name>/asset.txt): the model, its size against its "
     "budget, and whether it is fresh against its sources",
     kBuildFlags, "ge asset list --json", "assets"},
    {"asset build", "[<name>...]", 0, -1, "create",
     "build models in headless Blender from their Python builders and the game's data, then "
     "stamp and check each one; no names builds every stale one",
     kAssetBuildFlags, "ge asset build skier", "assets"},
    {"asset check", "", 0, 0, "build",
     "every committed model against its stamp (older than its sources?) and its size budget, "
     "and loaded; each problem a finding",
     kBuildFlags, "ge asset check", "assets"},
    {"doctor", "", 0, 0, "maintain",
     "the repo against the engine's rules: guidance, skills, the pin, flags, sources, messages",
     kRepoOnly, "ge doctor", "ge"},
    {"skills sync", "", 0, 0, "maintain", "the engine's skills, rewritten to this engine release",
     kSyncFlags, "ge skills sync", "engine"},
    {"upgrade", "", 0, 0, "maintain",
     "move the repo onto this ge's engine release: the pin rewritten, the skills synced, the repo "
     "doctored; run it with the new release's ge",
     kUpgradeFlags, "ge upgrade --engine-sha256=<hex>", "engine"},
};

constexpr FlagSpec kGlobal[] = {
    {"json", "",
     "answer as one JSON object (command, ok, exit, findings, written, result, "
     "summary, next)"},
    {"help", "", "this command's help"},
};

const FlagSpec* find_flag(const CommandSpec& c, std::string_view name) {
  for (const FlagSpec& f : c.flags) {
    if (f.name == name) return &f;
  }
  for (const FlagSpec& f : kGlobal) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

std::string usage_line(const CommandSpec& c) {
  std::string line = "ge " + std::string(c.name);
  if (!c.args.empty()) line += " " + std::string(c.args);
  for (const FlagSpec& f : c.flags) {
    line += " [--" + std::string(f.name) + flag_value_text(f.value) + "]";
  }
  return line;
}

std::string flag_json(const FlagSpec& f) {
  return json_obj({{"name", json_str("--" + std::string(f.name))},
                   {"value", f.value.empty() ? "null" : json_str(f.value)},
                   {"help", json_str(f.help)}});
}

std::string command_json(const CommandSpec& c) {
  std::vector<std::string> flags;
  for (const FlagSpec& f : c.flags) flags.push_back(flag_json(f));
  return json_obj({{"name", json_str(c.name)},
                   {"usage", json_str(usage_line(c))},
                   {"args", json_str(c.args)},
                   {"stage", json_str(c.stage)},
                   {"summary", json_str(c.summary)},
                   {"flags", json_arr(flags)},
                   {"example", json_str(c.example)},
                   {"skill", json_str(c.skill)}});
}

}  // namespace

std::span<const CommandSpec> commands() { return kCommands; }

const CommandSpec* find_command(std::string_view name) {
  for (const CommandSpec& c : kCommands) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

Args parse_args(int argc, const char* const* argv) {
  Args a;
  bool rest = false;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (rest) {
      a.passthrough.push_back(arg);
    } else if (arg == "--") {
      rest = true;
    } else if (arg.starts_with("--")) {
      const size_t eq = arg.find('=');
      a.flags.emplace_back(arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2),
                           eq == std::string::npos ? "" : arg.substr(eq + 1));
    } else {
      a.words.push_back(arg);
    }
  }
  // The longest command name the leading words spell: "skills sync" before "skills".
  if (a.words.size() >= 2) a.command = find_command(a.words[0] + " " + a.words[1]);
  if (a.command) {
    a.words.erase(a.words.begin(), a.words.begin() + 2);
  } else if (!a.words.empty()) {
    a.command = find_command(a.words[0]);
    if (a.command) a.words.erase(a.words.begin());
  }
  return a;
}

std::optional<std::string> check_args(const Args& a) {
  if (!a.command) return "no such command";
  const CommandSpec& c = *a.command;
  for (const auto& [name, value] : a.flags) {
    const FlagSpec* f = find_flag(c, name);
    if (!f) {
      std::string why = "unknown flag --" + name;
      why += " for ge ";
      why += c.name;
      return why;
    }
    if (f->value.empty() && !value.empty()) return "--" + name + " is a switch and takes no value";
    if (!f->value.empty() && f->value[0] != '[' && value.empty()) {
      std::string why = "--" + name + " needs a value: --";
      why += name;
      why += '=';
      why += f->value;
      return why;
    }
  }
  const int n = static_cast<int>(a.words.size());
  if (n < c.min_args || (c.max_args >= 0 && n > c.max_args)) {
    return "ge " + std::string(c.name) + " takes " +
           (c.args.empty() ? std::string("no arguments") : std::string(c.args)) + ", got " +
           std::to_string(n) + " word(s)";
  }
  static constexpr std::string_view kPassing[] = {"lab",     "lab diff", "lab sweep", "play",
                                                  "sim",     "shot",     "record",    "bench",
                                                  "profile", "mem",      "leaks",     "load"};
  if (!a.passthrough.empty() &&
      std::find(std::begin(kPassing), std::end(kPassing), c.name) == std::end(kPassing)) {
    return "only ge lab, lab diff, lab sweep, play, sim, shot, record, bench, profile, mem, "
           "leaks and load pass words after --";
  }
  return std::nullopt;
}

bool has_flag(const Args& a, std::string_view name) {
  return std::any_of(a.flags.begin(), a.flags.end(),
                     [&](const auto& f) { return f.first == name; });
}

std::string flag_value(const Args& a, std::string_view name, std::string_view fallback) {
  for (const auto& [n, v] : a.flags) {
    if (n == name) return v;
  }
  return std::string(fallback);
}

std::string help_text(const CommandSpec* one) {
  if (one) {
    std::string out = usage_line(*one) + "\n  " + std::string(one->summary) + "\n";
    for (const FlagSpec& f : one->flags) {
      out += "  --" + std::string(f.name) + flag_value_text(f.value) + "  " + std::string(f.help) +
             "\n";
    }
    out += "  --json  the answer as one JSON object\n  example: " + std::string(one->example) +
           "\n  skill: " + std::string(one->skill) + "\n";
    return out;
  }
  std::string out = "ge " + std::string(engine_version()) +
                    ": the engine's command line, the one interface an agent makes a game "
                    "through.\nEvery command takes --json (one JSON object: command, ok, exit, "
                    "findings, written, result, summary, next) and --help.\nExit codes: 0 done, 1 "
                    "the work found a problem, 2 a usage error.\n";
  std::string_view stage;
  for (const CommandSpec& c : kCommands) {
    if (c.stage != stage) {
      stage = c.stage;
      out += "\n" + std::string(stage) + ":\n";
    }
    std::string head =
        "  ge " + std::string(c.name) + (c.args.empty() ? "" : " ") + std::string(c.args);
    if (head.size() < 34) head.resize(34, ' ');
    out += head + "  " + std::string(c.summary) + "\n";
  }
  out += "\n`ge help <command>` for its flags; `ge help --json` for the whole catalog.\n";
  return out;
}

std::string help_json(const CommandSpec* one) {
  if (one) return command_json(*one);
  std::vector<std::string> rows;
  for (const CommandSpec& c : kCommands) rows.push_back(command_json(c));
  std::vector<std::string> global;
  for (const FlagSpec& f : kGlobal) global.push_back(flag_json(f));
  return json_obj({{"version", json_str(engine_version())},
                   {"exit_codes", json_obj({{"0", json_str("done")},
                                            {"1", json_str("the work found a problem")},
                                            {"2", json_str("a usage error")}})},
                   {"global_flags", json_arr(global)},
                   {"commands", json_arr(rows)}});
}

}  // namespace ge::cli
