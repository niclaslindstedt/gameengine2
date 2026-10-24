// LAB `gpu-lava`: LAVA DRAWN (render/liquid.hpp), on the software adapter, in a pit 40 m across in
// a quarry floor. Named views: the molten lake by day, at dusk and at night (its light the black
// body's glow of its temperature, so it reads dim by day and blinding by night with no change
// to the look); the crust's cover from open melt to crusted over; a close look at the plates,
// their cracks and the ropy folds; the crust carried by a flow, four moments two seconds apart;
// and the named looks side by side. It holds the glow's brightness to the black body's (a
// molten pixel's luminance at night against the table's), and fails on a GPU error, a pipeline
// made after the load, or a view with no lava in it.
//
// ge:summary Lab: lava drawn: day, dusk, night, cover, plates close up, flow, the looks
// ge:keywords lab gpu lava crust plates cracks glow blackbody night dusk flow cover pahoehoe
// ge:keywords picture software-adapter
// ge:skill render water labs
// ge:test tests/render_liquid_test.cpp
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "gpu/device.hpp"
#include "lab/lab.hpp"
#include "lab/labs/liquid_scene.hpp"
#include "lab/raster.hpp"
#include "render/liquid.hpp"
#include "water/glow.hpp"

namespace {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace gw = ge::water;
namespace ls = ge::lab::liquid_scene;
using gl::Det;
constexpr double kPi = 3.14159265358979323846;
constexpr uint8_t kBasalt[3] = {58, 54, 52};

gr::LiquidLook look_of(const std::string& name) { return *gr::find_liquid_look(name); }

void put(ge::gpu::Device& d, ls::Scene& s, const std::string& liquid, const gr::LiquidLook& look) {
  const gw::World world = ls::pit_world(liquid, 2);
  if (s.liquids.bodies.empty()) {
    gr::add_liquid_body(d, s.liquids, world.bodies[0], look);
  } else {
    gr::set_liquid_body(d, s.liquids, 0, world.bodies[0], look);
  }
}

// A sheet of `pics` side by side with a label over each.
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

// THE GLOW'S NUMBERS: the black body's luminance by temperature (no GPU needed).
void glow_table(gl::Lab& lab) {
  gl::LabTable table{
      "A black body's glow (water/glow.hpp)", {"T (C)", "luminance (cd/m^2)", "r", "g", "b"}, {}};
  for (const double c : {500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 1100.0, 1150.0, 1200.0}) {
    const gw::Glow g = gw::glow_of(c + 273.15, 0.9);
    table.rows.push_back({ls::fixed(c, 0), ls::fixed(g.luminance, 3), ls::fixed(g.colour.r, 3),
                          ls::fixed(g.colour.g, 3), ls::fixed(g.colour.b, 3)});
    lab.metric("glow." + ls::fixed(c, 0), g.luminance, "cd/m^2");
  }
  lab.table(table);
}

void run(gl::Lab& lab) {
  const uint32_t w = static_cast<uint32_t>(lab.arg_num("width"));
  const uint32_t h = w * 9 / 16;
  auto device = ge::gpu::create_device({.label = "gpu-lava", .validation = 1});
  if (!device) {
    if (ls::gpu_required()) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    // The same deterministic numbers as a run with pictures, so the digest is the same on every
    // row.
    lab.note("No GPU adapter on this machine: no pictures.");
    ls::placeholders(lab, {"day", "dusk", "night", "cover", "close", "flow", "looks"});
    glow_table(lab);
    ls::checked(lab, 0, 0);
    return;
  }
  std::vector<std::unique_ptr<ge::gpu::Device>> devices;
  devices.push_back(std::move(device));
  const auto fresh = [&]() -> ge::gpu::Device& {
    devices.push_back(ge::gpu::create_device({.label = "gpu-lava", .validation = 1}));
    return *devices.back();
  };
  ge::gpu::Device& d = *devices.front();
  ls::Scene big;
  if (!ls::make_scene(d, big, kBasalt, w, h)) {
    lab.fail("the liquid's pipelines did not build", "Read the gpu: lines above. Skill: render.");
    return;
  }
  put(d, big, "lava", look_of("lava"));
  // From the pit's south rim, 5 m over the floor, looking north and down into it.
  const double t = 30;
  struct Case {
    std::string id, title, caption, look_for;
    double sun;
  };
  for (const Case& c : std::vector<Case>{
           {"day", "A LAVA LAKE BY DAY",
            "Basalt lava at 1150 C in a pit 40 m across, its crust 62 % over in plates 1.6 m "
            "across, seen from the rim 6 m up; the sun 35 degrees up behind the camera's left.",
            "black glassy plates with glints of the sun, thin orange cracks between them, open "
            "patches of melt orange-yellow; the glow modest beside the sunlit rock",
            0.6},
           {"dusk", "THE SAME AT DUSK",
            "The same lake with the sun just under the horizon: nothing in the look has changed, "
            "only the light round it.",
            "the cracks and the melt now the brightest things in the picture, a bloom round the "
            "open melt, the crust's plates dark red at their young edges",
            -0.03},
           {"night", "THE SAME AT NIGHT",
            "The same lake on a moonless night: the eye adapted to the dark sky, the lava's own "
            "light is all that shows.",
            "the open melt blown out yellow-white with a wide glow, every crack a bright line, "
            "the crust's older plates black between them",
            -0.4}}) {
    gr::FrameView v =
        ls::view_at(ls::kPitX - 4, ls::kFloor + 6, ls::kPitZ - 30, 0.12, -0.32, c.sun, 2.4, t);
    v.adapt_cd = gr::adapt_for(look_of("lava"), 1.5);
    const gl::Picture pic = ls::draw(d, big, v);
    const gl::Picture dry = ls::draw(d, big, v, false);
    const double share = ls::changed(pic, dry);
    lab.metric(c.id + ".lava-share", share, "", Det::measured);
    if (share < 0.05) {
      lab.fail("the " + c.id + " view shows no lava (" + ls::fixed(share * 100, 1) + "% changed)",
               "Check prepare_liquid and the body's box (engine/render/liquid.cpp). "
               "Skill: render.");
    }
    lab.shot({c.id, c.title, c.caption, c.look_for, {{"sun", ls::fixed(c.sun, 2) + " rad"}}}, pic,
             Det::measured);
  }

  const int tw = static_cast<int>(w / 2), th = static_cast<int>(h / 2);
  // THE COVER: from open melt to crusted over, at dusk from above.
  {
    ls::Scene small;
    ge::gpu::Device& ds = fresh();
    ls::make_scene(ds, small, kBasalt, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (const double cover : {0.15, 0.45, 0.75, 0.97}) {
      gr::LiquidLook l = look_of("lava");
      l.lava.cover = cover;
      put(ds, small, "lava", l);
      gr::FrameView over =
          ls::view_at(ls::kPitX, ls::kFloor + 14, ls::kPitZ - 12, 0.0, -1.0, 0.25, 2.4, t);
      over.adapt_cd = gr::adapt_for(l, 1.5);
      pics.push_back(ls::draw(ds, small, over));
      labels.push_back("COVER " + ls::fixed(cover * 100, 0) + "%");
    }
    lab.shot({"cover",
              "THE CRUST'S COVER",
              "The same lake from 15 m up, the sun 14 degrees up, its crust laid over 15, 45, 75 "
              "and 97 % of it (LavaLook::cover).",
              "open melt bright between rafts of crust at the low covers; at the high ones the "
              "melt only in the cracks and a few windows",
              {}},
             strip(pics, labels, 2), Det::measured);
  }
  // THE PLATES CLOSE UP: from 2.5 m over the crust by day and at night.
  {
    ls::Scene small;
    ge::gpu::Device& ds = fresh();
    ls::make_scene(ds, small, kBasalt, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    gr::LiquidLook l = look_of("lava");
    l.lava.cover = 0.8;
    put(ds, small, "lava", l);
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (const double sun : {0.7, -0.4}) {
      gr::FrameView close =
          ls::view_at(ls::kPitX + 3, ls::kLevel + 2.5, ls::kPitZ - 4, 0.5, -0.75, sun, 2.4, t);
      close.adapt_cd = gr::adapt_for(l, 1.5);
      pics.push_back(ls::draw(ds, small, close));
      labels.push_back(sun > 0 ? "BY DAY" : "AT NIGHT");
    }
    lab.shot({"close",
              "THE CRUST'S PLATES CLOSE UP",
              "The crust from 2.5 m over it, by day and at night.",
              "polygonal plates of black glassy crust, the ropy folds running across the flow, "
              "cracks glowing orange where young and narrow and dark where old; no stripes or "
              "grain from the cracks at the far edge",
              {}},
             strip(pics, labels, 2), Det::measured);
  }
  // THE FLOW: the crust carried at 0.6 m/s, four moments 2 s apart, from above at dusk.
  {
    ls::Scene small;
    ge::gpu::Device& ds = fresh();
    ls::make_scene(ds, small, kBasalt, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    gr::LiquidLook l = look_of("lava");
    l.flow[0] = 0.6;
    put(ds, small, "lava", l);
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (int i = 0; i < 4; ++i) {
      const double at = t + 2.0 * i;
      gr::FrameView over =
          ls::view_at(ls::kPitX, ls::kLevel + 9, ls::kPitZ, 0.0, -kPi / 2 + 0.01, 0.1, 2.4, at);
      over.adapt_cd = gr::adapt_for(l, 1.5);
      pics.push_back(ls::draw(ds, small, over));
      labels.push_back("T = " + ls::fixed(at - t, 0) + " S");
    }
    lab.shot({"flow",
              "THE CRUST CARRIED BY THE FLOW",
              "Straight down from 9 m over the lake, its surface flowing east (right) at 0.6 m/s; "
              "four moments 2 s apart.",
              "the plates moving right by about a metre between frames without stretching; "
              "the pattern crossfading, never jumping",
              {{"flow", "0.6 m/s"}}},
             strip(pics, labels, 2), Det::measured);
  }
  // THE LOOKS.
  {
    ls::Scene small;
    ge::gpu::Device& ds = fresh();
    ls::make_scene(ds, small, kBasalt, static_cast<uint32_t>(tw), static_cast<uint32_t>(th));
    std::vector<gl::Picture> pics;
    std::vector<std::string> labels;
    for (const std::string name : {"lava", "lava-crusted"}) {
      put(ds, small, name, look_of(name));
      for (const double sun : {0.6, -0.2}) {
        gr::FrameView v =
            ls::view_at(ls::kPitX - 4, ls::kFloor + 6, ls::kPitZ - 30, 0.12, -0.32, sun, 2.4, t);
        v.adapt_cd = gr::adapt_for(look_of(name), 1.5);
        pics.push_back(ls::draw(ds, small, v));
        labels.push_back(name + (sun > 0 ? " BY DAY" : " AT NIGHT"));
      }
    }
    lab.shot({"looks",
              "THE NAMED LAVA LOOKS",
              "'lava' (molten, 1150 C, flowing) and 'lava-crusted' (near its solidus, 1100 C, "
              "crusted over) by day and at night.",
              "the crusted lake mostly black with a net of fine glowing cracks, the molten one "
              "with open melt",
              {}},
             strip(pics, labels, 2), Det::measured);
  }
  glow_table(lab);
  int errors = 0, late = 0;
  for (const auto& dev : devices) {
    errors += static_cast<int>(dev->errors.load());
    late += static_cast<int>(dev->pipelines_after_load);
  }
  ls::checked(lab, errors, late);
}

const gl::LabSpec kSpec{
    "gpu-lava",
    gl::LabKind::picture,
    "engine",
    "engine/render/liquid.hpp",
    "render",
    "Does lava draw as a black body's glow through a crust of plates, by day, at dusk and at "
    "night, at every cover, close up and flowing?",
    {{"width", "640", "a view's width, px (16:9)"}},
    "--width=256",
    run};
GE_LAB(kSpec);

}  // namespace
