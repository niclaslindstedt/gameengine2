// LAB `fetch`: a lake's sea read off its outline (engine/water/fetch.hpp) and laid on it
// (engine/water/world.hpp): the effective fetch over an irregular lake for a wind from four
// quarters; the height the wind grows at every point for one of them; and the lake's surface from
// above, sampled through the one query, calm under the windward shore and rough at the lee. A
// table reads the fetch, height and period at the windward shore, the middle and the lee.
//
// ge:summary Lab: a lake's fetch from four winds, the height it grows, the surface the query gives
// ge:keywords lab water fetch lake outline lee windward significant-height surface sample
// ge:skill water labs
// ge:test tests/water_fetch_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "water/fetch.hpp"
#include "water/world.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

constexpr double kPi = 3.141592653589793;
constexpr double kSpan = 3000;  // m across every map
constexpr int kSize = 360;      // px a map

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

// An irregular lake about 2.4 km long along z, with a bay to the west.
std::vector<gw::Point> lake_outline() {
  std::vector<gw::Point> out;
  for (int i = 0; i < 72; ++i) {
    const double a = i * kPi * 2 / 72;
    double r = 1.0 + 0.18 * ge::core::libm::sin(3 * a + 0.4) + 0.1 * ge::core::libm::cos(5 * a);
    if (a > 4.2 && a < 4.9) r *= 1.35;  // the bay to the west
    out.push_back(
        {1500 + 520 * r * ge::core::libm::sin(a), 1500 + 1150 * r * ge::core::libm::cos(a)});
  }
  return out;
}

gl::Rgba ramp(double v, double top) {
  const double s = std::fmin(1.0, std::fmax(0.0, v / top));
  return {static_cast<uint8_t>(225 - 205 * s), static_cast<uint8_t>(238 - 160 * s),
          static_cast<uint8_t>(250 - 110 * s)};
}

void outline(gl::Picture& p, const std::vector<gw::Point>& lake, int ox, int oy) {
  const double per = kSpan / kSize;
  for (size_t i = 0, j = lake.size() - 1; i < lake.size(); j = i++) {
    gl::draw_line(p, ox + lake[j].x / per, oy + kSize - lake[j].z / per, ox + lake[i].x / per,
                  oy + kSize - lake[i].z / per, {40, 60, 40});
  }
}

void arrow(gl::Picture& p, int cx, int cy, double toward) {
  const double dx = ge::core::libm::sin(toward), dy = -ge::core::libm::cos(toward);
  gl::draw_line(p, cx - 18 * dx, cy - 18 * dy, cx + 18 * dx, cy + 18 * dy, {150, 30, 30});
  gl::fill_disc(p, cx + 18 * dx, cy + 18 * dy, 3, {150, 30, 30});
}

void quarters(gl::Lab& lab, const std::vector<gw::Point>& lake) {
  gl::Picture sheet = gl::create_picture(kSize * 4 + 50, kSize + 40, {255, 255, 255});
  const char* names[] = {"FROM THE NORTH", "FROM THE EAST", "FROM THE SOUTH", "FROM THE WEST"};
  for (int q = 0; q < 4; ++q) {
    const double toward = q * kPi / 2 + kPi;  // from the north blows toward the south
    const gw::FetchGrid g = gw::bake_fetch(lake, toward, 96);
    const int ox = 10 + q * (kSize + 10), oy = 30;
    const double per = kSpan / kSize;
    for (int py = 0; py < kSize; py += 2) {
      for (int px = 0; px < kSize; px += 2) {
        const double x = px * per, z = (kSize - py) * per;
        if (!gw::inside(lake, x, z)) continue;
        gl::fill_rect(sheet, ox + px, oy + py, 2, 2, ramp(gw::fetch_at(g, x, z), 2400));
      }
    }
    outline(sheet, lake, ox, oy);
    arrow(sheet, ox + kSize - 30, oy + 30, toward);
    gl::draw_text(sheet, ox, 8, names[q], {20, 30, 50}, 2);
    lab.metric(std::string("longest.q") + std::to_string(q), g.longest, "m");
  }
  lab.shot({"fetch",
            "THE EFFECTIVE FETCH OVER A LAKE, FOUR WINDS",
            "A 2.4 km lake from above (north up) with the effective fetch at every point for a "
            "wind from each quarter (the red arrow is the way it blows). Darker is a longer run "
            "over water: 15 rays upwind, weighted toward the wind.",
            "light under the windward shore, darkest at the lee; the western bay lighter than "
            "the open water beside it; the long axis darkest for the north and south winds",
            {{"wind", lab.arg("wind")}}},
           sheet);
}

void heights(gl::Lab& lab, const std::vector<gw::Point>& lake) {
  const double wind = lab.arg_num("wind");
  ge::wind::Field field;
  field.mean = wind;
  field.from = 0.25;  // from just east of north
  gw::World world(field);
  world.seed = static_cast<uint32_t>(lab.arg_num("seed"));
  gw::add_lake(world, lake, 100, {}, {}, 20);
  const gw::Body& body = world.bodies[0];
  const double per = kSpan / kSize;
  gl::Picture sheet = gl::create_picture(kSize * 2 + 30, kSize + 40, {255, 255, 255});
  // The height the wind grows at each point off its own fetch.
  double top = gw::grow(wind, body.fetch.longest, 0).hs;
  for (int py = 0; py < kSize; py += 2) {
    for (int px = 0; px < kSize; px += 2) {
      const double x = px * per, z = (kSize - py) * per;
      if (!gw::inside(lake, x, z)) continue;
      const double hs = gw::grow(wind, gw::fetch_at(body.fetch, x, z), 0).hs;
      gl::fill_rect(sheet, 10 + px, 30 + py, 2, 2, ramp(hs, top));
    }
  }
  outline(sheet, lake, 10, 30);
  arrow(sheet, 10 + kSize - 30, 60, field.from + kPi);
  gl::draw_text(sheet, 10, 8, "HS GROWN, 0 TO " + fixed(top, 2) + " M", {20, 30, 50}, 2);
  // The surface the one query gives at t = 40 s in two 30 m patches on the long axis, lit off its
  // slopes (the waves are a metre or two long: a whole-lake view could not resolve them).
  const int ox = 20 + kSize;
  constexpr double kPatch = 30;  // m across
  constexpr int kPx = 170;
  const struct {
    double z;
    const char* name;
  } patches[] = {{2650, "WINDWARD (NORTH)"}, {700, "LEE (SOUTH)"}};
  int row = 0;
  for (const auto& patch : patches) {
    const int oy = 30 + row * (kPx + 20);
    for (int py = 0; py < kPx; ++py) {
      for (int px = 0; px < kPx; ++px) {
        const double x = 1500 + (px - 0.5 * kPx) * kPatch / kPx;
        const double z = patch.z + (0.5 * kPx - py) * kPatch / kPx;
        const gw::Sample s = gw::sample(world, x, 99, z, 40);
        if (s.body < 0) {
          gl::set_pixel(sheet, ox + px, oy + py, {214, 222, 206});
          continue;
        }
        const double lit = std::fmax(0.0, -0.6 * s.nx + 0.5 * s.ny - 0.62 * s.nz);
        const double l = std::fmin(1.0, std::fmax(0.0, 0.55 + 2.5 * (lit - 0.5)));
        gl::set_pixel(sheet, ox + px, oy + py,
                      {static_cast<uint8_t>(20 + 120 * l), static_cast<uint8_t>(60 + 140 * l),
                       static_cast<uint8_t>(90 + 160 * l)});
      }
    }
    gl::draw_text(sheet, ox + kPx + 10, oy + 4, patch.name, {20, 30, 50}, 1);
    gl::draw_text(sheet, ox + kPx + 10, oy + 18, "30 M ACROSS", {90, 90, 90}, 1);
    ++row;
  }
  gl::draw_text(sheet, ox, 8, "THE SURFACE, T = 40 S", {20, 30, 50}, 2);
  lab.shot({"lake",
            "ONE WIND OVER THE LAKE: THE HEIGHT IT GROWS AND THE SURFACE",
            "Left: the significant height the wind (" + fixed(wind, 0) +
                " m/s from just east of north) grows at each point off that point's own fetch. "
                "Right: two 30 m patches of the lake's surface from the one water query at t = 40 "
                "s, under the windward shore and at the lee, lit from the south-west off their "
                "slopes (contrast stretched); the waves are laid once for the longest fetch and "
                "scaled by each point's own.",
            "the height darkest at the south (lee) end and lightest under the north shore; the "
            "windward patch low and gentle, the lee patch busy with crests running across the wind",
            {{"wind", lab.arg("wind")}, {"seed", lab.arg("seed")}, {"t", "40"}}},
           sheet);
  gl::LabTable table{"Along the lake's long axis: the fetch and the sea it grows",
                     {"where", "z m", "fetch m", "Hs grown m", "Tp s", "surface sd m"},
                     {}};
  double windward_sd = 0, lee_sd = 0;
  for (const double z : {2700.0, 2200.0, 1700.0, 1100.0, 650.0}) {
    const double f = gw::fetch_at(body.fetch, 1500, z);
    const gw::Sea sea = gw::grow(wind, f, 0);
    double sum = 0, sum2 = 0;
    const int n = 1200;
    for (int i = 0; i < n; ++i) {
      const double h = gw::sample(world, 1500, 99, z, i * 0.25).surface - 100;
      sum += h;
      sum2 += h * h;
    }
    const double sd = std::sqrt(std::fmax(0.0, sum2 / n - (sum / n) * (sum / n)));
    if (z == 2700.0) windward_sd = sd;
    if (z == 650.0) lee_sd = sd;
    table.rows.push_back({z > 2500   ? "windward"
                          : z < 1000 ? "lee"
                                     : "middle",
                          fixed(z, 0), fixed(f, 0), fixed(sea.hs, 3), fixed(sea.tp, 2),
                          fixed(sd, 3)});
  }
  lab.table(table);
  lab.metric("lee-over-windward", windward_sd > 0 ? lee_sd / windward_sd : 0, "share");
  if (lee_sd <= windward_sd) {
    lab.fail("the lee shore is no rougher than the windward one",
             "Read add_lake and sample_lake in engine/water/world.cpp (the waves scaled by the "
             "root of each point's fetch), then rerun `make lab LAB=fetch` (skill: water).");
  }
}

void run(gl::Lab& lab) {
  const std::vector<gw::Point> lake = lake_outline();
  quarters(lab, lake);
  heights(lab, lake);
}

const gl::LabSpec kSpec{
    "fetch",
    gl::LabKind::picture,
    "engine",
    "engine/water/fetch.hpp, engine/water/world.hpp",
    "water",
    "Is a lake calm under its windward shore and rough at its lee, and does the one query's "
    "surface show it?",
    {{"wind", "12", "the mean wind at 10 m, m/s"},
     {"seed", "38", "the seed the lake's components are drawn off"}},
    "",
    run};
GE_LAB(kSpec);

}  // namespace
