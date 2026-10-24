// LAB `gpu-slime`: SLIME DRAWN (render/liquid.hpp), on the software adapter, in a pit 40 m across
// in a quarry floor with stones on its bed. Named views: the five named slimes side by side from
// the rim (toxic, gel, mucus, sludge, tar); each close up, where the bubbles, the scum and the
// oily film show; the toxic slime glowing at night; the gel seen into, the stones on the bed
// through it; and one gale over three slimes, the ripples each keeps after its viscosity. It
// fails on a GPU error, a pipeline made after the load, or a view with no slime in it.
//
// ge:summary Lab: slime drawn: five looks, close up, glowing at night, seen into, in a gale
// ge:keywords lab gpu slime toxic gel mucus sludge tar bubbles scum film iridescence ripples
// ge:keywords kubelka-munk glow picture software-adapter
// ge:skill render water labs
// ge:test tests/render_liquid_test.cpp
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "gpu/device.hpp"
#include "lab/lab.hpp"
#include "lab/labs/liquid_scene.hpp"
#include "lab/raster.hpp"
#include "render/liquid.hpp"

namespace {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace gw = ge::water;
namespace ls = ge::lab::liquid_scene;
using gl::Det;
constexpr uint8_t kConcrete[3] = {118, 114, 104};
const std::vector<std::string> kSlimes = {"slime", "slime-gel", "slime-mucus", "slime-sludge",
                                          "slime-tar"};

void put(ge::gpu::Device& d, ls::Scene& s, const std::string& name, double wind) {
  const gw::World world = ls::pit_world(name, wind);
  const gr::LiquidLook& look = *gr::find_liquid_look(name);
  const double toward = world.wind.from + 3.14159265358979;
  if (s.liquids.bodies.empty()) {
    gr::add_liquid_body(d, s.liquids, world.bodies[0], look, wind, toward, 2 * ls::kHalf);
  } else {
    gr::set_liquid_body(d, s.liquids, 0, world.bodies[0], look, wind, toward, 2 * ls::kHalf);
  }
}

gl::Picture strip(const std::vector<gl::Picture>& pics, const std::vector<std::string>& labels,
                  int columns) {
  const int tw = pics[0].width, th = pics[0].height;
  const int rows = (static_cast<int>(pics.size()) + columns - 1) / columns;
  gl::Picture sheet =
      gl::create_picture(columns * (tw + 10) + 10, rows * (th + 14) + 6, {250, 250, 248});
  for (size_t i = 0; i < pics.size(); ++i) {
    const int ox = 10 + static_cast<int>(i % columns) * (tw + 10);
    const int oy = 4 + static_cast<int>(i / columns) * (th + 14);
    gl::draw_text(sheet, ox, oy, labels[i], {24, 26, 32});
    ls::paste(sheet, pics[i], ox, oy + 10);
  }
  return sheet;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

void run(gl::Lab& lab) {
  const uint32_t w = static_cast<uint32_t>(lab.arg_num("width"));
  const uint32_t h = w * 9 / 16;
  auto device = ge::gpu::create_device({.label = "gpu-slime", .validation = 1});
  if (!device) {
    if (ls::gpu_required()) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    // The same deterministic numbers as a run with pictures, so the digest is the same on every
    // row.
    lab.note("No GPU adapter on this machine: no pictures.");
    ls::placeholders(lab, {"looks", "close", "glow", "gel", "gale"});
    ls::checked(lab, 0, 0);
    return;
  }
  std::vector<std::unique_ptr<ge::gpu::Device>> devices;
  devices.push_back(std::move(device));
  const auto fresh = [&]() -> ge::gpu::Device& {
    devices.push_back(ge::gpu::create_device({.label = "gpu-slime", .validation = 1}));
    return *devices.back();
  };
  const double t = 20;
  const int tw = static_cast<int>(w / 2), th = static_cast<int>(h / 2);
  // THE LOOKS from the rim, by day.
  {
    ge::gpu::Device& d = *devices.front();
    ls::Scene s;
    if (!ls::make_scene(d, s, kConcrete, static_cast<uint32_t>(tw), static_cast<uint32_t>(th))) {
      lab.fail("the liquid's pipelines did not build", "Read the gpu: lines above. Skill: render.");
      return;
    }
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    const gr::FrameView v =
        ls::view_at(ls::kPitX - 4, ls::kFloor + 5, ls::kPitZ - 28, 0.12, -0.3, 0.6, 2.4, t);
    for (const std::string& name : kSlimes) {
      put(d, s, name, 3);
      const gl::Picture pic = ls::draw(d, s, v);
      const gl::Picture dry = ls::draw(d, s, v, false);
      const double share = ls::changed(pic, dry);
      lab.metric(name + ".share", share, "", Det::measured);
      if (share < 0.05) {
        lab.fail("the " + name + " view shows no slime (" + ls::fixed(share * 100, 1) + "%)",
                 "Check the body's box and prepare_liquid (engine/render/liquid.cpp). "
                 "Skill: render.");
      }
      pics.push_back(pic);
      labels.push_back(upper(name));
    }
    pics.push_back(ls::draw(d, s, v, false));
    labels.push_back("THE PIT EMPTY");
    lab.shot({"looks",
              "THE FIVE SLIMES",
              "A pit 40 m across, 3 m deep with stones on its bed, filled with each named slime "
              "and seen from its rim 5 m up under a light air; the sun 35 degrees up. The last "
              "tile is the pit empty.",
              "the toxic slime an opaque bright green; the gel clear green-cyan with the stones "
              "showing through; the mucus a pale milky yellow-green and glossy; the sludge "
              "olive-brown under drifting grey-brown scum; the tar black glass mirroring the sky",
              {}},
             strip(pics, labels, 3), Det::measured);
  }
  // CLOSE UP: 1 m over each, looking down.
  {
    ge::gpu::Device& d = fresh();
    ls::Scene s;
    ls::make_scene(d, s, kConcrete, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    const gr::FrameView v =
        ls::view_at(ls::kPitX + 2, ls::kLevel + 1.0, ls::kPitZ - 6, 0.3, -0.7, 0.5, 2.0, t);
    for (const std::string& name : kSlimes) {
      put(d, s, name, 3);
      pics.push_back(ls::draw(d, s, v));
      labels.push_back(upper(name));
    }
    lab.shot({"close",
              "THE SLIMES CLOSE UP",
              "Each slime from 1 m over it, looking down and out toward the sun's side.",
              "bubbles swelling and bursting into rings on the toxic slime, the mucus and the "
              "sludge; the scum's patches matte against the sludge's gloss with an oily rainbow "
              "between; the tar's slow fat bubble and its film's interference colours",
              {}},
             strip(pics, labels, 3), Det::measured);
  }
  // THE TOXIC SLIME AT DUSK AND AT NIGHT, keyed to its own glow.
  {
    ge::gpu::Device& d = fresh();
    ls::Scene s;
    ls::make_scene(d, s, kConcrete, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    put(d, s, "slime", 2);
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (const double sun : {0.6, -0.05, -0.4}) {
      gr::FrameView v =
          ls::view_at(ls::kPitX - 4, ls::kFloor + 5, ls::kPitZ - 28, 0.12, -0.3, sun, 2.4, t);
      v.adapt_cd = gr::adapt_for(*gr::find_liquid_look("slime"), 1.0);
      pics.push_back(ls::draw(d, s, v));
      labels.push_back(sun > 0 ? "BY DAY" : (sun > -0.2 ? "AT DUSK" : "AT NIGHT"));
    }
    lab.shot({"glow",
              "THE TOXIC SLIME'S OWN LIGHT",
              "The toxic slime by day, at dusk and at night, the eye keyed to its glow (60 cd/m^2 "
              "of green, pulsing slowly).",
              "by day an opaque green; at dusk and at night a green glow brighter in the bubbles, "
              "the pit's rim lit only by the sky",
              {}},
             strip(pics, labels, 3), Det::measured);
  }
  // THE GEL SEEN INTO: from over the stones.
  {
    ge::gpu::Device& d = fresh();
    ls::Scene s;
    ls::make_scene(d, s, kConcrete, w, h);
    put(d, s, "slime-gel", 1);
    const gr::FrameView v =
        ls::view_at(ls::kPitX - 6, ls::kLevel + 3, ls::kPitZ - 10, 0.2, -0.55, 0.9, 2.0, t);
    lab.shot({"gel",
              "THE GEL SEEN INTO",
              "The clear gel from 3 m over it, looking down toward the stones on the pit's bed "
              "2 to 3 m under its surface.",
              "the stones visible through the gel, greener and dimmer the deeper they lie; the "
              "sky's reflection on top only where the view grazes",
              {}},
             ls::draw(d, s, v), Det::measured);
  }
  // A GALE OVER THREE SLIMES: the ripples each keeps.
  {
    ge::gpu::Device& d = fresh();
    ls::Scene s;
    ls::make_scene(d, s, kConcrete, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    const gr::FrameView v =
        ls::view_at(ls::kPitX - 4, ls::kLevel + 2, ls::kPitZ - 16, 0.1, -0.25, 0.25, 0.3, t);
    for (const std::string name : {"slime-mucus", "slime", "slime-gel"}) {
      put(d, s, name, 18);
      pics.push_back(ls::draw(d, s, v));
      const gw::Stuff st = gw::liquid(name);
      labels.push_back(upper(name) + " " + ls::fixed(st.liquid.viscosity, 2) + " PA S");
    }
    lab.shot({"gale",
              "ONE GALE OVER THREE SLIMES",
              "18 m/s over the pit (fetch 40 m) on the mucus (0.05 Pa s), the toxic slime (2 Pa s) "
              "and the gel (30 Pa s, a 20 Pa yield), into the low sun: the wind's ripples are "
              "each cut by the slime's viscosity (water/liquid.hpp's wind_keep).",
              "the mucus rippled across its surface and the sun's glint broken up; the toxic "
              "slime and the gel glassy, the glint one smooth streak",
              {{"wind", "18 m/s"}}},
             strip(pics, labels, 3), Det::measured);
  }
  int errors = 0, late = 0;
  for (const auto& dev : devices) {
    errors += static_cast<int>(dev->errors.load());
    late += static_cast<int>(dev->pipelines_after_load);
  }
  ls::checked(lab, errors, late);
}

const gl::LabSpec kSpec{
    "gpu-slime",
    gl::LabKind::picture,
    "engine",
    "engine/render/liquid.hpp",
    "render",
    "Do the five slimes draw as five different stuffs (opaque, clear, milky, scummed, black), "
    "close up, glowing at night, seen into and in a gale?",
    {{"width", "640", "a view's width, px (16:9)"}},
    "--width=256",
    run};
GE_LAB(kSpec);

}  // namespace
