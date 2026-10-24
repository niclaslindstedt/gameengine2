// LAB `menu-gpu`: the `menu` lab's pages drawn by the 2D pass on the GPU (render/ui_pass.hpp),
// held to the CPU raster's pictures of the same draw lists. The front door over every backdrop at
// the first viewport, and both pages at every viewport over the drifting picture: each drawn
// both ways, pictured as the GPU drew it beside a map of where it differs from the raster
// (black where it matches; brighter the more levels off). On the pinned adapter (Mesa's
// lavapipe) a page off the raster by more than 3 levels at more than 0.3% of its pixels fails
// the lab; on any other the numbers are measured and noted. The pass's GPU time a page is
// measured where the adapter has timestamps. Without an adapter it notes that and pictures
// placeholders, so the digest is the same on every row; GE_GPU_REQUIRE=1 fails instead. Built
// only with GE_GPU.
//
// ge:summary Lab: the menu's pages drawn by the GPU's 2D pass, held to the CPU raster, timed
// ge:keywords lab menu gpu 2d-pass ui raster parity diff tolerance lavapipe timing
// ge:skill ui render labs
// ge:test tests/render_ui_pass_test.cpp
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "render/frame.hpp"
#include "render/ui_pass.hpp"
#include "ui/background.hpp"
#include "ui/menu_view.hpp"
#include "ui/raster.hpp"

#include "ui_menu_scenes.hpp"

namespace {

using namespace ge::ui;
using namespace ge::lab::menu_scenes;
using ge::lab::Det;

constexpr int kLevels = 3;        // a channel's difference the raster and the GPU may have
constexpr double kShare = 0.003;  // of the pixels, beyond kLevels, on the pinned adapter
constexpr int kDiffGain = 32;     // the difference map's brightness a level

bool env_on(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool on = _dupenv_s(&v, &n, name) == 0 && v && std::strcmp(v, "1") == 0;
  std::free(v);
  return on;
#else
  const char* v = std::getenv(name);
  return v && std::strcmp(v, "1") == 0;
#endif
}

struct Page {
  std::string id, title, caption;
  const Backdrop* backdrop;
  const char* page;
  Viewport viewport;
};

struct Gpu {
  std::unique_ptr<ge::gpu::Device> device;
  ge::gpu::PipelineSet pipelines;
  ge::render::UiPass pass;
  ge::render::FrameRecorder rec;
};

void run(ge::lab::Lab& lab) {
  Scenes scenes;
  make_scenes(scenes, lab.arg_num("seed"));
  const std::vector<Viewport> viewports = parse_viewports(lab.arg("viewports"));
  if (viewports.empty()) {
    lab.fail("--viewports=" + lab.arg("viewports") + " named no WxH size",
             "run make lab LAB=menu-gpu ARGS=--viewports=1280x720,390x844");
    return;
  }
  const MenuStyle st = front_style();

  std::vector<Page> pages;
  pages.reserve(scenes.backdrops.size() + viewports.size() * 2);
  const Viewport& main = viewports.front();
  for (const Backdrop& b : scenes.backdrops) {
    pages.push_back({"door-" + std::string(b.id), "FRONT DOOR OVER " + std::string(b.id) + ", GPU",
                     "The root page over " + std::string(b.what) + ", drawn by the GPU's 2D pass.",
                     &b, "root", main});
  }
  for (const Viewport& v : viewports) {
    for (const char* page : {"root", "options"}) {
      const std::string size = std::to_string(v.w) + "x" + std::to_string(v.h);
      pages.push_back({std::string(page) + "-" + size,
                       std::string(page) + " PAGE AT " + size + ", GPU",
                       std::string("The ") + page + " page at " + size + " (" + v.name +
                           "), drawn by the GPU's 2D pass.",
                       &scenes.backdrops[3], page, v});
    }
  }

  Gpu gpu;
  gpu.device = ge::gpu::create_device({.label = "menu-gpu", .validation = 1});
  bool pinned = false;
  if (gpu.device) {
    const auto& r = gpu.device->report;
    pinned = r.name.rfind("llvmpipe", 0) == 0;
    lab.note("adapter: " + r.name + " / " + r.driver + " / " + r.backend +
             (r.software ? " (software)" : ""));
    ge::render::create_ui_pass(*gpu.device, gpu.pipelines, gpu.pass);
    if (!gpu.pipelines.finish(*gpu.device)) {
      lab.fail("the 2D pass's pipeline did not build", "Read the gpu: lines above. Skill: render.");
      return;
    }
    ge::gpu::end_load(*gpu.device);
    gpu.rec.init(*gpu.device, "menu-gpu.timer", 1);
  } else if (env_on("GE_GPU_REQUIRE")) {
    lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
             "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
  } else {
    lab.note("No GPU adapter on this machine: the pictures are placeholders.");
  }

  size_t commands = 0;
  int worst = 0, timed = 0;
  double worst_share = 0, gpu_ms = 0, prepare_ms = 0;
  uint32_t draws = 0;
  for (const Page& p : pages) {
    Menu m = front_door();
    if (std::string(p.page) == "options") m.open("options");
    m.input({});
    if (std::string(p.page) == "options") m.focus("volume");
    const int w = p.viewport.w, h = p.viewport.h;
    const MenuLayout L = layout_menu(m, w, h, st);
    MenuView view;
    view.settle(m, L);
    DrawList dl;
    dl.begin(w, h);
    draw_background(dl, p.backdrop->bg, {0, 0, float(w), float(h)}, 0,
                    p.backdrop->scene ? &scenes.soft : nullptr);
    view.draw(dl, m, L, st);
    commands += dl.size();
    const std::pair<std::string, std::string> size{"viewport",
                                                   std::to_string(w) + "x" + std::to_string(h)};

    if (!gpu.device) {
      ge::lab::Picture none = ge::lab::create_picture(w, h, {40, 40, 48});
      ge::lab::draw_text(none, 4, 4, "NO GPU ADAPTER", {230, 230, 230});
      // The same shot ids as a GPU's run, so the digest is the same on every row.
      lab.shot({p.id, p.title, p.caption, "a placeholder", {size}}, none, Det::measured);
      lab.shot({p.id + "-diff", p.title + ", OFF THE RASTER", p.caption, "a placeholder", {size}},
               none, Det::measured);
      continue;
    }
    Image cpu = make_image(w, h, hex(0x000000));
    render(dl, cpu);

    auto& d = *gpu.device;
    const ge::gpu::Target out = ge::gpu::create_target(d, "menu-gpu.out", static_cast<uint32_t>(w),
                                                       static_cast<uint32_t>(h));
    gpu.rec.begin_frame(d);
    const auto t0 = std::chrono::steady_clock::now();
    ge::render::prepare_ui(d, gpu.pass, dl, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    prepare_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
    ge::render::encode_ui(gpu.pass, gpu.pipelines, gpu.rec, e, out.view);
    gpu.rec.end_frame(e);
    wgpu::CommandBuffer c = e.Finish();
    d.queue.Submit(1, &c);
    gpu.rec.after_submit(d);
    const ge::gpu::Image got = ge::gpu::read_now(d, out);
    gpu.rec.wait(d);
    draws += gpu.rec.stats.passes[0].draws;
    if (const double ms = gpu.rec.stats.passes[0].gpu_ms; std::isfinite(ms)) {
      gpu_ms += ms;
      ++timed;
    }

    ge::lab::Picture pic{w, h, got.rgba};
    ge::lab::Picture diff = ge::lab::create_picture(w, h, {0, 0, 0});
    int page_worst = 0;
    size_t off = 0;
    for (size_t i = 0; i < cpu.rgba.size() && i < got.rgba.size(); i += 4) {
      int dmax = 0;
      for (size_t k = 0; k < 3; k++) {
        const int dk = std::abs(int{got.rgba[i + k]} - int{cpu.rgba[i + k]});
        dmax = dk > dmax ? dk : dmax;
      }
      page_worst = dmax > page_worst ? dmax : page_worst;
      off += dmax > kLevels;
      const auto v = static_cast<uint8_t>(dmax * kDiffGain > 255 ? 255 : dmax * kDiffGain);
      diff.rgba[i] = v, diff.rgba[i + 1] = v, diff.rgba[i + 2] = v;
    }
    const double share = static_cast<double>(off) * 4 / static_cast<double>(cpu.rgba.size());
    worst = page_worst > worst ? page_worst : worst;
    worst_share = share > worst_share ? share : worst_share;
    if (pinned && share > kShare) {
      lab.fail(p.id + ": " + std::to_string(share * 100).substr(0, 5) +
                   "% of the pixels differ from the raster by more than " +
                   std::to_string(kLevels) + " levels (worst " + std::to_string(page_worst) + ")",
               "Open previews/menu-gpu/" + p.id +
                   "-diff.png; the shader and ui/raster.cpp must "
                   "work coverage, paint and blending alike (render/ui_pass.wgsl). Skill: render.");
    }
    lab.shot({p.id,
              p.title,
              p.caption,
              "the same picture as the `menu` lab's shot of this page: every word sharp, the "
              "slabs' corners round, the gradients smooth",
              {size, {"backdrop", p.backdrop->id}, {"page", p.page}}},
             pic, Det::measured);
    lab.shot({p.id + "-diff",
              p.title + ", OFF THE RASTER",
              "Where the GPU's picture of " + p.id +
                  " differs from the CPU raster's: black is "
                  "equal, each level brighter by " +
                  std::to_string(kDiffGain) + " (worst " + std::to_string(page_worst) + " levels).",
              "black or nearly so: faint dots on edges and in gradients (the dither) only",
              {size}},
             diff, Det::measured);
  }

  lab.metric("pages", static_cast<double>(pages.size()), "count");
  lab.metric("commands", static_cast<double>(commands), "count");
  if (gpu.device) {
    lab.metric("parity.worst", worst, "levels", Det::measured);
    lab.metric("parity.off-share", worst_share * 100, "%", Det::measured);
    lab.metric("draws", draws, "count", Det::measured);
    lab.metric("prepare.ms", prepare_ms / static_cast<double>(pages.size()), "ms", Det::measured);
    if (timed) lab.metric("gpu.ms", gpu_ms / timed, "ms", Det::measured);
    lab.metric("atlas.glyphs", gpu.pass.glyphs_uploaded, "count", Det::measured);
    lab.metric("gpu-errors", gpu.device->errors.load(), "count", Det::measured);
    lab.metric("pipelines-after-load", gpu.device->pipelines_after_load, "count", Det::measured);
    if (gpu.device->errors.load() != 0) {
      lab.fail(std::to_string(gpu.device->errors.load()) + " GPU errors drawing the pages",
               "Read the gpu: lines above. Skill: render.");
    }
    if (!pinned) lab.note("Parity is held only on the pinned lavapipe; here it is measured.");
  }
}

const ge::lab::LabSpec kSpec{
    "menu-gpu",
    ge::lab::LabKind::picture,
    "engine",
    "engine/render/ui_pass.hpp (the 2D pass) against engine/ui/raster.hpp",
    "render",
    "Does the GPU's 2D pass draw the menu's pages as the CPU raster does, and what does a page "
    "cost it?",
    {
        {"viewports", "1280x720,390x844,844x390",
         "screen sizes, WxH, comma-separated; the first is the one every backdrop is shown at"},
        {"seed", "38", "the painted range's noise seed"},
    },
    "--viewports=640x360,390x844",
    run,
};
GE_LAB(kSpec);

}  // namespace
