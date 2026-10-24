// THE PLAY CONTRACT: what a game fills in so an agent can play it, replay it and inspect it
// without a screen, through `ge play`, `ge sim`, `ge replay`, `ge state` and `ge diverge`. A game
// describes itself once (a `GameSpec`: its name, its step rate, the axes a pilot holds, the
// options a run is set up with) and opens a `Run`, the one interface the engine drives: step it
// on one value per axis, ask whether it is over, read its digest, what a player sees (`observe`),
// what decides its next step (`state`) and its report, and, when the game has one, let its bot
// fill the axes (`pilot`). Everything above it (the line protocol, the input log, the replay,
// the divergence search) is the engine's (session.hpp), so every game is played the same way.
//
// THE AXES are the whole input: the engine snaps each value to its kind's grid before the step
// (`snap_axis`), so the figure a run is stepped on is the figure the input log writes down, and a
// log played back is the run again. A `pilot` and an agent are snapped the same way.
//
// THE VIEWS are what `ge shot` can picture: each a name the spec lists, drawn by the Run on the
// CPU (lab/raster.hpp) at the step the run has reached, so a still needs no GPU and no window and
// reproduces from the input log like the rest of the run. A game with a renderer can still draw a
// view on the CPU (a map from above, a chart of the field).
//
// THE GPU'S PICTURES (`ge shot --gpu`, `ge record --gpu`): a play executable built with its
// game's renderer also registers a `RendererSpec` (`GE_RENDERER(spec)`): the views the renderer
// draws, its MODES (each a way of drawing the same view: lit, the meshes' edges, the depth, the
// game's marks over the lit picture) and `open`, which makes the renderer once (a device, the
// game's frame) for a whole shot or recording. `Renderer::draw` pictures a Run as it stands; the
// renderer keeps what one frame hands the next (the tracks laid, the spray, the history), so a
// recording hands it every frame in order and says when a frame is a cut. A build with no GPU
// registers none, and `--gpu` says so.
//
// THE KEYFRAME is a `clone` of the run (engine/replay): a replay seeks by cloning the nearest one
// and stepping it on off the input log.
//
// THE RUN'S SOUND (`ge record --format=mkv`, engine/capture/av_export.hpp): a play executable
// built with its game's sound also registers a `SoundSpec` (`GE_SOUND(spec)`), whose `open` sets a
// synth up for the game (its buses, its beds) and returns the `Sound` that plays a run into it:
// `step` after every step (the step's events as one-shots) and `frame` once a picture (the beds
// steered to the run as it stands). The caller renders the synth, so an export hears the run
// sample for sample on the run's own clock.
//
// THE TRACE is what `ge why` reads: a Run whose bots write decision records (ai/why.hpp) says so
// by accepting a recorder (`trace`), and the engine replays a log with it on (play/why.hpp).
//
// THE CVARS are what `ge cvar` and `ge cfg` answer for: a game whose settings are cvars
// (console/cvar.hpp) names the function that registers them on a console (`cvars`), the same one
// its app calls, so the play executable lists, checks and runs cfgs against the game's own names
// without a screen (play/console_cmds.hpp).
//
// A game registers its spec with `GE_GAME(spec)` in one file of its `play` executable, whose
// `main` is `ge::play::play_main` (main.hpp). One executable holds one game.
//
// ge:summary The play contract: GameSpec, the axes, Run (step, observe, state, digest, pilot)
// ge:keywords play contract game spec run axis axes option setup pilot bot observe state digest
// ge:keywords snap_axis ge_game register view views shoot shot picture caption trace why
// ge:keywords clone keyframe copy
// ge:keywords renderer renderer-spec ge_renderer gpu mode modes lit wireframe depth overlay
// ge:keywords sound sound-spec ge_sound synth audio export
// ge:keywords cvars console cfg
// ge:skill agent-interface
// ge:test tests/play_session_test.cpp
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ai/why.hpp"
#include "console/console.hpp"
#include "lab/raster.hpp"

namespace ge::audio {
class Synth;
}

namespace ge::play {

// How an axis is held, and its grid.
enum class AxisKind : uint8_t {
  signed_unit,  // -1 .. 1, on 1/127 steps (a steer)
  lever,        // 0 .. 1, on 1/255 steps (a throttle, a brake)
  toggle,       // 0 or 1, held (a button held down)
  press,        // 0 or 1, taken once: a hold of several steps presses it on the first only
};

inline constexpr double kSignedSteps = 127;
inline constexpr double kLeverSteps = 255;

struct Axis {
  const char* name;  // snake_case, as the protocol and the input log spell it
  AxisKind kind;
  const char* help;  // what it does, a phrase
};

// A setting a run is opened with, as `--<name>=<value>` on the command line and `option` in the
// input log.
struct Option {
  const char* name;
  const char* fallback;  // its value when none is given
  const char* help;
};

// A picture `ge shot` can take, as `--view=<name>`.
struct View {
  const char* name;  // snake_case
  const char* help;  // what it shows, a phrase
};

// What a run is opened with: the seed and every option (each one present, its fallback filled).
struct Setup {
  double seed = 0;
  std::map<std::string, std::string> options;
  // The option as a number; its fallback's when it does not read as one.
  double number(const std::string& name) const;
};

class Run {
 public:
  virtual ~Run() = default;
  // One fixed step on `input`: one value per axis, in the spec's order, already snapped.
  virtual void step(std::span<const double> input) = 0;
  // Nothing is left to play (the finish, a game over). The engine also stops at the spec's
  // `most_seconds`.
  virtual bool over() const = 0;
  // FNV-1a (or any 64-bit hash) folded over every step's state so far: two runs with the same
  // digest after the same steps are the same run.
  virtual uint64_t digest() const = 0;
  // What a player sees now, as one JSON object: an agent decides on this. It may report what
  // happened since the last call (its events), so it is not const.
  virtual std::string observe() = 0;
  // Everything that decides the next step, as one JSON object of scalars (nested objects are
  // read as dotted keys; no arrays): `ge diverge` compares two runs field by field on it.
  virtual std::string state() const = 0;
  // The run's result so far, as one JSON object (`ge sim` and `ge replay` print it).
  virtual std::string report() const = 0;
  // The game's own pilot: fill one value per axis and return true, or return false when the
  // game has none (then `ge sim` cannot run it).
  virtual bool pilot(std::span<double> input) {
    (void)input;
    return false;
  }
  // THE DECISION TRACE (ai/why.hpp): from now on every bot of the run, its pilot among them,
  // records its decisions into `why` (null: stop). Return false when the game's bots record
  // none (then `ge why` cannot explain them). Recording must not change a decision: the run's
  // digest is the same traced and untraced.
  virtual bool trace(ai::Why* why) {
    (void)why;
    return false;
  }
  // The still `view` (one the spec lists) of the run as it stands: drawn into `out` on the CPU,
  // with `caption` one line saying what it shows and when. False with `error` saying why not.
  virtual bool shoot(std::string_view view, lab::Picture& out, std::string& caption,
                     std::string& error) {
    (void)view;
    (void)out;
    (void)caption;
    error = "this game draws no views";
    return false;
  }
  // A KEYFRAME (engine/replay/keep.hpp): a deep copy of the run as it stands that steps on exactly
  // as this one would, its digest included, sharing only what no step writes (the map). Null when
  // the game keeps no copies: then a replay seeks back only by playing again from the start. The
  // copy's decision trace is off whatever this run's is.
  virtual std::unique_ptr<Run> clone() const { return nullptr; }
};

struct GameSpec {
  const char* name;  // snake_case: the input log's `game` line
  const char* summary;
  double hz;                   // steps a second
  double most_seconds;         // the longest run `ge sim` and the protocol drive
  std::span<const Axis> axes;  // the input, in order
  std::span<const Option> options;
  // A new run, or null with `error` saying why (an option it cannot use).
  std::unique_ptr<Run> (*open)(const Setup& setup, std::string& error);
  // What `ge shot` can picture, the first the default; empty when the game draws none.
  std::span<const View> views = {};
  // Registers every cvar the game reads on `c` (null: the game has none).
  void (*cvars)(console::Console& c) = nullptr;
  // THE BEHAVIOUR'S VERSION: raised by a change that moves what a log plays to (a step, a rule, a
  // tuning), so a replay file recorded by another version is refused rather than played as a
  // different run (replay/file.hpp).
  int32_t version = 1;
};

// What one GPU picture is asked for: a view and a mode the renderer lists, the picture's size,
// and whether it is a cut (the first frame of a shot or a recording: nothing of a last frame is
// kept).
struct RenderShot {
  std::string_view view;
  std::string_view mode;
  int width = 0, height = 0;
  bool cut = true;
};

class Renderer {
 public:
  virtual ~Renderer() = default;
  // `run` as it stands, drawn on the GPU as `shot` asks, into `out` (RGBA, shot.width x
  // shot.height), with `caption` one line saying what it shows. False with `error` saying why.
  virtual bool draw(const Run& run, const RenderShot& shot, lab::Picture& out, std::string& caption,
                    std::string& error) = 0;
};

struct RendererSpec {
  std::span<const View> views;  // the first the default
  std::span<const View> modes;  // the first the default
  int width, height;            // a picture's size when none is asked for
  // The renderer, or null with `error` saying why (no adapter, a pipeline that failed).
  std::unique_ptr<Renderer> (*open)(std::string& error);
};

// `v` on its axis's grid: clamped to the axis's range and rounded to its step (toggles to 0 or
// 1). NaN reads as 0.
double snap_axis(double v, AxisKind kind);

// `seed` and the given options (`name` → text) over `spec`'s fallbacks; an option the spec does
// not name is an error.
bool make_setup(const GameSpec& spec, double seed, const std::map<std::string, std::string>& given,
                Setup& out, std::string& error);

// The game registered in this executable, or null.
const GameSpec* registered_game();

struct GameRegister {
  explicit GameRegister(const GameSpec& spec);
};

// The renderer registered in this executable, or null (a build with no GPU).
const RendererSpec* registered_renderer();

struct RendererRegister {
  explicit RendererRegister(const RendererSpec& spec);
};

// A run's sound, played into the synth its spec opened it over (THE RUN'S SOUND above).
class Sound {
 public:
  virtual ~Sound() = default;
  // After each step of `run`: that step's events.
  virtual void step(const Run& run) = 0;
  // Once a picture, `dt` seconds after the last: the beds steered to `run` as it stands.
  virtual void frame(const Run& run, double dt) = 0;
};

struct SoundSpec {
  // The game's sound over `synth`, or null with `error` saying why (a run of another game).
  std::unique_ptr<Sound> (*open)(audio::Synth& synth, std::string& error);
};

// The sound registered in this executable, or null (a game with no sound).
const SoundSpec* registered_sound();

struct SoundRegister {
  explicit SoundRegister(const SoundSpec& spec);
};

}  // namespace ge::play

#define GE_GAME_CAT2(a, b) a##b
#define GE_GAME_CAT(a, b) GE_GAME_CAT2(a, b)
// Register `spec` (a GameSpec with static storage) as this executable's game.
#define GE_GAME(spec) static ::ge::play::GameRegister GE_GAME_CAT(ge_game_reg_, __LINE__)(spec)
// Register `spec` (a RendererSpec with static storage) as this executable's GPU renderer.
#define GE_RENDERER(spec) \
  static ::ge::play::RendererRegister GE_GAME_CAT(ge_renderer_reg_, __LINE__)(spec)
// Register `spec` (a SoundSpec with static storage) as this executable's sound.
#define GE_SOUND(spec) static ::ge::play::SoundRegister GE_GAME_CAT(ge_sound_reg_, __LINE__)(spec)
