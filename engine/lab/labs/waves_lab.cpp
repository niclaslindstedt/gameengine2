// LAB `waves`: the sea a wind grows (engine/water/spectrum.hpp) and the waves the simulation reads
// (engine/water/waves.hpp), read the way their readers read them: the growth laws' height and
// period against the fetch for four winds; the spectrum each of three seas carries and the
// components laid off it (where they sit in frequency and how they fan round the wind); a long
// series at one point measured against the laid height and mean period; and each sea's surface
// from above, lit off its own slopes, beside a profile along the wind.
//
// ge:summary Lab: the waves: growth laws, the spectrum and its components, a measured series, the
// surface ge:keywords lab water waves sea growth fetch jonswap spectrum components series surface
// ge:keywords significant-height period hillshade profile
// ge:skill water labs
// ge:test tests/water_waves_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "water/spectrum.hpp"
#include "water/waves.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2 * kPi;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id,
            chart.title,
            caption,
            look_for,
            {{"seed", lab.arg("seed")}, {"minutes", lab.arg("minutes")}}},
           gl::to_picture(chart.plot));
}

// The three seas every section reads: a lake's chop, a coast's sea, a swell over a shelf.
struct Case {
  const char* name;
  double wind;   // m/s at 10 m
  double fetch;  // m
  double depth;  // m
};
constexpr Case kCases[] = {
    {"lake chop", 9, 3000, 30}, {"open sea", 14, 2e5, 1e9}, {"shelf swell", 16, 1e9, 25}};

gw::Waves laid(const Case& c, uint32_t seed) {
  gw::Sea sea = gw::grow(c.wind, c.fetch, 0);  // running toward +z
  sea.depth = c.depth;
  gw::Lay how;
  how.seed = seed;
  return gw::lay(sea, how);
}

void growth(gl::Lab& lab) {
  gl::LineChart hs;
  hs.frame = {"THE SEA A WIND GROWS: HEIGHT BY FETCH",
              "fetch-limited growth, capped where the sea is fully developed",
              "log10 of the fetch (m)", "Hs (m)"};
  hs.spikes = false;
  hs.log_y = true;
  gl::LineChart tp = hs;
  tp.frame.title = "THE SEA A WIND GROWS: PERIOD BY FETCH";
  tp.frame.y_label = "Tp (s)";
  gl::LabTable table{"Growth: Hs (m) and Tp (s) by wind and fetch",
                     {"wind m/s", "1 km", "10 km", "100 km", "developed", "developed at km"},
                     {}};
  for (const double wind : {5.0, 10.0, 15.0, 20.0}) {
    gl::Trace th{fixed(wind, 0) + " m/s", {}, {}}, tt = th;
    for (int step = 0; step <= 120; ++step) {
      const double lf = 1 + step * 0.05;  // 10 m .. 10 000 km
      const gw::Sea s = gw::grow(wind, ge::core::libm::pow(10, lf), 0);
      th.x.push_back(lf);
      th.y.push_back(s.hs);
      tt.x.push_back(lf);
      tt.y.push_back(s.tp);
    }
    hs.traces.push_back(th);
    tp.traces.push_back(tt);
    const auto cell = [&](double f) {
      const gw::Sea s = gw::grow(wind, f, 0);
      return fixed(s.hs, 2) + " / " + fixed(s.tp, 1);
    };
    table.rows.push_back({fixed(wind, 0), cell(1e3), cell(1e4), cell(1e5), cell(1e9),
                          fixed(gw::developed_fetch() * wind * wind / gw::kGravity / 1000, 0)});
    lab.metric("developed-hs." + fixed(wind, 0), gw::grow(wind, 1e9, 0).hs, "m");
  }
  chart_shot(lab, "growth-height", gl::line_chart(hs),
             "The significant height a wind grows against the fetch it has blown over (log "
             "scales), for four winds at 10 m. Each line rises as the root of the fetch until the "
             "sea is fully developed, then stays flat.",
             "straight rising lines that turn flat, the stronger winds' later and higher; a pond "
             "(10 m) grows millimetres");
  chart_shot(lab, "growth-period", gl::line_chart(tp),
             "The peak period against the fetch for the same winds: rising as the cube root of "
             "the fetch, flat once developed.",
             "the same shape as the heights, flatter; the developed periods about 0.83 s per m/s");
  lab.table(table);
}

void spectrum(gl::Lab& lab) {
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));
  gl::LineChart line;
  line.frame = {"THE SPECTRUM EACH SEA CARRIES", "JONSWAP, normalised to the sea's own height",
                "w / wp", "S(w) / S(wp)"};
  line.spikes = false;
  gl::LineChart fan;
  fan.frame = {"THE COMPONENTS LAID",
               "24 a sea, slices of equal weight (energy and slope), directions off the spread",
               "w / wp", "direction off the wind (deg)"};
  fan.points = true;
  fan.spikes = false;
  gl::LabTable table{"The seas laid: what the band keeps of the sea",
                     {"sea", "wind m/s", "fetch km", "depth m", "Hs asked m", "Hs laid m", "Tp s",
                      "band w/wp", "steepest ka"},
                     {}};
  for (const Case& c : kCases) {
    const gw::Waves w = laid(c, seed);
    const double wp = kTau / w.sea.tp;
    const double norm = gw::normaliser(w.sea);
    const double peak = gw::jonswap_at(w.sea, wp, norm);
    gl::Trace tr{c.name, {}, {}};
    for (int step = 0; step <= 300; ++step) {
      const double q = 0.3 + step * 0.01;
      tr.x.push_back(q);
      tr.y.push_back(gw::jonswap_at(w.sea, q * wp, norm) / peak);
    }
    line.traces.push_back(tr);
    gl::Trace pts{c.name, {}, {}};
    double steepest = 0;
    for (int i = 0; i < w.count; ++i) {
      const gw::Component& k = w.c[i];
      double off = ge::core::libm::atan2(k.kx, k.kz) - w.sea.toward;
      while (off > kPi) off -= kTau;
      while (off < -kPi) off += kTau;
      pts.x.push_back(k.w / wp);
      pts.y.push_back(off * 180 / kPi);
      steepest = std::fmax(steepest, k.k * k.a);
    }
    fan.traces.push_back(pts);
    const double hs_laid = 4 * std::sqrt(w.m0);
    table.rows.push_back({c.name, fixed(c.wind, 0), fixed(c.fetch / 1000, 0),
                          c.depth > 1e6 ? "deep" : fixed(c.depth, 0), fixed(w.sea.hs, 2),
                          fixed(hs_laid, 2), fixed(w.sea.tp, 2),
                          fixed(w.w_lo / wp, 2) + " .. " + fixed(w.w_hi / wp, 2),
                          fixed(steepest, 3)});
    lab.metric(std::string("hs-laid-share.") + c.name, hs_laid / w.sea.hs, "share");
    if (hs_laid < 0.85 * w.sea.hs) {
      lab.fail(std::string("the ") + c.name + " band keeps " + fixed(hs_laid, 2) + " m of " +
                   fixed(w.sea.hs, 2) + " m",
               "Read lay in engine/water/waves.cpp (the band's ends, the steepness cap), then "
               "rerun `make lab LAB=waves` (skill: water).");
    }
  }
  line.marks.push_back({1.0, "PEAK"});
  chart_shot(lab, "spectrum", gl::line_chart(line),
             "Each sea's energy against frequency (as a share of its peak frequency), scaled to "
             "its peak. The shelf's swell is cut below the peak by the depth.",
             "one sharp peak at 1, a steep face below it and a long tail above; the shelf swell "
             "lower and wider below the peak");
  chart_shot(
      lab, "components", gl::line_chart(fan),
      "Every component laid: its frequency against its direction off the wind. Slices of "
      "equal weight (half energy, half slope) crowd onto the peak and still reach the tail; the "
      "fan is narrowest at the peak and widens above "
      "and below it.",
      "dots dense near 1 and sparse in the tail; the spread narrow at 1 and wide at the ends; "
      "no two neighbours pointing the same way");
  lab.table(table);
}

void series(gl::Lab& lab) {
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));
  const int steps = static_cast<int>(lab.arg_num("minutes") * 60 * 20);
  gl::LineChart line;
  line.frame = {"THE SURFACE AT ONE POINT, TWO MINUTES", "the height over the still level", "t (s)",
                "height (m)"};
  line.spikes = false;
  gl::LabTable table{"A long series at one point against the band laid",
                     {"sea", "Hs laid m", "Hs measured m", "Tz laid s", "Tz measured s",
                      "highest crest m", "deepest trough m"},
                     {}};
  for (const Case& c : kCases) {
    const gw::Waves w = laid(c, seed);
    gl::Trace tr{c.name, {}, {}};
    double sum = 0, sum2 = 0, hi = -1e9, lo = 1e9, last = 0;
    int ups = 0;
    for (int i = 0; i < steps; ++i) {
      const double t = i * 0.05;
      const double h = gw::surface_at(w, 0, 0, t).height;
      sum += h;
      sum2 += h * h;
      hi = std::fmax(hi, h);
      lo = std::fmin(lo, h);
      if (i > 0 && last < 0 && h >= 0) ++ups;
      last = h;
      if (t <= 120 && i % 2 == 0) {
        tr.x.push_back(t);
        tr.y.push_back(h);
      }
    }
    line.traces.push_back(tr);
    const double mean = sum / steps;
    const double hs = 4 * std::sqrt(sum2 / steps - mean * mean);
    const double hs_laid = 4 * std::sqrt(w.m0);
    const double tz_laid = kTau * std::sqrt(w.m0 / w.m2);
    const double tz = ups > 0 ? steps * 0.05 / ups : 0;
    table.rows.push_back({c.name, fixed(hs_laid, 3), fixed(hs, 3), fixed(tz_laid, 2), fixed(tz, 2),
                          fixed(hi, 2), fixed(lo, 2)});
    lab.metric(std::string("hs-measured.") + c.name, hs, "m");
    lab.metric(std::string("tz-measured.") + c.name, tz, "s");
    if (steps >= 6000 && std::fabs(hs / hs_laid - 1) > 0.08) {
      lab.fail(std::string("the ") + c.name + " series measures Hs " + fixed(hs, 3) +
                   " m against " + fixed(hs_laid, 3) + " m laid",
               "Read surface_at in engine/water/waves.cpp, then rerun `make lab LAB=waves` "
               "(skill: water).");
    }
  }
  chart_shot(lab, "series", gl::line_chart(line),
             "The surface's height at one point over two minutes for the three seas. Crests are "
             "sharper and higher than the troughs are deep (the second-order crest), and the "
             "groups come and go as the components beat.",
             "irregular groups, never a single sine; crests pointed, troughs flat; the swell "
             "slowest and the chop fastest");
  lab.table(table);
}

// A sea from above, lit by a low sun in the south off its slopes: blue water, light faces toward
// the sun. Components shorter than four pixels are left out, as the picture's own tier would leave
// them to a finer view: drawn, they would alias into stripes that are not there.
gl::Picture plan(const gw::Waves& all, double span, int size, double t) {
  gl::Picture p = gl::create_picture(size, size, {255, 255, 255});
  const double per = span / size;
  gw::Waves w = all;
  w.count = 0;
  for (int i = 0; i < all.count; ++i)
    if (kTau / all.c[i].k >= 4 * per) w.c[w.count++] = all.c[i];
  const double sx = 0.15, sy = 0.45, sz = -0.88;  // toward the sun
  for (int py = 0; py < size; ++py) {
    for (int px = 0; px < size; ++px) {
      const double x = px * per, z = (size - py) * per;
      const gw::Surface s = gw::surface_at(w, x, z, t);
      const double n = std::sqrt(s.dx * s.dx + 1 + s.dz * s.dz);
      const double lit = std::fmax(0.0, (-s.dx * sx + sy - s.dz * sz) / n);
      const double l = std::fmin(1.0, std::fmax(0.0, 0.5 + 1.6 * (lit - sy)));
      gl::set_pixel(p, px, py,
                    {static_cast<uint8_t>(20 + 120 * l), static_cast<uint8_t>(60 + 140 * l),
                     static_cast<uint8_t>(90 + 160 * l)});
    }
  }
  return p;
}

void surfaces(gl::Lab& lab) {
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));
  constexpr int kTile = 300;
  gl::Picture sheet = gl::create_picture(kTile * 3 + 40, kTile + 60, {255, 255, 255});
  gl::LineChart profile;
  profile.frame = {"EACH SEA ALONG THE WIND",
                   "the surface along +z through the tile's middle, t = 30 s", "z (m)",
                   "height (m)"};
  profile.spikes = false;
  int col = 0;
  for (const Case& c : kCases) {
    const gw::Waves w = laid(c, seed);
    const double span = std::fmax(12.0, 6 * gw::kGravity * w.sea.tp * w.sea.tp / kTau);
    const gl::Picture tile = plan(w, span, kTile, 30);
    gl::blit(sheet, tile, 10 + col * (kTile + 10), 40);
    gl::draw_text(sheet, 10 + col * (kTile + 10), 8, c.name, {20, 30, 50}, 2);
    gl::draw_text(sheet, 10 + col * (kTile + 10), 26, fixed(span, 0) + " M ACROSS", {90, 90, 90},
                  1);
    gl::Trace tr{c.name, {}, {}};
    for (int i = 0; i <= 400; ++i) {
      const double z = i * span / 400;
      tr.x.push_back(z / span);
      tr.y.push_back(gw::surface_at(w, span / 2, z, 30).height);
    }
    profile.traces.push_back(tr);
    ++col;
  }
  lab.shot(
      {"surfaces",
       "THE THREE SEAS FROM ABOVE",
       "Each sea at t = 30 s from above (north up, the wind toward the north), six peak "
       "wavelengths across, lit by a low sun in the south off the surface's own slopes (contrast "
       "stretched). Light faces lean toward the sun. Components shorter than four pixels are "
       "left out, as the picture's tier would at that distance.",
       "short-crested bands roughly across the wind, never one clean stripe; the swell the "
       "longest; no tiling or repeating pattern",
       {{"seed", lab.arg("seed")}, {"t", "30"}}},
      sheet);
  profile.frame.x_label = "share of the tile along the wind";
  chart_shot(lab, "profile", gl::line_chart(profile),
             "The surface along the wind through each tile's middle: pointed crests over flat "
             "troughs.",
             "crests sharper than troughs; the chop small and quick, the swell tall and long");
}

void run(gl::Lab& lab) {
  growth(lab);
  spectrum(lab);
  series(lab);
  surfaces(lab);
}

const gl::LabSpec kSpec{
    "waves",
    gl::LabKind::picture,
    "engine",
    "engine/water/spectrum.hpp, engine/water/waves.hpp",
    "water",
    "Does a wind over a fetch grow the textbook sea, does the band laid carry its height, and do "
    "the surface's crests, groups and fans look like water?",
    {{"seed", "38", "the seed the components' phases and directions are drawn off"},
     {"minutes", "30", "how long a series each sea is measured over"}},
    "--minutes=3",
    run};
GE_LAB(kSpec);

}  // namespace
