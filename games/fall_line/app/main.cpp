// FALL LINE'S APP: the window, the frame loop, the GPU and the sound, put together. Each frame:
// read the window and every device (engine/platform/devices.hpp) into one input state, read it
// through the player's bindings (games/fall_line/controls), hand the run clock the frame's wall
// time (engine/loop/run_clock.hpp: focus lost pauses it), take the steps it grants (the PLAYER
// skis the race through the play contract, controls/by_hand.hpp; each step's events routed to the
// bank, sound/route.hpp, and to the pads' motors, controls/felt.hpp), steer the ride bed off the
// skier, and draw the map's frame (frame.hpp) at vsync, the racers drawn where the run has them,
// from the camera ladder on the player (games/fall_line/camera: the chase to start with, the
// camera key walks tips, helmet, chase, far and high by its hand-overs; --view names the rung to
// start on, or a still view of the map).
//
// THE PLAYER: the keys (A D or the arrows, W tuck, S brake, SPACE jump, R reset, C camera), a pad
// or a wheel, all at once, as the player's bindings file says (controls/bindings.hpp; the defaults
// until F5 walks the rebinding, controls/rebinding.hpp, and saves it). The run is written as an
// INPUT LOG when the window closes (play/inputs.hpp), which `ge replay` plays back to the digest
// it records. --bot hands the skis to the game's bot (the same loop, the same log).
//
// A SCRIPTED SESSION: --events plays a device event log (engine/input/event_log.hpp) into this loop
// in place of the devices, each frame its logged frame and the clock fed the log's frame times, so
// the session steps the same run on any machine, and the window closes once the session is played
// and the run is done; --record-events writes the session's own.
//
// THE SOUND: one synth, rendered on the audio device's thread (platform/audio.hpp) and fed from
// this one through its ring. No device (a container, SDL_AUDIO_DRIVER=dummy failing) is not an
// error: the app runs silent. Focus lost fades the buses out with the clock.
//
//   fall_line [--frames=N] [--size=WxH] [--no-vsync] [--seed=N] [--rivals=N] [--view=NAME]
//             [--hour=H] [--mute] [--bot] [--bindings=PATH] [--inputs=PATH] [--events=PATH]
//             [--record-events=PATH] [+<command> <args>...]
//
// THE SETTINGS are cvars (cfg/cvars.hpp): the console execs default.cfg, config.cfg and
// autoexec.cfg from the current directory, then the command line's `+set r_bloom 0 +exec my.cfg`
// (console/cfg.hpp's order), and the app reads the Settings they wrote. The --flags are the same
// cvars spelled the old way (--size sets vid_width and vid_height, --no-vsync vid_vsync 0, --seed
// sv_seed, --view cl_view, --hour sv_hour, --mute s_mute 1), applied before the command line. A
// sky picked by the sv_ cvars (sim/sky.hpp) is skied as well as drawn, so its run's log names a
// map `ge replay` does not build from the seed alone.
//
// --bindings is the player's file (default: bindings.txt in the platform's per-user directory
// for the game); --inputs where the run's log is written (default: runs/fall_line-<seed>.inputs
// there).
//
// --frames quits after N presented frames and prints the frame pace (a smoke run under a
// virtual display: xvfb-run -a build/release/games/fall_line/fall_line --frames=60).
// Escape or closing the window quits.
//
// ge:summary Fall Line's app: the window, the frame loop on the run clock, the GPU frame
// ge:keywords app main frame-loop window vsync present run-clock frame-pace fall-line
// ge:keywords camera-key camera-ladder player by-hand bindings rebind rumble input-log events
// ge:skill platform gpu render audio input
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio/synth.hpp"
#include "camera/to_view.hpp"
#include "console/cfg.hpp"
#include "core/num_text.hpp"
#include "core/output.hpp"
#include "fall_line/app/frame.hpp"
#include "fall_line/app/sky_type.hpp"
#include "fall_line/app/views.hpp"
#include "fall_line/camera/rungs.hpp"
#include "fall_line/camera/subject.hpp"
#include "fall_line/camera/world.hpp"
#include "fall_line/cfg/cvars.hpp"
#include "fall_line/controls/bindings.hpp"
#include "fall_line/controls/by_hand.hpp"
#include "fall_line/controls/rebinding.hpp"
#include "fall_line/controls/script.hpp"
#include "fall_line/sim/sky.hpp"
#include "fall_line/sim/state.hpp"
#include "fall_line/sound/mix.hpp"
#include "fall_line/sound/route.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/surface.hpp"
#include "input/event_log.hpp"
#include "input/state.hpp"
#include "loop/run_clock.hpp"
#include "platform/audio.hpp"
#include "platform/devices.hpp"
#include "platform/window.hpp"
#include "play/inputs.hpp"

namespace {

namespace out = ge::core::output;

void print_line(const ge::core::output::LogLine& line, void*) {
  std::FILE* to = line.level == ge::core::output::Level::error ? stderr : stdout;
  std::fprintf(to, "%.*s\n", static_cast<int>(line.message.size()), line.message.data());
}

struct Args {
  long frames = 0;                     // 0: until quit
  std::vector<std::string> sets;       // the --flags as console commands
  std::vector<std::string> plus_args;  // the command line from the first `+` word on
  int rivals = 0;
  bool bot = false;           // the game's bot skis
  std::string bindings;       // the player's file ("" its place in the prefs directory)
  std::string inputs;         // where the run's log goes ("" its place in the prefs directory)
  std::string events;         // a scripted session to play in place of the devices
  std::string record_events;  // where the session's device events go ("" nowhere)
};

// The device's thread renders the synth; everything reaches it through the synth's ring.
void pull(float* out, int frames, void* user) {
  static_cast<ge::audio::Synth*>(user)->render(out, frames);
}

// How long the buses take to fade in or out, s.
constexpr float kFaderGlide = 0.3f;

bool read_file(const std::string& path, std::string& text) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream all;
  all << file.rdbuf();
  text = all.str();
  return true;
}

bool write_file(const std::string& path, const std::string& text) {
  const std::filesystem::path p(path);
  std::error_code ec;
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  return static_cast<bool>(file);
}

bool parse(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (!a.plus_args.empty() || (!arg.empty() && arg[0] == '+')) {
      a.plus_args.emplace_back(arg);
    } else if (arg.rfind("--frames=", 0) == 0) {
      a.frames = std::strtol(argv[i] + 9, nullptr, 10);
    } else if (arg.rfind("--size=", 0) == 0) {
      const std::string_view wh = arg.substr(7);
      const size_t x = wh.find('x');
      if (x == std::string_view::npos) return false;
      a.sets.push_back("vid_width " + std::string(wh.substr(0, x)));
      a.sets.push_back("vid_height " + std::string(wh.substr(x + 1)));
    } else if (arg == "--no-vsync") {
      a.sets.emplace_back("vid_vsync 0");
    } else if (arg.rfind("--seed=", 0) == 0) {
      a.sets.push_back("sv_seed " + std::string(arg.substr(7)));
    } else if (arg.rfind("--view=", 0) == 0) {
      a.sets.push_back("cl_view " + std::string(arg.substr(7)));
    } else if (arg.rfind("--hour=", 0) == 0) {
      a.sets.push_back("sv_hour " + std::string(arg.substr(7)));
    } else if (arg == "--mute") {
      a.sets.emplace_back("s_mute 1");
    } else if (arg.rfind("--rivals=", 0) == 0) {
      a.rivals = static_cast<int>(std::strtol(argv[i] + 9, nullptr, 10));
    } else if (arg == "--bot") {
      a.bot = true;
    } else if (arg.rfind("--bindings=", 0) == 0) {
      a.bindings = std::string(arg.substr(11));
    } else if (arg.rfind("--inputs=", 0) == 0) {
      a.inputs = std::string(arg.substr(9));
    } else if (arg.rfind("--events=", 0) == 0) {
      a.events = std::string(arg.substr(9));
    } else if (arg.rfind("--record-events=", 0) == 0) {
      a.record_events = std::string(arg.substr(16));
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  out::set_output_sink(print_line);
  Args args;
  if (!parse(argc, argv, args)) {
    out::error(
        "fall_line: unknown flag. Usage: fall_line [--frames=N] [--size=WxH] [--no-vsync] "
        "[--seed=N] [--rivals=N] "
        "[--view=tips|helmet|chase|far|high|orbit|start|piste|lookback|vista|aerial] "
        "[--hour=H] [--mute] [--bot] [--bindings=PATH] [--inputs=PATH] [--events=PATH] "
        "[--record-events=PATH] [+<command> ...]; next: rerun with those flags only.");
    return 2;
  }
  // The settings: the cfgs, the --flags, the command line, then every latched value applied.
  ge::console::Console console;
  console.set_sink([](std::string_view line) { out::status("console: " + std::string(line)); });
  fall_line::cfg::Settings set;
  fall_line::cfg::add_cvars(console, set);
  for (const std::string& cmd : args.sets) console.append(cmd);
  ge::console::start(console, args.plus_args);
  console.cvars().apply_latched();
  auto window =
      ge::platform::open_window({.title = "Fall Line", .width = set.width, .height = set.height});
  if (!window) return 1;
  auto device = ge::gpu::create_device();
  if (!device) return 1;
  ge::gpu::Surface surface;
  if (!surface.create(*device, window->native())) return 1;
  int w = 0, h = 0;
  window->pixel_size(w, h);
  surface.configure(*device, static_cast<uint32_t>(w), static_cast<uint32_t>(h), set.vsync);

  // The player's bindings and the run they ski, on the play contract's race map for the seed.
  const std::string prefs = ge::platform::prefs_dir("gameengine2", "fall_line");
  const std::string bindings_path = !args.bindings.empty() ? args.bindings : prefs + "bindings.txt";
  const std::string inputs_path =
      !args.inputs.empty()
          ? args.inputs
          : prefs + "runs/fall_line-" + ge::core::num::to_string(set.seed) + ".inputs";
  ge::input::ActionMap map = fall_line::controls::default_map();
  std::string error;
  if (!fall_line::controls::load_player_map(bindings_path, map, error))
    out::error("fall_line: the player's bindings did not read, so the defaults stand: " + error +
               "; see docs/input.md");
  std::unique_ptr<fall_line::controls::HandRun> hand =
      fall_line::controls::open_hand_run(set.seed, args.rivals, std::move(map), error);
  if (!hand) {
    out::error("fall_line: the run on seed " + ge::core::num::to_string(set.seed) +
               " did not open: " + error + "; next: pass another --seed or fix the bindings");
    return 2;
  }
  // A sky the sv_ cvars picked is the run's as well as the picture's: the run reopened on the map
  // under it (sim/sky.hpp), which only this window's run knows.
  const bool sky_picked =
      set.sky != "map" || set.fall >= 0 || set.fog >= 0 || set.air >= 0 || set.air_from >= 0;
  if (sky_picked) {
    ge::mountain::mapgen::Level built = *fall_line::controls::hand_state(*hand).level;
    if (const std::string problem = fall_line::sim::server_sky(set, built); !problem.empty())
      out::error("fall_line: the run's sky was not picked as set (see docs/console.md): " +
                 problem);
    hand = fall_line::controls::open_hand_run(
        set.seed, args.rivals, std::move(hand->map), error,
        std::make_shared<const ge::mountain::mapgen::Level>(std::move(built)));
    if (!hand) {
      out::error("fall_line: the run under the picked sky did not open: " + error +
                 "; next: set sv_weather map, or see docs/console.md");
      return 2;
    }
  }
  hand->pilot = args.bot;
  ge::input::EventLog script;
  if (!args.events.empty()) {
    std::string text;
    if (!read_file(args.events, text) || !ge::input::read_events(text, script, error)) {
      out::error("fall_line: the session " + args.events + " does not read" +
                 (error.empty() ? "" : ": " + error) +
                 "; next: record one with --record-events=PATH, or see docs/input.md");
      return 2;
    }
  }
  const ge::mountain::mapgen::Level& level = *fall_line::controls::hand_state(*hand).level;
  ge::render::FrameView view;
  fall_line::app::day_view(level, set.hour, view);
  if (const std::string problem = fall_line::app::sky_style_of(set, view.style); !problem.empty())
    out::error("fall_line: the run's sky type was not picked as set (see docs/console.md): " +
               problem);
  // A rung of the camera ladder follows the run; any other name is a still view of the map.
  fall_line::camera::Rung rung = fall_line::camera::Rung::chase;
  const bool follow = fall_line::camera::rung_named(set.view, rung);
  if (!follow && !fall_line::app::make_view(level, set.view, view)) {
    out::error("fall_line: no view named '" + set.view +
               "'; next: set cl_view to a rung in games/fall_line/camera/rungs.hpp or a rig in "
               "games/fall_line/app/views.hpp.");
    return 2;
  }
  auto frame = fall_line::app::create_frame(*device, level, static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h), surface.format(),
                                            fall_line::app::frame_options(set));
  if (!frame) return 1;

  // The run's sound. The synth outlives the device that pulls it (declared first).
  ge::audio::Synth synth;
  fall_line::sound::RunSound sound = fall_line::sound::make_run_sound();
  fall_line::sound::Faders faders;
  faders.master = set.volume;
  faders.wind = set.wind;
  faders.effects = set.effects;
  faders.on = !set.mute;
  fall_line::sound::apply_mix(synth, faders, 0);
  std::unique_ptr<ge::platform::AudioOut> speaker;
  if (!set.mute) {
    speaker = ge::platform::open_audio({.rate = 48000, .pull = pull, .user = &synth});
    if (!speaker) {
      out::status("fall_line: no audio device; running silent.");
    } else {
      out::status(std::string("fall_line: audio on '") + speaker->driver() + "'");
    }
  }

  // Every device the player holds, read in the window's pump (none with a scripted session).
  ge::platform::DevicesPtr devices;
  if (script.frames.empty()) {
    devices = ge::platform::open_devices();
    if (!devices) return 1;
  }
  ge::input::Queue queue;
  ge::input::InputState held;
  ge::input::EventLog session;
  fall_line::controls::Rebinding rebinding;

  ge::loop::RunClock clock = ge::loop::create_run_clock(120);
  ge::platform::InputSnapshot input;
  uint64_t last = ge::platform::now_ns();
  const uint64_t first = last;
  long presented = 0;
  long steps = 0;
  size_t scripted = 0;          // the next frame of the scripted session
  int64_t script_due = 0;       // the steps its frames have been granted
  uint64_t script_last_ns = 0;  // its last frame's time
  std::vector<ge::skier::SkierState> skiers;
  skiers.reserve(fall_line::controls::hand_state(*hand).racers.size());
  bool heard_out = false;
  // The run's camera: the ladder kept warm, walked by the camera key (camera/rungs.hpp).
  const ge::skier::World skier_world = fall_line::controls::hand_state(*hand).world();
  const ge::camera::World camera_world = fall_line::camera::camera_world(skier_world);
  fall_line::camera::RunCamera camera = fall_line::camera::make_skier_camera(rung, 1);
  double last_frame_s = 0;
  while (args.frames == 0 || presented < args.frames) {
    // The frame's input: the window's and every device's events, or the session's next frame.
    queue.clear();
    double wall = 0;
    const uint64_t now = ge::platform::now_ns();
    uint64_t frame_ns = now - first;  // the frame's time: the felt's clock and the session's
    int scripted_steps = 0;           // a scripted frame's steps, by its time (script.hpp)
    if (!script.frames.empty()) {
      ge::platform::poll_input(*window, input);
      const size_t at = scripted++;
      if (at < script.frames.size()) {
        queue = script.frames[at].q;
        frame_ns = script.frames[at].t_ns;
      } else {  // past the session's end: nothing new held, the run skis on to its end
        frame_ns =
            script.frames.back().t_ns +
            fall_line::controls::frame_time(static_cast<int64_t>(at - script.frames.size() + 1));
      }
      wall = static_cast<double>(frame_ns - script_last_ns) * 1e-9;
      const int64_t due = fall_line::controls::steps_due(frame_ns);
      scripted_steps = static_cast<int>(due - script_due);
      script_due = due;
      script_last_ns = frame_ns;
    } else {
      ge::platform::poll_input(*window, input, *devices, queue);
      wall = static_cast<double>(now - last) * 1e-9;
    }
    last = now;
    ge::input::update(held, queue);
    if (!args.record_events.empty()) session.add(frame_ns, queue);
    if (input.quit || (input.tapped(ge::platform::Key::escape) && !rebinding.active)) break;
    // A scripted session is a batch: the window closes once it is played and the run is done.
    if (!script.frames.empty() && scripted > script.frames.size() &&
        !fall_line::controls::hand_playing(*hand)) {
      break;
    }
    if (input.focus_lost && script.frames.empty()) {
      ge::loop::pause(clock);
      fall_line::sound::Faders hush = faders;
      hush.on = false;
      fall_line::sound::apply_mix(synth, hush, kFaderGlide);
    }
    if (input.focus_gained && script.frames.empty()) {
      ge::loop::resume(clock);
      fall_line::sound::apply_mix(synth, faders, kFaderGlide);
    }
    if (input.resized) {
      surface.configure(*device, static_cast<uint32_t>(input.width_px),
                        static_cast<uint32_t>(input.height_px), set.vsync);
      fall_line::app::resize(*device, *frame, static_cast<uint32_t>(input.width_px),
                             static_cast<uint32_t>(input.height_px));
    }

    // The actions; the rebinding walk holds the run while it asks.
    fall_line::controls::hand_frame(*hand, held, wall);
    std::string said;
    if (rebinding.active) {
      const auto at = fall_line::controls::poll_rebinding(rebinding, held, hand->map, said);
      if (!said.empty()) out::status("fall_line: " + said);
      if (at == fall_line::controls::RebindStep::done ||
          at == fall_line::controls::RebindStep::stopped) {
        if (fall_line::controls::save_player_map(bindings_path, hand->map, error)) {
          out::status("fall_line: the bindings are saved to " + bindings_path);
        } else {
          out::error("fall_line: the rebinding was not saved: " + error + "; see docs/input.md");
        }
        ge::loop::resume(clock);
      }
    } else if (fall_line::controls::hand_pressed(*hand, "rebind")) {
      fall_line::controls::begin_rebinding(rebinding, held, hand->map, said);
      out::status("fall_line: " + said);
      ge::loop::pause(clock);
    }

    const double felt_s = static_cast<double>(frame_ns) * 1e-9;
    // The run clock grants a live frame its steps; a scripted frame's are due by its time.
    const int granted = script.frames.empty() ? ge::loop::frame(clock, wall) : scripted_steps;
    steps += granted;
    for (int k = 0; k < granted && fall_line::controls::hand_playing(*hand); ++k) {
      fall_line::controls::hand_step(*hand, held, felt_s);
      fall_line::sound::run_sound_step(sound, synth, fall_line::controls::hand_state(*hand));
    }
    if (devices) ge::platform::play_feedback(*devices, hand->felt, held, felt_s);
    const fall_line::sim::GameState& state = fall_line::controls::hand_state(*hand);
    const bool playing = fall_line::controls::hand_playing(*hand);
    if (!playing && !heard_out) {
      fall_line::sound::run_sound_silence(sound, synth);  // the finish rings on, the bed goes
      heard_out = true;
    } else if (granted > 0 && playing) {
      fall_line::sound::run_sound_frame(sound, synth, state, static_cast<float>(granted) / 120.0f);
    }

    wgpu::TextureView target = surface.acquire(*device);
    if (!target) continue;
    view.time_s = static_cast<double>(now - first) * 1e-9;
    if (set.sky_rate > 0)
      fall_line::app::sky_view(level, fall_line::sim::sky_hour(set, level, view.time_s), view);
    view.frame = static_cast<uint64_t>(presented);
    skiers.clear();
    for (const auto& racer : state.racers) skiers.push_back(racer.skier);
    std::optional<std::array<double, 3>> subject;
    if (follow && !skiers.empty()) {
      if (fall_line::controls::hand_pressed(*hand, "camera") && !rebinding.active) {
        fall_line::camera::change_rung(
            camera.cam, fall_line::camera::next_rung(fall_line::camera::rung_of(camera.cam)));
      }
      const double dt = presented == 0 ? 1.0 / 60 : view.time_s - last_frame_s;
      ge::camera::to_frame_view(
          fall_line::camera::frame_camera(camera, skiers[0], skier_world, camera_world, dt), view);
      const ge::camera::Vec3 p = fall_line::camera::framed_point(skiers[0]);
      subject = std::array<double, 3>{p.x, p.y, p.z};
    }
    last_frame_s = view.time_s;
    // The figures' lags run over the steps this frame took; the first frame starts them settled.
    fall_line::app::draw(
        *device, *frame, view,
        {skiers, static_cast<double>(granted) / 120.0, presented == 0, {}, subject}, target);
    surface.present();
    ++presented;
  }

  // The run as its input log, and the session as its device events.
  const ge::play::InputLog log = fall_line::controls::hand_log(*hand);
  if (write_file(inputs_path, ge::play::to_text(log))) {
    out::status("fall_line: the run is written to " + inputs_path + " (" +
                std::to_string(log.steps()) + " steps, digest " +
                ge::play::hex64(log.digest.value_or(0)) + (hand->run->over() ? ", finished" : "") +
                (sky_picked ? "); under a picked sky, which `ge replay` does not rebuild"
                            : "); play it back with `ge replay " + inputs_path + "`"));
  } else {
    out::error("fall_line: cannot write the run to " + inputs_path +
               "; pass --inputs=<a writable path>");
  }
  if (!args.record_events.empty()) {
    if (write_file(args.record_events, ge::input::write_events(session))) {
      out::status("fall_line: the session's device events are written to " + args.record_events);
    } else {
      out::error("fall_line: cannot write the session to " + args.record_events +
                 "; next: pass --record-events=<a writable path>");
    }
  }
  const double seconds = static_cast<double>(ge::platform::now_ns() - first) * 1e-9;
  char line[160];
  std::snprintf(line, sizeof line,
                "fall_line: %ld frames in %.2f s (%.1f ms a frame), %ld steps, %u GPU errors",
                presented, seconds,
                presented ? seconds * 1e3 / static_cast<double>(presented) : 0.0, steps,
                device->errors.load());
  out::status(line);
  return device->errors.load() == 0 && !device->lost.load() ? 0 : 1;
}
