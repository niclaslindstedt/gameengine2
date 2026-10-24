// FALL LINE'S APP: the window, the frame loop, the GPU and the sound, put together. Each frame:
// read the input, hand the run clock the frame's wall time (engine/loop/run_clock.hpp: focus
// lost pauses it), take the steps it grants (the bot skis the map's run, each step's events
// routed to the bank: sound/route.hpp), steer the ride bed off the skier, and draw the map's
// frame (frame.hpp) at vsync, the racers drawn where the run has them, from the camera ladder on
// the player (games/fall_line/camera: the chase to start with, C walks tips, helmet, chase, far
// and high by its hand-overs, and on a machine its own ladder: camera/machines.hpp; --view names
// the rung to start on, or a still view of the map).
//
// THE SOUND: one synth, rendered on the audio device's thread (platform/audio.hpp) and fed from
// this one through its ring. No device (a container, SDL_AUDIO_DRIVER=dummy failing) is not an
// error: the app runs silent. Focus lost fades the buses out with the clock.
//
//   fall_line [--frames=N] [--size=WxH] [--no-vsync] [--seed=N] [--view=NAME] [--hour=H] [--mute]
//             [+<command> <args>...]
//
// THE SETTINGS are cvars (cfg/cvars.hpp): the console execs default.cfg, config.cfg and
// autoexec.cfg from the current directory, then the command line's `+set r_bloom 0 +exec my.cfg`
// (console/cfg.hpp's order), and the app reads the Settings they wrote. The --flags are the same
// cvars spelled the old way (--size sets vid_width and vid_height, --no-vsync vid_vsync 0, --seed
// sv_seed, --view cl_view, --hour sv_hour, --mute s_mute 1), applied before the command line.
//
// C changes the camera.
//
// --frames quits after N presented frames and prints the frame pace (a smoke run under a
// virtual display: xvfb-run -a build/release/games/fall_line/fall_line --frames=60).
// Escape or closing the window quits.
//
// ge:summary Fall Line's app: the window, the frame loop on the run clock, the GPU frame
// ge:keywords app main frame-loop window vsync present run-clock frame-pace fall-line
// ge:keywords camera-key camera-ladder
// ge:skill platform gpu render audio
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio/synth.hpp"
#include "camera/to_view.hpp"
#include "console/cfg.hpp"
#include "core/output.hpp"
#include "fall_line/app/frame.hpp"
#include "fall_line/app/views.hpp"
#include "fall_line/camera/machines.hpp"
#include "fall_line/camera/rungs.hpp"
#include "fall_line/camera/subject.hpp"
#include "fall_line/camera/world.hpp"
#include "fall_line/cfg/cvars.hpp"
#include "fall_line/sim/bot.hpp"
#include "fall_line/sim/simulate.hpp"
#include "fall_line/sim/sky.hpp"
#include "fall_line/sound/mix.hpp"
#include "fall_line/sound/route.hpp"
#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/surface.hpp"
#include "loop/run_clock.hpp"
#include "mountain/mapgen/generate.hpp"
#include "platform/audio.hpp"
#include "platform/window.hpp"

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
};

// The device's thread renders the synth; everything reaches it through the synth's ring.
void pull(float* out, int frames, void* user) {
  static_cast<ge::audio::Synth*>(user)->render(out, frames);
}

// How long the buses take to fade in or out, s.
constexpr float kFaderGlide = 0.3f;

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
        "[--seed=N] [--view=tips|helmet|chase|far|high|orbit|start|piste|lookback|vista|aerial] "
        "[--hour=H] [--mute] [+<command> ...]; next: rerun with those flags only.");
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

  ge::mountain::mapgen::GenerateOptions opts;
  opts.version = fall_line::app::kMapVersion;
  ge::mountain::mapgen::Level built = ge::mountain::mapgen::generate_level(set.seed, opts);
  if (const std::string problem = fall_line::sim::server_sky(set, built); !problem.empty())
    out::error("fall_line: the run's sky was not picked as set (see docs/console.md): " + problem);
  auto shared = std::make_shared<const ge::mountain::mapgen::Level>(std::move(built));
  const ge::mountain::mapgen::Level& level = *shared;
  ge::render::FrameView view;
  fall_line::app::day_view(level, set.hour, view);
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

  // The run and its sound. The synth outlives the device that pulls it (declared first).
  ge::audio::Synth synth;
  fall_line::sim::Driver run(shared, 0);
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

  ge::loop::RunClock clock = ge::loop::create_run_clock(120);
  ge::platform::InputSnapshot input;
  uint64_t last = ge::platform::now_ns();
  const uint64_t first = last;
  long presented = 0;
  long steps = 0;
  std::vector<ge::skier::SkierState> skiers;
  skiers.reserve(run.state().racers.size());
  bool heard_out = false;
  // The run's camera: the ladder kept warm, walked by C (games/fall_line/camera/rungs.hpp), and
  // each machine's own while he rides it (machines.hpp).
  const ge::skier::World skier_world = run.state().world();
  const ge::camera::World camera_world = fall_line::camera::camera_world(skier_world);
  fall_line::camera::RideCamera camera = fall_line::camera::make_ride_camera(rung, 1);
  double last_frame_s = 0;
  while (args.frames == 0 || presented < args.frames) {
    ge::platform::poll_input(*window, input);
    if (input.quit || input.tapped(ge::platform::Key::escape)) break;
    if (input.focus_lost) {
      ge::loop::pause(clock);
      fall_line::sound::Faders hush = faders;
      hush.on = false;
      fall_line::sound::apply_mix(synth, hush, kFaderGlide);
    }
    if (input.focus_gained) {
      ge::loop::resume(clock);
      fall_line::sound::apply_mix(synth, faders, kFaderGlide);
    }
    if (input.resized) {
      surface.configure(*device, static_cast<uint32_t>(input.width_px),
                        static_cast<uint32_t>(input.height_px), set.vsync);
      fall_line::app::resize(*device, *frame, static_cast<uint32_t>(input.width_px),
                             static_cast<uint32_t>(input.height_px));
    }
    const uint64_t now = ge::platform::now_ns();
    const double wall = static_cast<double>(now - last) * 1e-9;
    const int granted = ge::loop::frame(clock, wall);
    steps += granted;
    last = now;
    for (int k = 0; k < granted && !run.over(); ++k) {
      run.step(fall_line::sim::bot_controls(run.state(), 0, run.plan()));
      fall_line::sound::run_sound_step(sound, synth, run.state());
    }
    if (run.over() && !heard_out) {
      fall_line::sound::run_sound_silence(sound, synth);  // the finish rings on, the bed goes
      heard_out = true;
    } else if (granted > 0 && !run.over()) {
      fall_line::sound::run_sound_frame(sound, synth, run.state(),
                                        static_cast<float>(granted) / 120.0f);
    }

    wgpu::TextureView target = surface.acquire(*device);
    if (!target) continue;
    view.time_s = static_cast<double>(now - first) * 1e-9;
    if (set.sky_rate > 0)
      fall_line::app::sky_view(level, fall_line::sim::sky_hour(set, level, view.time_s), view);
    view.frame = static_cast<uint64_t>(presented);
    skiers.clear();
    for (const auto& racer : run.state().racers) skiers.push_back(racer.skier);
    std::optional<std::array<double, 3>> subject;
    if (follow && !skiers.empty()) {
      if (input.tapped(ge::platform::Key::c)) {
        fall_line::camera::next_lens(camera);
      }
      fall_line::camera::apply_ride_cvars(set.camera, camera);
      const double dt = presented == 0 ? 1.0 / 60 : view.time_s - last_frame_s;
      ge::camera::to_frame_view(
          fall_line::camera::frame_ride(camera, run.state(), skier_world, camera_world, dt), view);
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
