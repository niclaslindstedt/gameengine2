// LAB `wind`: the engine's wind (engine/wind) read the way its readers read it, so a change to the
// field shows as numbers and pictures: the gusts' series for three grounds and the intensity each
// really gives against the one asked; the log-law profile over each ground; a plan of a sheltered
// field (a wood that keeps less of the wind and turns it round its edges) with a rotor's downwash
// and a whirl added, as arrows over the speed; and the sources' own decay. The mountain's wind is
// one field of this module, held to its bits by tests/golden/mountain_air.txt.
//
// ge:summary Lab: the wind's gusts, intensity, log profile, shelter plan and local sources
// ge:keywords lab wind gust intensity gust-factor log-law profile roughness shelter jet whirl
// ge:skill water labs
// ge:test tests/wind_field_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "wind/field.hpp"
#include "wind/sources.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::wind;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot(
      {id,
       chart.title,
       caption,
       look_for,
       {{"mean", lab.arg("mean")}, {"seed", lab.arg("seed")}, {"minutes", lab.arg("minutes")}}},
      gl::to_picture(chart.plot));
}

struct Ground {
  const char* name;
  double z0;
};
constexpr Ground kGrounds[] = {{"water", gw::kRoughness::water},   {"snow", gw::kRoughness::snow},
                               {"grass", gw::kRoughness::grass},   {"crops", gw::kRoughness::crops},
                               {"forest", gw::kRoughness::forest}, {"town", gw::kRoughness::town}};

void gusts(gl::Lab& lab) {
  const double mean = lab.arg_num("mean");
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));
  const int steps = static_cast<int>(lab.arg_num("minutes") * 60 * 10);
  gl::LineChart line;
  line.frame = {
      "THE WIND'S SPEED OVER " + lab.arg("minutes") + " MINUTES, THREE GROUNDS",
      "the mean " + fixed(mean, 1) + " m/s at 10 m, gusts laid for each ground's intensity",
      "t (s)", "speed (m/s)"};
  line.spikes = false;
  gl::LabTable table{
      "Gusts: the intensity asked and measured, the gust factor",
      {"ground", "z0 m", "asked I", "measured I", "gust factor 3 s", "min m/s", "max m/s"},
      {}};
  for (const char* name : {"water", "grass", "forest"}) {
    double z0 = 0;
    for (const Ground& g : kGrounds) {
      if (std::string(g.name) == name) z0 = g.z0;
    }
    gw::Field f;
    f.mean = mean;
    f.from = 0.6;
    f.roughness = z0;
    const double asked = gw::turbulence_for(z0);
    gw::lay_gusts(f, seed, asked);
    gl::Trace tr{name, {}, {}};
    double sum = 0, sum2 = 0, lo = 1e9, hi = 0, peak3 = 0, run = 0;
    std::vector<double> window;
    for (int k = 0; k < steps; ++k) {
      const double t = k * 0.1;
      const double v = gw::open_at(f, t).speed;
      sum += v;
      sum2 += v * v;
      lo = std::fmin(lo, v);
      hi = std::fmax(hi, v);
      window.push_back(v);
      run += v;
      if (window.size() > 30) run -= window[window.size() - 31];
      if (window.size() >= 30) peak3 = std::fmax(peak3, run / 30);
      if (k % 10 == 0) {
        tr.x.push_back(t);
        tr.y.push_back(v);
      }
    }
    const double m = sum / steps, measured = std::sqrt(sum2 / steps - m * m) / m;
    line.traces.push_back(tr);
    table.rows.push_back({name, fixed(z0, 4), fixed(asked, 3), fixed(measured, 3),
                          fixed(peak3 / m, 2), fixed(lo, 2), fixed(hi, 2)});
    lab.metric(std::string("intensity.") + name, measured, "share");
    lab.metric(std::string("gust-factor.") + name, peak3 / m, "share");
    if (std::fabs(measured - asked) > 0.1 * asked && steps >= 3000) {
      lab.fail(std::string("the gusts over ") + name + " give an intensity of " +
                   fixed(measured, 3) + ", asked " + fixed(asked, 3),
               "Read lay_gusts in engine/wind/field.cpp (the shares are scaled for the asked "
               "standard deviation), then rerun `make lab LAB=wind` (skill: water).");
    }
  }
  line.rules.push_back(mean);
  chart_shot(lab, "gusts", gl::line_chart(line),
             "The wind's speed at 10 m against the clock over open water, grass and forest, each "
             "with the default gusts laid for the intensity its ground gives (1 / ln(10 / z0)). "
             "The dashed line is the mean.",
             "water wavers least and forest most; only the forest's deepest lulls touch zero; no "
             "step or flat stretch");
  lab.table(table);
}

void profile(gl::Lab& lab) {
  gl::LineChart line;
  line.frame = {"THE LOG LAW: THE WIND BY HEIGHT OVER EACH GROUND",
                "speed as a share of the speed at 10 m", "share of the 10 m speed", "height (m)"};
  line.spikes = false;
  line.log_y = true;
  gl::LabTable table{"The profile: the share of the 10 m wind at each height",
                     {"ground", "z0 m", "I at 10 m", "at 0.5 m", "at 2 m", "at 50 m", "at 200 m"},
                     {}};
  for (const Ground& g : kGrounds) {
    gl::Trace tr{g.name, {}, {}};
    for (int step = 0; step < 95; ++step) {
      const double h = 0.2 * ge::core::libm::pow(1.08, step);
      if (h <= g.z0) continue;
      tr.x.push_back(gw::profile_at(g.z0, h));
      tr.y.push_back(h);
    }
    line.traces.push_back(tr);
    table.rows.push_back({g.name, fixed(g.z0, 4), fixed(gw::turbulence_for(g.z0), 3),
                          fixed(gw::profile_at(g.z0, 0.5), 3), fixed(gw::profile_at(g.z0, 2), 3),
                          fixed(gw::profile_at(g.z0, 50), 3), fixed(gw::profile_at(g.z0, 200), 3)});
    lab.metric(std::string("profile-2m.") + g.name, gw::profile_at(g.z0, 2), "share");
  }
  line.marks.push_back({1.0, "10 M"});
  chart_shot(
      lab, "profile", gl::line_chart(line),
      "Height (log scale) against the wind's speed as a share of its speed at 10 m, for each "
      "ground's roughness length. Every line crosses 1 at 10 m.",
      "rougher ground leans further left near the ground and further right aloft; every line "
      "meets the 10 M mark at 10 m");
  lab.table(table);
}

// One hue, light to dark, by speed.
gl::Rgba shade(double v, double top) {
  const double s = std::fmin(1.0, std::fmax(0.0, v / top));
  return {static_cast<uint8_t>(240 - 190 * s), static_cast<uint8_t>(244 - 150 * s),
          static_cast<uint8_t>(250 - 80 * s)};
}

void plan(gl::Lab& lab) {
  const double mean = lab.arg_num("mean");
  constexpr int kSize = 720;
  constexpr double kSpan = 240;  // m across
  constexpr double kPerPx = kSpan / kSize;
  gw::Field f;
  f.mean = mean;
  f.from = 3.141592653589793 * 1.25;  // from the south-west: blowing toward the north-east
  f.roughness = gw::kRoughness::grass;
  // A wood in the middle: keeps 35 % of the wind inside, 60 % for a wood's depth in its lee, and
  // turns the air round its corners.
  gw::Shelter s;
  s.x0 = 0;
  s.z0 = 0;
  s.cell = 8;
  s.nx = static_cast<int>(kSpan / s.cell) + 1;
  s.nz = s.nx;
  const auto cells = static_cast<size_t>(s.nx) * static_cast<size_t>(s.nz);
  s.keep.assign(cells, 1.0f);
  s.turn.assign(cells, 0.0f);
  for (int k = 0; k < s.nz; ++k) {
    for (int i = 0; i < s.nx; ++i) {
      const double x = i * s.cell, z = k * s.cell;
      const bool in = x >= 80 && x <= 140 && z >= 80 && z <= 140;
      const bool lee = !in && x >= 80 && x <= 200 && z >= 80 && z <= 200 && (x > 140 || z > 140);
      float keep = 1, turn = 0;
      if (in) keep = 0.35f;
      if (lee) keep = 0.6f;
      if ((in || lee) && x > 130) turn = 0.25f;
      if ((in || lee) && z > 130) turn = -0.25f;
      const size_t at = static_cast<size_t>(k) * static_cast<size_t>(s.nx) + static_cast<size_t>(i);
      s.keep[at] = keep;
      s.turn[at] = turn;
    }
  }
  gw::Source src[2];
  src[0].kind = gw::SourceKind::jet;  // a rotor's downwash, seen at 2 m, 6 m under the disc
  src[0].x = 60;
  src[0].y = 8;
  src[0].z = 190;
  src[0].radius = 5;
  src[0].strength = 10;
  src[1].kind = gw::SourceKind::whirl;
  src[1].x = 190;
  src[1].y = 0;
  src[1].z = 50;
  src[1].radius = 6;
  src[1].strength = 9;
  gl::Picture p = gl::create_picture(kSize, kSize, {255, 255, 255});
  double top = 0, wood = 0;
  int wood_n = 0;
  for (int py = 0; py < kSize; py += 4) {
    for (int px = 0; px < kSize; px += 4) {
      const double x = px * kPerPx, z = (kSize - py) * kPerPx;
      const gw::Air a = gw::wind_at(f, {x, 2, z, 2}, 30, &s, src);
      const double v = a.speed;  // the whole vector's: the downwash is mostly down
      top = std::fmax(top, v);
      if (x > 95 && x < 125 && z > 95 && z < 125) {
        wood += v;
        ++wood_n;
      }
      gl::fill_rect(p, px, py, 4, 4, shade(v, mean * 1.2));
    }
  }
  for (int py = 12; py < kSize; py += 24) {
    for (int px = 12; px < kSize; px += 24) {
      const double x = px * kPerPx, z = (kSize - py) * kPerPx;
      const gw::Air a = gw::wind_at(f, {x, 2, z, 2}, 30, &s, src);
      const double v = std::sqrt(a.x * a.x + a.z * a.z);
      if (v <= 0) continue;
      const double len = std::fmin(11.0, 2.0 * v);
      gl::draw_line(p, px, py, px + a.x / v * len, py - a.z / v * len, {20, 30, 50});
      gl::fill_disc(p, px + a.x / v * len, py - a.z / v * len, 1.5, {20, 30, 50});
    }
  }
  gl::draw_text(p, 80 / kPerPx, kSize - 142 / kPerPx, "WOOD", {20, 60, 20}, 2);
  gl::draw_text(p, 46 / kPerPx, kSize - 206 / kPerPx, "DOWNWASH", {120, 20, 20}, 1);
  gl::draw_text(p, 176 / kPerPx, kSize - 64 / kPerPx, "WHIRL", {120, 20, 20}, 1);
  lab.metric("plan.top", top, "m/s");
  lab.metric("plan.wood-mean", wood / wood_n, "m/s");
  lab.shot({"plan",
            "A SHELTERED FIELD 2 M UP, WITH A DOWNWASH AND A WHIRL",
            "240 m of open grass from above (north up), the wind from the south-west at " +
                fixed(mean, 1) +
                " m/s at 10 m, read 2 m up at t = 30 s. A wood in the middle keeps 35 % of the "
                "wind, its lee 60 %, and both turn the air round their far edges. A rotor's "
                "downwash (a jet down, 6 m over the ground) spreads at the top left; a whirl turns "
                "at the bottom right. Darker is faster (the whole speed, the downwash's downward "
                "part included); arrows point the way the air moves across the ground.",
            "the wood and its lee lighter and calm; arrows bend at the wood's far edges; the "
            "whirl turns clockwise; the downwash a dark disc",
            {{"mean", lab.arg("mean")}, {"t", "30"}, {"height", "2 m"}}},
           p);
}

void decay(gl::Lab& lab) {
  gw::Source jet;
  jet.kind = gw::SourceKind::jet;
  jet.radius = 1;
  jet.strength = 12;
  gw::Source whirl;
  whirl.kind = gw::SourceKind::whirl;
  whirl.radius = 4;
  whirl.strength = 10;
  gw::Source blast;
  blast.kind = gw::SourceKind::blast;
  blast.radius = 2;
  blast.strength = 30;
  gl::LineChart line;
  line.frame = {"THE SOURCES BY DISTANCE",
                "a 1 m jet at 12 m/s along its axis; a 4 m whirl at 10 m/s; a blast 0.1 s on",
                "distance (m)", "speed (m/s)"};
  line.spikes = false;
  gl::Trace tj{"jet centreline", {}, {}}, tw{"whirl", {}, {}}, tb{"blast", {}, {}};
  for (int step = 1; step <= 240; ++step) {
    const double d = step * 0.25;
    tj.x.push_back(d);
    tj.y.push_back(gw::source_at(jet, 0, -d, 0, 0).speed);
    tw.x.push_back(d);
    const gw::Air aw = gw::source_at(whirl, d, 50, 0, 0);
    tw.y.push_back(std::sqrt(aw.x * aw.x + aw.z * aw.z));
    tb.x.push_back(d);
    tb.y.push_back(gw::source_at(blast, d, 0, 0, 0.1).speed);
  }
  line.traces = {tj, tw, tb};
  line.marks.push_back({12.4, "JET CORE"});
  line.marks.push_back({4, "WHIRL RIM"});
  chart_shot(lab, "sources", gl::line_chart(line),
             "Each local source's speed against the distance from it: the jet holds its speed "
             "through its core (6.2 diameters) and falls as their share of the distance; the whirl "
             "rises linearly to its rim and falls as 1 / r; the blast falls as the inverse square "
             "and stops at its front (34 m at 0.1 s).",
             "the jet flat then falling; the whirl a peak at 4 m; the blast cut off at 34 m");
  lab.metric("jet-at-24.8m", tj.y[98], "m/s");
}

void run(gl::Lab& lab) {
  gusts(lab);
  profile(lab);
  plan(lab);
  decay(lab);
}

const gl::LabSpec kSpec{
    "wind",
    gl::LabKind::picture,
    "engine",
    "engine/wind/field.hpp, engine/wind/sources.hpp",
    "water",
    "Does the engine's wind gust at the asked intensity, fall off with height by its ground's log "
    "law, bend round shelter, and do its local sources decay as their closed forms say?",
    {{"mean", "8", "the mean wind at 10 m, m/s"},
     {"seed", "38", "the seed the gusts' phases are hashed off"},
     {"minutes", "10", "how long a series the gusts are measured over"}},
    "--minutes=2",
    run};
GE_LAB(kSpec);

}  // namespace
