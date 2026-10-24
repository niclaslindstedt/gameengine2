// LAB `snow-contact`: the snow contact laws (engine/snow/contact.hpp) charted for every named
// snow, so a change to a row or a law shows as a curve that moved. Four charts: the sink a
// pressure reaches under a plate (Bekker, no base in reach); a track's traction against its slip
// (Mohr–Coulomb and Janosi–Hanamoto); a base's glide against the temperature; and a snowmobile
// track's resistance against its speed (compaction and bulldozing over the sink planing leaves).
// The table gives each kind's numbers under a boot, a tyre and a track; the lab fails a kind whose
// plate sink is off its fitted value by more than 5 %.
//
// ge:summary Lab: the snow contact laws charted for every kind: sink, traction, glide, drag
// ge:keywords lab snow contact bekker sinkage traction slip janosi glide friction temperature
// ge:keywords resistance bulldozing planing chart kinds
// ge:skill snow labs
// ge:test tests/snow_contact_test.cpp
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "snow/contact.hpp"
#include "snow/kinds.hpp"

namespace {

namespace gl = ge::lab;
namespace sn = ge::snow;

constexpr int kKinds = static_cast<int>(sn::Kind::count);
// The charts draw the seven snows (a chart has seven colours); ice is in the table.
constexpr int kCharted = static_cast<int>(sn::Kind::ice);
// Each kind's fitted sink under a 10 kPa plate 10 cm wide, m (contact.hpp's row comments).
constexpr double kSink10[kKinds] = {0.58, 0.137, 0.02, 0.01, 0.04, 0.01, 0.012, 0.0005};

std::string fix(double v, int digits) {
  char b[48];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

sn::Kind kind_of(int k) { return static_cast<sn::Kind>(k); }
std::string name_of(int k) { return std::string(sn::kind_row(kind_of(k)).name); }

void chart(gl::Lab& lab, const std::string& id, gl::LineChart c, const std::string& caption,
           const std::string& look_for) {
  c.spikes = false;
  const gl::Chart drawn = gl::line_chart(c);
  const std::string svg = gl::to_svg(drawn.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id, c.frame.title, caption, look_for, {{"width", lab.arg("width")}}},
           gl::to_picture(drawn.plot));
}

void run(gl::Lab& lab) {
  const double b = lab.arg_num("width");
  const int points = static_cast<int>(lab.arg_num("points"));

  // SINK AGAINST PRESSURE, 1 to 100 kPa on a log sweep.
  gl::LineChart sink;
  sink.frame = {"Sink under a plate against its pressure, every kind",
                "Bekker's law, no base in reach, plate " + fix(b * 100, 0) + " cm wide",
                "pressure (kPa)", "sink (cm)"};
  sink.log_y = true;
  for (int k = 0; k < kCharted; k++) {
    gl::Trace t{name_of(k), {}, {}};
    for (int i = 0; i <= points; i++) {
      const double kpa = ge::core::libm::pow(10, 2.0 * i / points);
      t.x.push_back(kpa);
      t.y.push_back(100 * sn::bekker_sink(sn::contact_row(kind_of(k)), b, kpa * 1000));
    }
    sink.traces.push_back(t);
  }
  sink.rules = {50};
  chart(lab, "sink", sink,
        "Bekker's sink for every named snow under a plate the --width wide, 1 to 100 kPa (a "
        "skier stands on 3-4, a boot 30, a tyre 60). The dashed line is 50 cm: deeper than that a "
        "real column has bottomed out, which column_sink's cap applies and this chart does not.",
        "Powder far deepest, then settled, spring and wind-packed; machine, groomed and crust "
        "close together near a centimetre at 10 kPa; every curve rising, none crossing");

  // TRACTION AGAINST SLIP: a 3 m track 0.4 m wide at 10 kPa.
  gl::LineChart grip;
  grip.frame = {"A track's traction against its slip, every kind",
                "Mohr-Coulomb strength, Janosi-Hanamoto slip; 3 m x 0.4 m at 10 kPa", "slip ratio",
                "traction (kN)"};
  for (int k = 0; k < kCharted; k++) {
    gl::Trace t{name_of(k), {}, {}};
    for (int i = 0; i <= points; i++) {
      const double slip = static_cast<double>(i) / points;
      t.x.push_back(slip);
      t.y.push_back(sn::tread_traction(sn::contact_row(kind_of(k)), 1.2, 1e4, slip, 3) / 1000);
    }
    grip.traces.push_back(t);
  }
  chart(lab, "traction", grip,
        "A snowmobile-sized track (3 m long, 0.4 m wide, 10 kPa) on every kind, its traction as "
        "the slip grows from none to spinning in place.",
        "Every curve rising steeply then flattening towards area x tau_max; powder weakest, the "
        "machine snow and the crust strongest (ice, friction alone, is weaker still: the table)");

  // GLIDE AGAINST TEMPERATURE.
  gl::LineChart glide;
  glide.frame = {"A waxed base's friction against the temperature, every kind", "", "air (C)",
                 "friction coefficient"};
  for (int k = 0; k < kCharted; k++) {
    gl::Trace t{name_of(k), {}, {}};
    for (int i = 0; i <= points; i++) {
      const double temp = -30 + 30.0 * i / points;
      t.x.push_back(temp);
      t.y.push_back(sn::glide_friction(kind_of(k), temp));
    }
    glide.traces.push_back(t);
  }
  glide.marks.push_back({-5, "FASTEST"});
  chart(lab, "glide", glide,
        "The friction of a waxed base on every kind from -30 to 0 C: dry grains in deep cold, "
        "water at the melt.",
        "Every curve lowest at -5 C, rising gently into the cold and steeply to the melt; powder "
        "the slowest snow, groomed the fastest");

  // RESISTANCE AGAINST SPEED: a track 0.4 m wide at 3.2 kPa in 60 cm of each kind, planing at 12.
  gl::LineChart drag;
  drag.frame = {"A snowmobile track's snow resistance against its speed, every kind",
                "compaction + bulldozing over the sink planing leaves; 0.4 m wide, 3.2 kPa, "
                "60 cm deep, planing at 12 m/s",
                "speed (m/s)", "resistance (N)"};
  for (int k = 0; k < kCharted; k++) {
    const sn::Kind kind = kind_of(k);
    const sn::ContactRow& row = sn::contact_row(kind);
    const double rho = sn::kind_row(kind).loose;
    const double rest = sn::column_sink(kind, 0.6, rho, 0.4, 3200);
    gl::Trace t{name_of(k), {}, {}};
    for (int i = 0; i <= points; i++) {
      const double v = 25.0 * i / points;
      const double z = sn::planing_sink(rest, v, 12);
      t.x.push_back(v);
      t.y.push_back(sn::compaction_resistance(row, 0.4, z) +
                    sn::bulldozing_resistance(row, rho, 0.4, z, v));
    }
    drag.traces.push_back(t);
  }
  chart(lab, "drag", drag,
        "What the snow holds a track back with as it speeds up: deep and slow it ploughs, fast it "
        "rides up and the sink, and the drag with it, fall away.",
        "Powder's curve far above the rest and settled's below it, both peaking near 11 m/s and "
        "falling past the planing speed; crust low; the packed kinds at zero (their pressure "
        "packs them no further)");

  // THE TABLE AND THE CHECK.
  gl::LabTable table{"every kind",
                     {"kind", "plate sink cm", "boot sink cm", "tyre sink cm", "tau_max kPa",
                      "track kN @20%", "glide -5C", "tyre mu"},
                     {}};
  for (int k = 0; k < kKinds; k++) {
    const sn::Kind kind = kind_of(k);
    const sn::ContactRow& row = sn::contact_row(kind);
    const double rho = sn::kind_row(kind).loose;
    const double plate = sn::bekker_sink(row, 0.1, 1e4);
    const double boot = sn::column_sink(kind, 0.5, rho, 0.11, 3.03e4);
    const double tyre = sn::column_sink(kind, 0.5, rho, 0.2, 6.13e4);
    const double track = sn::tread_traction(row, 1.2, 1e4, 0.2, 3);
    table.rows.push_back({name_of(k), fix(plate * 100, 2), fix(boot * 100, 1), fix(tyre * 100, 1),
                          fix(sn::shear_strength(row, 1e4) / 1000, 2), fix(track / 1000, 2),
                          fix(sn::glide_friction(kind, -5), 3),
                          fix(sn::tyre_friction(kind, -5), 2)});
    lab.metric(name_of(k) + ".plate-sink", plate, "m");
    lab.metric(name_of(k) + ".boot-sink", boot, "m");
    lab.metric(name_of(k) + ".track-traction", track, "N");
    if (std::fabs(plate - kSink10[k]) > 0.05 * kSink10[k]) {
      lab.fail(name_of(k) + "'s plate sink " + fix(plate, 4) +
                   " m is more than 5 % off its fitted " + fix(kSink10[k], 4) + " m",
               "Read the kind's row in engine/snow/contact.hpp (its comment states the fit), then "
               "rerun `make lab LAB=snow-contact` (skill: snow).");
    }
  }
  lab.table(table);
}

const gl::LabSpec kSpec{
    "snow-contact",
    gl::LabKind::scenario,
    "engine",
    "engine/snow/contact.hpp",
    "snow",
    "What do the snow contact laws give for every named snow: sink, traction, glide and drag?",
    {
        {"width", "0.1", "the plate's width in the sink chart, m"},
        {"points", "60", "points along each curve"},
    },
    "--points=12",
    run,
};
GE_LAB(kSpec);

}  // namespace
