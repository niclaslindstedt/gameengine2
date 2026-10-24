// The `menu` lab's scenes, shared with `menu-gpu` (gpu_ui_menu_lab.cpp), which draws the same
// pages on the GPU: the painted range, the front door and its style, the five backdrops, the
// viewports a `--viewports` flag names.
//
// ge:summary The menu labs' shared scenes: the painted range, the front door, the backdrops
// ge:keywords lab menu scenes painted-range backdrop front-door viewports
// ge:skill ui labs
// ge:test tests/ui_menu_test.cpp
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <vector>

#include "core/math.hpp"
#include "core/noise.hpp"
#include "ui/background.hpp"
#include "ui/menu.hpp"
#include "ui/menu_view.hpp"
#include "ui/raster.hpp"

namespace ge::lab::menu_scenes {

using namespace ge::ui;

// A winter range under a clear sky, from value noise: the sky's gradient, three ridgelines (ridged
// noise over four octaves, so the peaks come to points), each hazier with distance, snow down to
// a line that wanders, the faces lit from the left by the ridge's slope, dark woods low on the
// nearest.
inline double ridge_height(double x, double seed) {
  double h = 0, amp = 1, scale = 420, sum = 0;
  for (int o = 0; o < 4; o++) {
    const double n = ge::core::value_noise(x, 0, scale, seed + o * 17);
    h += (1 - std::fabs(2 * n - 1)) * amp;  // ridged: a crease at every crossing of 0.5
    sum += amp;
    amp *= 0.48, scale *= 0.45;
  }
  return h / sum;
}

inline Image painted_range(int w, int h, double seed) {
  Image img = make_image(w, h, hex(0x000000));
  const Color sky_top = hex(0x1f4f8f), sky_low = hex(0xc3d8ec), haze = hex(0xc9dbea);
  struct Ridge {
    double base, amp, seed_off;
    Color rock, snow, shade;
    float fade;
  };
  const Ridge ridges[3] = {{0.50, 0.30, 1, hex(0x8193ab), hex(0xe4ecf5), hex(0xa9bbd2), 0.5f},
                           {0.62, 0.34, 2, hex(0x4e5d72), hex(0xf1f5fa), hex(0x9fb2cc), 0.22f},
                           {0.80, 0.30, 3, hex(0x2c3640), hex(0xf8fafc), hex(0x95aac6), 0.0f}};
  const double span = 2400.0;  // the noise's metres across the picture
  for (int x = 0; x < w; x++) {
    const double fx = x * span / w, step = 6 * span / w;
    for (int y = 0; y < h; y++) {
      const float t = static_cast<float>(y) / static_cast<float>(h);
      Color c = lerp(sky_top, sky_low, std::min(1.0f, t * 1.5f));
      for (int r = 0; r < 3; r++) {
        const Ridge& rd = ridges[r];
        const double s = seed + rd.seed_off * 101;
        const double top = (rd.base - rd.amp * ridge_height(fx, s)) * h;
        if (y < top) continue;
        // The face's slope across: positive where the ridge rises to the right (lit from the left).
        const double slope =
            (ridge_height(fx + step, s) - ridge_height(fx - step, s)) * rd.amp * h / 2;
        const double depth = (y - top) / h;
        const double snowline = 0.12 + 0.12 * ge::core::value_noise(fx, 0, 35, s + 5);
        // Lit faces (rising to the right) bright, the others in the sky's blue, blended across
        // the crease so a ridge reads as two planes, not as stripes.
        // Under the crest the faces break into ribs and gullies (2D noise stretched down the
        // fall line), which take over from the crest's slope a little way down.
        const double crest = ge::core::clamp(1 - depth / 0.08, 0, 1);
        const double ribs = ge::core::value_noise(fx + y * 0.35, y * 0.25, 22, s + 7) - 0.5;
        const auto lit = static_cast<float>(
            ge::core::clamp(0.5 + slope / 6 * crest + ribs * 1.6 * (1 - crest * 0.6), 0, 1));
        Color g =
            depth < snowline ? lerp(rd.shade, rd.snow, lit) : lerp(rd.rock, rd.shade, lit * 0.3f);
        if (r == 2 && depth > 0.07) {
          const double n = ge::core::value_noise(fx, y * 2.0, 9, s + 11);
          g = lerp(hex(0x16241e), hex(0x31443a), static_cast<float>(n));  // the woods
        }
        c = lerp(g, haze, rd.fade);
      }
      uint8_t* p = img.rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
      blend_over(p, c);
    }
  }
  return img;
}

inline Menu front_door() {
  Menu m;
  Page& root = m.add_page("root");
  root.items = {
      {.id = "play",
       .text = "PLAY",
       .hint = "Ride the mountain, from the top of any lift",
       .prominent = true},
      {.id = "quick", .text = "QUICK START", .hint = "A mountain dealt at random, no questions"},
      {.id = "options", .text = "OPTIONS", .hint = "Picture, sound, controls", .opens = "options"},
      {.id = "replays", .text = "REPLAYS", .hint = "Runs kept, to watch again", .disabled = true},
      {.id = "quit", .text = "QUIT"}};
  Page& opt = m.add_page("options", "OPTIONS");
  opt.items = {{.id = "h-picture", .text = "PICTURE", .kind = ItemKind::label},
               {.id = "quality",
                .text = "QUALITY",
                .kind = ItemKind::choice,
                .hint = "Fitted to this machine on first start",
                .choices = {"LOW", "MEDIUM", "HIGH", "MAX"},
                .choice = 2},
               {.id = "fps", .text = "SHOW FRAME RATE", .kind = ItemKind::toggle},
               {.id = "h-sound", .text = "SOUND", .kind = ItemKind::label},
               {.id = "volume",
                .text = "MASTER VOLUME",
                .kind = ItemKind::slider,
                .value = 80,
                .min = 0,
                .max = 100,
                .step = 5,
                .unit = "%"},
               {.id = "sound", .text = "SOUND", .kind = ItemKind::toggle, .on = true},
               {.id = "back", .text = "BACK", .kind = ItemKind::back}};
  return m;
}

inline MenuStyle front_style() {
  MenuStyle st;
  st.logo = "FALL LINE";
  st.help = "ENTER SELECT    ESC BACK    ARROWS MOVE";
  st.footer = "v0.1.0 · lab";
  return st;
}

struct Viewport {
  int w, h;
  const char* name;
};

// "1280x720,390x844": every well-formed WxH in the list (empty when none is).
inline std::vector<Viewport> parse_viewports(const std::string& list) {
  std::vector<Viewport> out;
  size_t at = 0;
  while (at < list.size()) {
    const size_t comma = std::min(list.find(',', at), list.size());
    const std::string one = list.substr(at, comma - at);
    int w = 0, h = 0;
    const char* end = one.data() + one.size();
    const auto [wx, we] = std::from_chars(one.data(), end, w);
    const bool by = we == std::errc{} && wx != end && *wx == 'x';
    const auto [hx, he] = by ? std::from_chars(wx + 1, end, h) : std::from_chars_result{};
    if (by && he == std::errc{} && hx == end && w > 0 && h > 0)
      out.push_back({w, h, h > w ? "portrait" : "landscape"});
    at = comma + 1;
  }
  return out;
}

struct Backdrop {
  const char* id;
  const char* what;
  Background bg;
  bool scene = false;
};

// The painted range, its blurred copy (the "live scene"), the drift and the five backdrops over
// them, in the order the labs show them: colour, gradient, glow, image, scene.
// The image backdrop holds `range` by reference, so a Scenes stays where it is made.
struct Scenes {
  Image range, soft;
  Drift drift;
  std::vector<Backdrop> backdrops;
  Scenes() = default;
  Scenes(const Scenes&) = delete;
  Scenes& operator=(const Scenes&) = delete;
};

inline void make_scenes(Scenes& s, double seed) {
  s.range = painted_range(1600, 900, seed);
  s.soft = blurred(s.range, 18);
  Drift& drift = s.drift;
  drift.period_s = 24;
  drift.zoom_from = 1.0f, drift.zoom_to = 1.18f;
  drift.focus_from_x = 0.62f, drift.focus_to_x = 0.4f;
  drift.focus_from_y = drift.focus_to_y = 0.45f;
  std::vector<Backdrop>& backdrops = s.backdrops;
  backdrops.push_back({"color", "a flat colour", Background::color(hex(0x101b27)), false});
  {
    const Stop stops[3] = {{0, hex(0x0e2a47)}, {0.6f, hex(0x14385e)}, {1, hex(0x5d86b0)}};
    backdrops.push_back({"gradient", "a three-stop gradient, top to bottom",
                         Background::gradient(180, stops, 3), false});
  }
  {
    Background bg = Background::gradient(160, hex(0x0b1524), hex(0x1d3550));
    bg.glow(0.72f, 0.3f, 0.55f, hex(0xffc58a, 0.55f)).vignette(0.7f);
    backdrops.push_back({"glow", "a gradient with a warm glow and a vignette", bg, false});
  }
  {
    Background bg = Background::image(s.range, Fit::cover, drift);
    bg.scrim(Side::left, 0.7f, 0.55f).vignette(0.35f);
    backdrops.push_back(
        {"image", "a painted range drifting slowly, a scrim on the column's side", bg, false});
  }
  {
    Background bg = Background::scene();
    bg.tint(hex(0xd6e2ef)).scrim(Side::left, 0.6f, 0.6f).dim(0.12f);
    backdrops.push_back(
        {"scene", "the live scene (here the range, blurred) softened behind a scrim", bg, true});
  }
}

}  // namespace ge::lab::menu_scenes
