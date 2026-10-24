// LAB `snow-press`: what each thing that crosses snow leaves in it (engine/snow/press.hpp). On
// each snow of --snows (a layer of flat cover: fresh powder, settled snow, a groomed piste), every
// reference contact (engine/snow/reference.hpp: a carving ski, a skidding pair, a boot, a tyre, a
// snowmobile's tread, a body) lays a short track along +z, and the lab draws, for each:
//
// - THE CROSS-SECTION across the track's middle: the ground, the untouched
//   cover's surface as a dashed line (the vertical drawn 1.3 times the across), and the snow as it
//   stands now, each column shaded by its density (white loose, deep blue packed), so a groove's
//   packed floor, its walls and the berm beside it are read straight off it;
// - THE PLAN of the track from above, the surface's drop shaded (blue under the cover, amber
//   over it: the berm), the contact's true outline at its last press drawn on it.
//
// Numbers per contact and snow: the pressure, the sink on the axis, the density there, the berm's
// height, the kilograms thrown and compacted; and the check that the layer holds every unit of
// mass it started with (a failure says which contact made or lost snow).
//
// ge:summary Lab: every reference contact's imprint on three snows, cross-sections and plans
// ge:keywords lab snow press imprint cross-section plan groove rut trench footprint berm density
// ge:keywords mass-kept ski boot tyre tread body
// ge:skill snow labs
// ge:test tests/snow_press_test.cpp
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "snow/layer.hpp"
#include "snow/press.hpp"
#include "snow/reference.hpp"

namespace {

namespace sn = ge::snow;
namespace gl = ge::lab;

struct Snow {
  const char* name;
  double depth, packed;
  sn::Kind kind;
};

constexpr Snow kSnows[] = {
    {"powder", 0.6, 0, sn::Kind::powder},
    {"settled", 0.35, 0.5, sn::Kind::settled},
    {"groomed", 0.25, 1, sn::Kind::groomed},
};

constexpr int kPresses = 10;
constexpr double kSpan = 2.4;  // m across a panel
constexpr int kPanel = 300;    // px a panel side
constexpr int kSection = 170;  // px of cross-section
constexpr double kUp = 165;    // px a metre up in the cross-section (1.3 times the across)

std::string fmt(double v, int places) {
  char b[48];
  std::snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}

// White loose snow to deep blue packed, by density.
gl::Rgba density_colour(double rho) {
  const double t = ge::core::clamp((rho - 50) / 550, 0, 1);
  auto mix = [&](double a, double b) { return static_cast<uint8_t>(a + (b - a) * t); };
  return {mix(250, 40), mix(252, 90), mix(255, 170), 255};
}

// The drop's shade: blue under the cover, amber over it, white level with it.
gl::Rgba drop_colour(double drop) {
  if (drop >= 0) {
    const double t = ge::core::clamp(drop / 0.4, 0, 1);
    return {static_cast<uint8_t>(245 - 200 * t), static_cast<uint8_t>(248 - 150 * t),
            static_cast<uint8_t>(252 - 60 * t), 255};
  }
  const double t = ge::core::clamp(-drop / 0.15, 0, 1);
  return {static_cast<uint8_t>(250 - 10 * t), static_cast<uint8_t>(248 - 90 * t),
          static_cast<uint8_t>(245 - 200 * t), 255};
}

void outline(gl::Picture& p, const sn::Shape& s, double cx, double cz, double px_per_m, int left,
             int top, gl::Rgba c) {
  // The outline's edge: the points where shape_u crosses 1, traced on a fine grid.
  for (int j = 0; j < kPanel; j++) {
    for (int i = 0; i < kPanel; i++) {
      const double x = cx + (i - kPanel / 2.0) / px_per_m, z = cz + (kPanel / 2.0 - j) / px_per_m;
      const double u = sn::shape_u(s, x, z);
      const double ur = sn::shape_u(s, x + 1 / px_per_m, z);
      const double ud = sn::shape_u(s, x, z - 1 / px_per_m);
      if ((u <= 1) != (ur <= 1) || (u <= 1) != (ud <= 1)) gl::set_pixel(p, left + i, top + j, c);
    }
  }
}

void run(gl::Lab& lab) {
  const std::vector<double> which = lab.arg_nums("snows");
  gl::LabTable table{"EVERY CONTACT ON EVERY SNOW",
                     {"snow", "contact", "pressure kPa", "sink cm", "density kg/m3", "berm cm",
                      "thrown kg", "compacted kg", "samples"},
                     {}};
  for (const double wi : which) {
    const int si = static_cast<int>(wi);
    if (si < 0 || si > 2) continue;
    const Snow& snow = kSnows[si];
    const int cols = 3, rows = 2;
    gl::Picture sheet =
        gl::create_picture(cols * kPanel, rows * (kSection + kPanel + 14), {255, 255, 255, 255});
    for (int k = 0; k < sn::kReferenceCount; k++) {
      sn::LayerSpec spec;
      spec.region = {-3, -6, 3, 6};
      spec.cover.depth = snow.depth;
      spec.cover.packed = snow.packed;
      spec.cover.kind = snow.kind;
      sn::SnowWorld world;
      sn::Layer& layer = sn::add_layer(world, spec);
      const sn::ReferenceContact rc = sn::reference_contact(k);
      double pressure = 0, thrown = 0, compacted = 0;
      int samples = 0;
      sn::Imprint last;
      for (int i = 0; i < kPresses; i++) {
        // A walker's steps fall left and right of his line.
        const double x = rc.imprint.motion == sn::Motion::step ? (i % 2 ? 0.12 : -0.12) : 0;
        last = sn::place(rc.imprint, x, -0.5 * kPresses * rc.step + rc.step * i, 0);
        const sn::Pressed p = sn::press(layer, last, i / 120.0);
        pressure = p.pressure;
        thrown += p.thrown;
        compacted += p.compacted;
        samples += p.samples;
      }
      const std::string id = std::string(snow.name) + "." + std::string(rc.name);
      if (sn::mass_units(layer) + layer.spilled != layer.mass_in) {
        lab.fail(id + " made or lost snow: the tiles hold " +
                     std::to_string(sn::mass_units(layer)) + " units and spilled " +
                     std::to_string(layer.spilled) + " of " + std::to_string(layer.mass_in),
                 "press must keep the mass to the unit: run tests/snow_press_test.cpp and read "
                 "press.cpp's berm. Skill: snow.");
      }
      // The middle of the track, where the cross-section is cut.
      const double mid_z = 0;
      const sn::Column axis = sn::sample(layer, 0, mid_z);
      double berm = 0;
      for (int n = 0; -kSpan / 2 + n * 0.01 <= kSpan / 2; n++) {
        const double x = -kSpan / 2 + n * 0.01;
        const double d = -sn::sample(layer, x, mid_z).drop;
        berm = d > berm ? d : berm;
      }
      lab.metric(id + ".sink", axis.drop, "m");
      lab.metric(id + ".density", axis.density, "kg/m3");
      lab.metric(id + ".berm", berm, "m");
      lab.metric(id + ".thrown", thrown, "kg");
      table.rows.push_back({snow.name, std::string(rc.name), fmt(pressure / 1000, 1),
                            fmt(axis.drop * 100, 1), fmt(axis.density, 0), fmt(berm * 100, 1),
                            fmt(thrown, 2), fmt(compacted, 2), std::to_string(samples)});

      // The panel: the cross-section over the plan.
      const int ox = (k % cols) * kPanel, oy = (k / cols) * (kSection + kPanel + 14);
      const double ppm = kPanel / kSpan;
      const double base_y = oy + kSection - 12;  // the ground's line
      const double vs = kUp;
      gl::fill_rect(sheet, ox, oy, kPanel, kSection, {236, 240, 244, 255});
      for (int i = 0; i < kPanel; i++) {
        const double x = (i - kPanel / 2.0) / ppm;
        const sn::Column c = sn::sample(layer, x, mid_z);
        const double top = base_y - c.depth * vs;
        const gl::Rgba col = density_colour(c.density);
        gl::fill_rect(sheet, ox + i, top, 1, base_y - top, col);
        const double cover_top = base_y - (c.depth + c.drop) * vs;
        if (i % 6 < 3)
          gl::set_pixel(sheet, ox + i, static_cast<int>(cover_top), {200, 60, 40, 255});
      }
      gl::fill_rect(sheet, ox, base_y, kPanel, 12, {122, 106, 88, 255});
      gl::draw_text(sheet, ox + 6, oy + 6, std::string(rc.name), {24, 35, 46, 255}, 2);
      gl::draw_text(sheet, ox + 6, oy + 26,
                    "sink " + fmt(axis.drop * 100, 1) + " cm  rho " + fmt(axis.density, 0),
                    {70, 86, 100, 255});
      const int plan_y = oy + kSection;
      for (int j = 0; j < kPanel; j++) {
        for (int i = 0; i < kPanel; i++) {
          const double x = (i - kPanel / 2.0) / ppm, z = (kPanel / 2.0 - j) / ppm;
          gl::set_pixel(sheet, ox + i, plan_y + j, drop_colour(sn::sample(layer, x, z).drop));
        }
      }
      outline(sheet, last.shape, 0, 0, ppm, ox, plan_y, {200, 60, 40, 255});
      gl::draw_line(sheet, ox, plan_y + kPanel / 2.0, ox + kPanel, plan_y + kPanel / 2.0,
                    {120, 130, 140, 120});
      gl::draw_line(sheet, ox + kPanel - 1, oy, ox + kPanel - 1, oy + kSection + kPanel + 14,
                    {200, 208, 216, 255});
    }
    lab.shot({std::string("contacts-") + snow.name,
              std::string("EVERY CONTACT ON ") + (si == 0   ? "FRESH POWDER"
                                                  : si == 1 ? "SETTLED SNOW"
                                                            : "A GROOMED PISTE"),
              "Each reference contact pressed " + std::to_string(kPresses) + " times along +z on " +
                  fmt(snow.depth * 100, 0) + " cm of " + snow.name +
                  ". Top: the cross-section at the track's middle, the vertical 1.3 times the "
                  "across, columns "
                  "shaded by density (white loose, blue packed), the untouched surface dashed "
                  "red. Bottom: the plan, 2.4 m square, blue where the surface dropped, amber "
                  "where a berm stands over it, the last press's true outline in red.",
              "grooves with packed floors under each contact; berms beside the skid, the "
              "plough-like body and the tread; a boot's posthole as deep as its pressure packs; "
              "nothing pressed where the snow is already denser than the contact can pack it",
              {{"snow", snow.name}, {"depth", fmt(snow.depth, 2)}, {"cell", "0.1"}}},
             sheet);
  }
  lab.table(table);
}

const gl::LabSpec kSpec{
    "snow-press",
    gl::LabKind::scenario,
    "engine",
    "engine/snow/press.hpp",
    "snow",
    "What does each thing that crosses snow (a ski, a boot, a tyre, a tread, a body) leave in it, "
    "on fresh, settled and groomed snow, with the mass kept?",
    {{"snows", "0,1,2", "which snows: 0 powder, 1 settled, 2 groomed, comma-separated"}},
    "--snows=0",
    run,
};
GE_LAB(kSpec);

}  // namespace
