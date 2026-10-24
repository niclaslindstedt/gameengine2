// LAB `water-colour`: the colour of water from what is in it (engine/water/optics.hpp), every
// named look on the CPU raster: from above over its own bed at six depths, the shore shelving
// from the waterline to 20 m, and from below (the veil, and white posts fading into it); each
// look's deep reflectance as a spectrum and the absorption a brown lake is made of; and each
// look's visibility against the band such water has in the field, out of it fails.
//
// ge:summary Lab: water colour per look: swatches, the shore, the view below, spectra, visibility
// ge:keywords lab water colour optics look swatch shelf shore veil visibility spectrum
// ge:keywords absorption cdom chlorophyll sediment
// ge:skill water labs
// ge:test tests/water_optics_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "water/optics.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

uint8_t encode(double linear) {
  const double x = linear <= 0 ? 0 : linear >= 1 ? 1 : linear;
  const double s = x <= 0.0031308 ? 12.92 * x : 1.055 * ge::core::libm::pow(x, 1 / 2.4) - 0.055;
  return static_cast<uint8_t>(std::lround(s * 255));
}

// A colour on the page at an exposure, plus the water's own light.
gl::Rgba shown(const gw::Rgb& c, double exposure, const gw::Rgb& glow = {}) {
  return {encode(c.r * exposure + glow.r), encode(c.g * exposure + glow.g),
          encode(c.b * exposure + glow.b)};
}

constexpr double kAbove = 4;  // the exposure the views from above are shown at
constexpr gl::Rgba kInk{24, 26, 32};
constexpr gl::Rgba kPaper{250, 250, 248};
constexpr int kName = 150;  // the names' column, px

void swatches(gl::Lab& lab) {
  constexpr double kDepths[] = {0.5, 1, 2, 5, 10, INFINITY};
  constexpr double kPosts[] = {1, 3, 10};
  constexpr int kCell = 56, kGap = 6, kRow = kCell + kGap;
  const auto looks = gw::looks();
  const int below_x = kName + 6 * (kCell + kGap) + 24;
  gl::Picture p = gl::create_picture(below_x + 4 * (kCell + kGap) + 10,
                                     40 + static_cast<int>(looks.size()) * kRow, kPaper);
  gl::draw_text(p, kName, 6, "FROM ABOVE, OVER ITS BED", kInk);
  gl::draw_text(p, below_x, 6, "FROM 2 M DOWN", kInk);
  for (int i = 0; i < 6; ++i) {
    const std::string d =
        std::isfinite(kDepths[i]) ? fixed(kDepths[i], kDepths[i] < 1 ? 1 : 0) + " M" : "DEEP";
    gl::draw_text(p, kName + i * (kCell + kGap), 20, d, kInk);
  }
  gl::draw_text(p, below_x, 20, "VEIL", kInk);
  for (int i = 0; i < 3; ++i)
    gl::draw_text(p, below_x + (i + 1) * (kCell + kGap), 20, "POST " + fixed(kPosts[i], 0), kInk);
  int y = 34;
  for (const gw::NamedLook& l : looks) {
    const gw::Iops in = gw::iops(l.water);
    const gw::Optics o = gw::optics(l.water);
    gl::draw_text(p, 6, y + 14, std::string(l.name), kInk);
    gl::draw_text(p, 6, y + 30, "SEEN " + fixed(o.visibility, o.visibility < 10 ? 2 : 0) + " M",
                  {110, 110, 120});
    for (int i = 0; i < 6; ++i) {
      const gw::Rgb c = gw::colour_of(gw::reflectance(in, l.water, kDepths[i]));
      gl::fill_rect(p, kName + i * (kCell + kGap), y, kCell, kCell, shown(c, kAbove, o.emission));
    }
    gl::fill_rect(p, below_x, y, kCell, kCell,
                  shown(gw::colour_of(gw::under_view(in, 2, INFINITY)), 1, o.emission));
    for (int i = 0; i < 3; ++i) {
      const gw::Rgb c = gw::colour_of(gw::under_view(in, 2, kPosts[i]));
      gl::fill_rect(p, below_x + (i + 1) * (kCell + kGap), y, kCell, kCell,
                    shown(c, 1, o.emission));
    }
    y += kRow;
  }
  lab.shot(
      {"swatches",
       "EVERY LOOK FROM ABOVE AND FROM BELOW",
       "Each look's water from above (exposed x4) over its own bed at six depths, and from 2 m "
       "down: the veil a view looks through, and a white post 1, 3 and 10 m away fading "
       "into it. Every colour comes from what is in the water.",
       "brown lakes amber in the shallows and black deep; clear seas blue; the tropical "
       "shallows and the pool turquoise over their pale beds; glacial water milky "
       "turquoise; the muddy river brown at every depth; posts gone into the veil where the "
       "visibility is short",
       {}},
      p);
}

void shore(gl::Lab& lab) {
  constexpr int kW = 640, kH = 30;
  const auto looks = gw::looks();
  gl::Picture p =
      gl::create_picture(kName + kW + 10, 24 + static_cast<int>(looks.size()) * (kH + 4), kPaper);
  for (int m : {0, 1, 2, 5, 10, 20}) {
    const double x = kName + kW * std::sqrt(m / 20.0);
    gl::draw_text(p, x, 6, std::to_string(m) + " M", kInk);
  }
  int y = 20;
  for (const gw::NamedLook& l : looks) {
    const gw::Iops in = gw::iops(l.water);
    const gw::Optics o = gw::optics(l.water);
    gl::draw_text(p, 6, y + 11, std::string(l.name), kInk);
    for (int x = 0; x < kW; x += 2) {
      const double s = static_cast<double>(x) / kW;
      const double depth = 20 * s * s;
      const gw::Rgb c = gw::colour_of(gw::reflectance(in, l.water, depth));
      gl::fill_rect(p, kName + x, y, 2, kH, shown(c, kAbove, o.emission));
    }
    y += kH + 4;
  }
  lab.shot({"shore",
            "THE SHORE SHELVING TO 20 M",
            "Each look seen from above as its bed shelves from the waterline to 20 m (the depth "
            "grows as the square of the distance, so the shallows are wide), exposed x4.",
            "the bed's colour at the waterline giving way to the water's own; quickly in brown "
            "and muddy water, slowly in clear water",
            {}},
           p);
}

void under(gl::Lab& lab) {
  constexpr int kW = 600, kH = 30;
  const auto looks = gw::looks();
  gl::Picture p =
      gl::create_picture(kName + kW + 10, 24 + static_cast<int>(looks.size()) * (kH + 4), kPaper);
  for (int m : {0, 5, 10, 20, 30})
    gl::draw_text(p, kName + kW * m / 30.0, 6, std::to_string(m) + " M", kInk);
  int y = 20;
  for (const gw::NamedLook& l : looks) {
    const gw::Iops in = gw::iops(l.water);
    const gw::Optics o = gw::optics(l.water);
    gl::draw_text(p, 6, y + 11, std::string(l.name), kInk);
    for (int x = 0; x < kW; x += 2) {
      const double d = 30.0 * x / kW;
      const gw::Rgb c = gw::colour_of(gw::under_view(in, 2, d));
      gl::fill_rect(p, kName + x, y, 2, kH, shown(c, 1, o.emission));
    }
    const double vx = kName + kW * o.visibility / 30;
    if (vx < kName + kW) gl::draw_line(p, vx, y - 2, vx, y + kH + 1, kInk, 2);
    y += kH + 4;
  }
  lab.shot({"under",
            "A WHITE WALL RECEDING, SEEN FROM 2 M DOWN",
            "Each look from 2 m down: a white target at every distance to 30 m, fading into the "
            "veil as e^(-c d). The black tick is the visibility, 4.8 / c, where a black target is "
            "lost.",
            "clear water holding the white for tens of metres; brown, green and muddy water "
            "swallowing it within a metre or two; each tick where its strip has faded",
            {}},
           p);
}

void spectra(gl::Lab& lab) {
  gl::LineChart line;
  line.frame = {"DEEP WATER'S REFLECTANCE BY WAVELENGTH", "pi Rrs above the surface",
                "wavelength (nm)", "reflectance"};
  line.spikes = false;
  for (const char* name : {"nordic-brown", "nordic-clear", "baltic", "adriatic", "glacial",
                           "algal-pond", "muddy-river"}) {
    const gw::Constituents& c = *gw::find_look(name);
    const gw::Spectrum r = gw::reflectance(gw::iops(c), c, INFINITY);
    gl::Trace t{name, {}, {}};
    for (int i = 0; i < gw::kBands; ++i) {
      t.x.push_back(gw::wavelength(i));
      t.y.push_back(r[i]);
    }
    line.traces.push_back(t);
  }
  const gl::Chart chart = gl::line_chart(line);
  lab.shot({"spectra",
            chart.title,
            "Each look's deep water as a spectrum: where its reflectance peaks is its colour.",
            "the clear sea peaking in the blue, the glacial in the blue-green, the plankton and "
            "the humus pushing the peak into the green and the yellow, the mud brightest and "
            "reddest",
            {}},
           gl::to_picture(chart.plot));

  gl::LineChart parts;
  parts.frame = {"WHAT A BROWN LAKE'S ABSORPTION IS MADE OF", "nordic-brown, /m", "wavelength (nm)",
                 "absorption (/m)"};
  parts.spikes = false;
  parts.log_y = true;
  const gw::Constituents& brown = *gw::find_look("nordic-brown");
  gw::Constituents only_cdom{.cdom = brown.cdom}, only_chl{.chlorophyll = brown.chlorophyll},
      only_sed{.sediment = brown.sediment};
  const gw::Iops all = gw::iops(brown), cdom = gw::iops(only_cdom), chl = gw::iops(only_chl),
                 sed = gw::iops(only_sed);
  gl::Trace tw{"pure water", {}, {}}, tg{"dissolved organic matter", {}, {}},
      tp{"phytoplankton", {}, {}}, ts{"sediment", {}, {}}, ta{"all", {}, {}};
  for (int i = 0; i < gw::kBands; ++i) {
    const double nm = gw::wavelength(i), w = gw::water_absorption(nm);
    for (gl::Trace* t : {&tw, &tg, &tp, &ts, &ta}) t->x.push_back(nm);
    tw.y.push_back(w);
    tg.y.push_back(cdom.a[i] - w);
    tp.y.push_back(chl.a[i] - w);
    ts.y.push_back(sed.a[i] - w);
    ta.y.push_back(all.a[i]);
  }
  parts.traces = {tw, tg, tp, ts, ta};
  const gl::Chart pc = gl::line_chart(parts);
  lab.shot({"absorption",
            pc.title,
            "The absorption of a brown forest lake by its parts: pure water takes the red, the "
            "dissolved organic matter the blue (falling off exponentially toward the red), the "
            "plankton its two peaks, the sediment a little everywhere.",
            "the organic matter's straight falling line on the log scale; pure water's climb past "
            "580 nm; their sum smallest in the orange: the amber",
            {}},
           gl::to_picture(pc.plot));
}

void visibility(gl::Lab& lab) {
  gl::LabTable table{
      "Each look's optics",
      {"look", "visibility m", "field band m", "c r/g/b /m", "Kd r/g/b /m", "deep colour r/g/b"},
      {}};
  for (const gw::NamedLook& l : gw::looks()) {
    const gw::Optics o = gw::optics(l.water);
    const auto rgb = [](const gw::Rgb& c, int d) {
      return fixed(c.r, d) + " " + fixed(c.g, d) + " " + fixed(c.b, d);
    };
    table.rows.push_back({std::string(l.name), fixed(o.visibility, 2),
                          fixed(l.seen[0], 2) + "-" + fixed(l.seen[1], 2), rgb(o.c, 3),
                          rgb(o.kd, 3), rgb(o.deep, 4)});
    lab.metric("visibility." + std::string(l.name), o.visibility, "m");
    if (o.visibility < l.seen[0] || o.visibility > l.seen[1]) {
      lab.fail("the look " + std::string(l.name) + " is visible to " + fixed(o.visibility, 2) +
                   " m, out of its field band " + fixed(l.seen[0], 2) + "-" + fixed(l.seen[1], 2) +
                   " m",
               "Retune its constituents in engine/water/optics.cpp (skill: water).");
    }
  }
  lab.table(table);
}

void run(gl::Lab& lab) {
  swatches(lab);
  shore(lab);
  under(lab);
  spectra(lab);
  visibility(lab);
}

const gl::LabSpec kSpec{
    "water-colour",
    gl::LabKind::picture,
    "engine",
    "engine/water/optics.hpp",
    "water",
    "Does every look's water have the colour what is in it gives, from above over its bed and "
    "from below, and the visibility such water has in the field?",
    {},
    "",
    run};
GE_LAB(kSpec);

}  // namespace
