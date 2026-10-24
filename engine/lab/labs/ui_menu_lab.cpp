// LAB `menu`: a game's front door drawn by the 2D layer (engine/ui) on the CPU, LOOKED AT. One
// menu (a root page with a prominent PLAY, an options page with every item kind) over every kind
// of backdrop the engine offers (a colour, a gradient, a glow with a vignette, a slowly drifting
// picture, the live scene softened behind a scrim), at the three reference viewports (a desktop,
// a phone held upright, a phone on its side), then driven: the focus moved and the highlight
// caught gliding, a page opened mid-fade, and the picture's drift at its two ends.
//
// The picture behind is painted here from value noise (a winter range under a clear sky), so the
// lab needs no asset and no GPU. Every layout is held to `layout_problems`: an item's words wider
// than its slab, a slab out of the safe area or two overlapping fail the lab.
//
// ge:summary Lab: the menu and its backdrops pictured at three viewports, driven, layout-checked
// ge:keywords lab menu main-menu front-door backdrop background gradient vignette scrim drift
// ge:keywords scene viewport portrait landscape phone desktop focus glide ui screenshot
// ge:skill ui labs
// ge:test tests/ui_menu_test.cpp
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "lab/lab.hpp"
#include "ui/background.hpp"
#include "ui/menu_view.hpp"
#include "ui/raster.hpp"

#include "ui_menu_scenes.hpp"

namespace {

using namespace ge::ui;
using namespace ge::lab::menu_scenes;

ge::lab::Picture to_picture(Image img) {
  ge::lab::Picture p;
  p.width = img.width, p.height = img.height;
  p.rgba = std::move(img.rgba);
  return p;
}

}  // namespace

namespace {

void run(ge::lab::Lab& lab) {
  Scenes scenes;
  make_scenes(scenes, lab.arg_num("seed"));
  const Image& soft = scenes.soft;
  const Drift& drift = scenes.drift;
  const std::vector<Backdrop>& backdrops = scenes.backdrops;
  const std::vector<Viewport> viewports = parse_viewports(lab.arg("viewports"));
  if (viewports.empty()) {
    lab.fail("--viewports=" + lab.arg("viewports") + " named no WxH size",
             "run make lab LAB=menu ARGS=--viewports=1280x720,390x844");
    return;
  }

  const MenuStyle st = front_style();
  int problems_total = 0;
  double raster_ms = 0;
  auto shoot = [&](const std::string& id, const std::string& title, const std::string& caption,
                   const std::string& look, Menu& m, MenuView& view, const Backdrop& b, int w,
                   int h, double t) {
    const MenuLayout L = layout_menu(m, w, h, st);
    for (const std::string& p : layout_problems(m, L, st)) {
      problems_total++;
      std::string what = id;
      what.append(": ").append(p);
      lab.fail(what,
               "fix the layout in engine/ui/menu_view.cpp or the menu's words; "
               "rerun `make lab LAB=menu`. Skill: ui.");
    }
    DrawList dl;
    dl.begin(w, h);
    draw_background(dl, b.bg, {0, 0, float(w), float(h)}, t, b.scene ? &soft : nullptr);
    view.draw(dl, m, L, st);
    Image img = make_image(w, h, hex(0x000000));
    const auto t0 = std::chrono::steady_clock::now();
    render(dl, img);
    raster_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    lab.shot({id,
              title,
              caption,
              look,
              {{"viewport", std::to_string(w) + "x" + std::to_string(h)},
               {"backdrop", b.id},
               {"page", m.top().id},
               {"t", std::to_string(t)},
               {"scale", std::to_string(L.scale).substr(0, 4)}}},
             to_picture(std::move(img)));
  };

  // 1. The front door over every backdrop, at the first viewport.
  const Viewport& main = viewports.front();
  for (const Backdrop& b : backdrops) {
    Menu m = front_door();
    m.input({});
    MenuView view;
    view.settle(m, layout_menu(m, main.w, main.h, st));
    shoot("door-" + std::string(b.id), "FRONT DOOR OVER " + std::string(b.id),
          "The root page (the game's name, PLAY lit as the focus, a disabled REPLAYS) over " +
              std::string(b.what) + ".",
          "every word readable over the backdrop; PLAY's slab lit red and bigger than the rest; "
          "REPLAYS dimmed; the help bottom left and the stamp bottom right",
          m, view, b, main.w, main.h, 0);
  }

  // 2. Both pages at every viewport, over the drifting picture.
  const Backdrop& image = backdrops[3];
  for (const Viewport& v : viewports) {
    for (const char* page : {"root", "options"}) {
      Menu m = front_door();
      if (std::string(page) == "options") m.open("options");
      m.input({});
      if (std::string(page) == "options") m.focus("volume");
      MenuView view;
      view.settle(m, layout_menu(m, v.w, v.h, st));
      const std::string size = std::to_string(v.w) + "x" + std::to_string(v.h);
      shoot(std::string(page) + "-" + size,
            std::string(page) + " PAGE AT " + size + " (" + v.name + ")",
            std::string("The ") + page + " page laid out for a " + v.name + " screen of " + size +
                (v.h > v.w ? ": the column runs the width, anchored low, the picture above it."
                           : ": the column stands left, the picture's subject to its right."),
            "nothing clipped or overlapping; the words fit their slabs; the column clear of the "
            "edges by its margin; the focused item's hint under the column",
            m, view, image, v.w, v.h, 0);
    }
  }

  // 3. Driven: the focus moved down twice, the highlight caught 2 and 5 frames into its glide,
  // then OPTIONS opened and caught mid-fade.
  {
    Menu m = front_door();
    m.input({});
    MenuLayout L = layout_menu(m, main.w, main.h, st);
    MenuView view;
    view.settle(m, L);
    MenuInput down;
    down.down = true;
    m.input(down);
    m.input(down);
    int settled = -1;
    const Rect target = L.items[static_cast<size_t>(m.top().focus)];
    for (int frames = 0; frames < 120; frames++) {
      view.frame(m, L, 1.0 / 60);
      if (settled < 0 && std::fabs(view.highlight().y - target.y) < 0.5f) settled = frames + 1;
      if (frames == 1 || frames == 4) {
        shoot("glide-" + std::to_string(frames + 1),
              "HIGHLIGHT GLIDING, FRAME " + std::to_string(frames + 1),
              "PLAY to OPTIONS in two presses at 60 Hz; the highlight eases to the focus rather "
              "than jumping, " +
                  std::to_string(frames + 1) + " frames in.",
              "the red slab between PLAY and OPTIONS, nearer OPTIONS on the later frame; the words "
              "stay on their own slabs",
              m, view, backdrops[1], main.w, main.h, 0);
      }
    }
    lab.metric("glide.frames-to-settle", settled, "count");
    if (settled < 0 || settled > 15)
      lab.fail(
          "the highlight took " + std::to_string(settled) + " frames at 60 Hz to reach its item",
          "it should settle within a quarter second: check MenuView::frame's rate in "
          "engine/ui/menu_view.cpp. Skill: ui.");
    MenuInput accept;
    accept.accept = true;
    const auto ev = m.input(accept);
    lab.metric("drive.events-on-open", static_cast<double>(ev.size()), "count");
    L = layout_menu(m, main.w, main.h, st);
    view.frame(m, L, 1.0 / 60);
    view.frame(m, L, 1.0 / 60);
    view.frame(m, L, 1.0 / 60);
    shoot("open-fading", "OPTIONS OPENING, 3 FRAMES IN",
          "OPTIONS pressed: the page fades up and slides in from the left over about 0.2 s; caught "
          "three frames in.",
          "the page part-transparent and a little left of its place; the backdrop unchanged", m,
          view, backdrops[1], main.w, main.h, 0);
  }

  // 4. The drift at both ends of its period.
  for (const double t : {0.0, drift.period_s / 2.0}) {
    Menu m = front_door();
    m.input({});
    MenuView view;
    view.settle(m, layout_menu(m, main.w, main.h, st));
    const std::string at = t == 0 ? "start" : "half";
    shoot("drift-" + at, "DRIFT AT ITS " + std::string(t == 0 ? "START" : "TURN"),
          "The drifting picture at t = " + std::to_string(static_cast<int>(t)) +
              " s of a 24 s period: it zooms from 1.0 to 1.18 and pans left, then back.",
          "the later frame closer in and shifted; the menu exactly where it was", m, view, image,
          main.w, main.h, t);
  }

  lab.metric("layout.problems", problems_total, "count");
  lab.metric("viewports", static_cast<double>(viewports.size()), "count");
  lab.metric("raster.ms", raster_ms, "ms", ge::lab::Det::measured);
}

const ge::lab::LabSpec kSpec{
    "menu",
    ge::lab::LabKind::picture,
    "engine",
    "engine/ui/menu.hpp, engine/ui/menu_view.hpp, engine/ui/background.hpp, engine/ui/raster.hpp",
    "ui",
    "Does a menu read over every kind of backdrop at a desktop's, an upright phone's and a phone "
    "on its side's size, and does it move smoothly?",
    {
        {"viewports", "1280x720,390x844,844x390",
         "screen sizes, WxH, comma-separated; the first "
         "is the one every backdrop is shown at"},
        {"seed", "38", "the painted range's noise seed"},
    },
    "--viewports=640x360,390x844,844x390",
    run,
};
GE_LAB(kSpec);

}  // namespace
