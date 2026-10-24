// LAB `gpu-load`: how well does Fall Line use the machine's CPU threads and its GPU, building a
// map and then drawing the bot's run? It records every thread's timeline (prof/timeline.hpp) and
// the OS sampler (prof/cpu_sampler.hpp) over two phases and reports each with the load charts
// (lab/load_chart.hpp):
//
//   the open    generate_level for --seed, with its bakes on the worker pool (`open-*` shots)
//   the frames  --frames frames of the bot's run from the chase camera, as the window draws
//               them: the simulation's two steps (`sim`), the frame's prepare, encode and submit
//               (`frame.draw`), then the wait for the GPU (`gpu`), paced to --fps like a vsync
//               (the rest of the frame untracked, as a present is)
//
// THE GPU LANE is made from the frame's pass timestamps (gpu/timer.hpp): each pass a slice, its
// length and its gaps the GPU's own, laid from the moment the frame was submitted, because no
// offset between the GPU's clock and the CPU's is known. Its busy share is how much of each frame
// the GPU worked; the main lane's `gpu` waits are laid on it.
//
// ON A SOFTWARE ADAPTER (the GPU-less container: lavapipe) the passes run on CPU threads of this
// process (`llvmpipe-N`), so the GPU lane's times are a CPU rasterizer's and say nothing of a real
// GPU's. The lab says so on every shot and in its notes, names the lane `gpu (software)`, and the
// OS sampler shows the rasterizer's threads as unmarked work: on such a machine "the GPU" is CPU
// cores. Measure a real GPU on the Mac (docs/perf.md § The load monitor): `make lab LAB=gpu-load
// PRESET=release` there. Without an adapter it reports the open alone and the same shots empty,
// so the digest (the shots' and files' names) is every row's. Built only with GE_GPU.
//
// ge:summary Lab: Fall Line's CPU threads and GPU load over a map's open and a run's frames
// ge:keywords lab load gpu-load utilisation threads cores gpu-busy critical-path software-adapter
// ge:keywords lavapipe llvmpipe frame-bound open mapgen pool timeline fall-line
// ge:skill perf render labs
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/thread_name.hpp"
#include "fall_line/app/frame.hpp"
#include "fall_line/app/views.hpp"
#include "fall_line/sim/bot.hpp"
#include "fall_line/sim/simulate.hpp"
#include "gpu/device.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/load_chart.hpp"
#include "mountain/mapgen/generate.hpp"
#include "prof/cpu_sampler.hpp"
#include "prof/load.hpp"
#include "prof/timeline.hpp"

namespace {

namespace gl = ge::lab;
namespace pf = ge::prof;
using ge::lab::Det;

bool env_on(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool on = _dupenv_s(&v, &n, name) == 0 && v && std::strcmp(v, "1") == 0;
  std::free(v);
  return on;
#else
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, "1") == 0;
#endif
}

void run(gl::Lab& lab) {
  const double seed = lab.arg_num("seed");
  const int width = static_cast<int>(lab.arg_num("width"));
  const int height = static_cast<int>(lab.arg_num("height"));
  const int frames = static_cast<int>(lab.arg_num("frames"));
  const double from = lab.arg_num("from");
  const double fps = lab.arg_num("fps");
  const int rivals = static_cast<int>(lab.arg_num("rivals"));
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || frames < 1 || from < 0 ||
      fps < 0) {
    lab.fail("--width and --height must be 1..4096, --frames at least 1, --from and --fps >= 0",
             "next: pass a size such as --width=1280 --height=720 --frames=120.");
    return;
  }
  lab.metric("frames", frames, "count");
  const std::vector<std::pair<std::string, std::string>> inputs = {
      {"seed", lab.arg("seed")},     {"size", lab.arg("width") + "x" + lab.arg("height")},
      {"frames", lab.arg("frames")}, {"from", lab.arg("from")},
      {"fps", lab.arg("fps")},       {"rivals", lab.arg("rivals")}};

  ge::core::name_this_thread("main", false);  // the process keeps its OS name
  pf::CpuSampler sampler;
  pf::timeline_start(size_t{1} << 18);
  const int64_t origin = pf::timeline_origin_ns();
  sampler.start(static_cast<int64_t>(lab.arg_num("sample-us")), origin);

  // The open: the map built, its bakes on the pool.
  ge::mountain::mapgen::GenerateOptions opts;
  opts.version = fall_line::app::kMapVersion;
  std::shared_ptr<const ge::mountain::mapgen::Level> shared;
  {
    GE_WORK("generate_level");
    shared = std::make_shared<const ge::mountain::mapgen::Level>(
        ge::mountain::mapgen::generate_level(seed, opts));
  }
  const int64_t open_end = pf::now_ns() - origin;
  const ge::mountain::mapgen::Level& level = *shared;

  auto device = ge::gpu::create_device({.label = "gpu-load", .validation = 0});
  std::vector<pf::FrameWindow> windows;
  pf::Lane gpu_lane;
  gpu_lane.gpu = true;
  std::string adapter = "no GPU adapter";
  if (device) {
    const auto& rep = device->report;
    adapter = rep.name + " / " + rep.driver + " / " + rep.backend;
    gpu_lane.name = rep.software ? "gpu (software)" : "gpu";
    lab.note("adapter: " + adapter +
             (rep.software ? " (software: the GPU lane's passes ran on this machine's CPU "
                             "threads, the unmarked llvmpipe ones; its times say nothing of a "
                             "real GPU's)"
                           : ""));
    fall_line::app::FrameOptions options;
    auto frame = fall_line::app::create_frame(*device, level, static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height),
                                              wgpu::TextureFormat::RGBA8Unorm, options);
    if (!frame) {
      lab.fail("Fall Line's frame did not build", "Read the gpu: lines above. Skill: render.");
    } else {
      const ge::gpu::Target target = ge::gpu::create_target(
          *device, "gpu-load.final", static_cast<uint32_t>(width), static_cast<uint32_t>(height));
      fall_line::sim::Driver racing(shared, std::max(rivals, 0));
      auto step = [&] {
        racing.step(fall_line::sim::bot_controls(racing.state(), 0, racing.plan()));
      };
      while (racing.state().t < from && !racing.over()) step();
      std::vector<ge::skier::SkierState> skiers;
      ge::render::FrameView view;
      fall_line::app::day_view(level, -1, view);
      fall_line::app::cut(*frame);
      const int64_t period = fps > 0 ? static_cast<int64_t>(1e9 / fps) : 0;
      for (int k = 0; k < frames; ++k) {
        const int64_t start = pf::now_ns();
        {
          GE_WORK("sim");
          for (int s = 0; s < 2 && !racing.over(); ++s) step();
        }
        skiers.clear();
        for (const auto& racer : racing.state().racers) skiers.push_back(racer.skier);
        fall_line::app::chase_view(level, skiers[0], view);
        view.frame = static_cast<uint64_t>(k);
        view.time_s = racing.state().t;
        {
          GE_WORK("frame.draw");
          fall_line::app::draw(*device, *frame, view, {skiers, 2.0 / 120.0, k == 0}, target.view);
        }
        {
          GE_WAIT("gpu");
          frame->recorder.wait(*device);
        }
        // The frame's passes on the GPU lane, laid from its submit.
        const ge::render::FrameStats& st = frame->recorder.stats;
        const int64_t submit = frame->recorder.submit_ns - origin;
        for (int i = 0; i < st.count; ++i) {
          const ge::render::PassStat& p = st.passes[static_cast<size_t>(i)];
          if (!std::isfinite(p.gpu_ms) || !std::isfinite(p.gpu_start_ms)) continue;
          pf::Slice s;
          s.name = pf::zone_id_of(p.name);
          s.start_ns = submit + static_cast<int64_t>(p.gpu_start_ms * 1e6);
          s.dur_ns = static_cast<int64_t>(p.gpu_ms * 1e6);
          gpu_lane.slices.push_back(s);
        }
        if (period > 0) {
          const int64_t left = start + period - pf::now_ns();
          if (left > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(left));
        }
        windows.push_back({start - origin, pf::now_ns() - origin});
      }
      if (gpu_lane.slices.empty()) {
        lab.note(
            "the adapter has no timestamp queries: the GPU lane is empty and its waits are "
            "laid on nothing");
      }
    }
  } else {
    if (env_on("GE_GPU_REQUIRE")) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    lab.note(
        "No GPU adapter on this machine: the open alone is measured; the frames' shots are "
        "empty.");
  }
  pf::timeline_stop();
  sampler.stop();
  std::vector<pf::Lane> lanes = pf::timeline_lanes();
  const auto& samples = sampler.samples();

  pf::LoadOptions o;
  o.sampler_os_id = sampler.os_id();
  o.bin_ns = static_cast<int64_t>(std::max(0.1, lab.arg_num("bin-ms")) * 1e6);
  o.to_ns = open_end;
  const pf::Load open = pf::analyze_load(lanes, samples, {}, o);
  gl::LoadReport ro;
  ro.prefix = "open-";
  ro.subject = "seed " + lab.arg("seed") + ": generate_level (map version " +
               std::to_string(fall_line::app::kMapVersion) + ")";
  ro.inputs = inputs;
  ro.zoom_what = "THE MAP'S OPEN";
  ro.per_frame = false;
  gl::report_load(lab, ro, lanes, open, {});

  if (device) lanes.push_back(gpu_lane);  // the frames' alone: the open drew nothing
  o.from_ns = windows.empty() ? open_end : windows.front().start_ns;
  o.to_ns = windows.empty() ? open_end + 1 : windows.back().end_ns;
  const pf::Load run = pf::analyze_load(lanes, samples, windows, o);
  gl::LoadReport rf;
  rf.subject = "seed " + lab.arg("seed") + ", " + std::to_string(windows.size()) + " frames at " +
               lab.arg("width") + "x" + lab.arg("height") + " on " + adapter;
  rf.inputs = inputs;
  if (windows.size() >= 2) {
    const size_t zoom = std::min<size_t>(windows.size(), 4);
    rf.zoom_from_ns = windows[windows.size() - zoom].start_ns;
    rf.zoom_to_ns = windows.back().end_ns;
    rf.zoom_what = "THE LAST " + std::to_string(zoom) + " FRAMES";
  }
  gl::report_load(lab, rf, lanes, run, windows);
  if (device) {
    lab.metric("gpu-errors", device->errors.load(), "count", Det::measured);
    if (device->errors.load() != 0) {
      lab.fail(std::to_string(device->errors.load()) + " GPU errors while drawing",
               "Read the gpu: lines above (each names its object's label). Skill: render.");
    }
  }
}

const ge::lab::LabSpec kSpec{
    "gpu-load",
    ge::lab::LabKind::bench,
    "game:fall_line",
    "Fall Line's open and frame loop over engine/prof (timeline, sampler, load)",
    "perf",
    "How well does Fall Line use the machine's CPU threads and its GPU while it builds a map and "
    "draws a run: which threads work, which sit idle, and what bounds a frame?",
    {
        {"seed", "38", "the map's seed"},
        {"width", "1280", "the output's width, px"},
        {"height", "720", "the output's height, px"},
        {"frames", "60", "frames drawn"},
        {"from", "10", "seconds into the bot's run the first frame is drawn at"},
        {"fps", "60", "the pace a frame is held to, as a vsync (0: back to back)"},
        {"rivals", "3", "the bots racing beside him"},
        {"bin-ms", "10", "the busy chart's bin"},
        {"sample-us", "5000", "the OS sampler's period"},
    },
    "--width=160 --height=90 --frames=4 --from=3 --rivals=1",
    run,
};
GE_LAB(kSpec);

}  // namespace
