// LAB `liquids`: the named liquids (engine/water/liquids.hpp) read the way a game reads them:
// the share of each wavelength's wind input each keeps under a gale against its viscous loss
// (water/liquid.hpp's wind_keep), the surface drift the wind drags; the swimmer's scales each
// derives (swim_for), and how far a stroke gets him; what each does to a body (water/harm.hpp)
// all in, waist deep and feet first, and the radiant heat over a lava pool by height.
//
// ge:summary Lab: the liquids: the wind's ripples kept and the drift, the swim, the harm, the heat
// ge:keywords lab liquids lava slime viscosity yield wind_keep drift swim_for stroke harm dose
// ge:keywords radiant-flux burn
// ge:skill water labs
// ge:test tests/water_liquids_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "water/harm.hpp"
#include "water/liquid.hpp"
#include "water/liquids.hpp"

namespace {

namespace gl = ge::lab;
namespace gw = ge::water;

constexpr double kDt = 1.0 / 120;
constexpr double kG = 9.80665;

std::string fixed(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}

void chart_shot(gl::Lab& lab, const std::string& id, const gl::Chart& chart,
                const std::string& caption, const std::string& look_for) {
  const std::string svg = gl::to_svg(chart.plot);
  lab.attach(id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()));
  lab.shot({id, chart.title, caption, look_for, {{"gale", lab.arg("gale") + " m/s"}}},
           gl::to_picture(chart.plot));
}

// THE WIND: the share each liquid keeps by wavelength, and the drift.
void wind(gl::Lab& lab) {
  const double gale = lab.arg_num("gale");
  gl::LineChart line;
  line.frame = {"WHAT A GALE GROWS ON EACH LIQUID",
                "the share of the wind's input a wave keeps against its viscous loss",
                "log10 of "
                "the wavelength (m)",
                "kept"};
  line.spikes = false;
  gl::LabTable table{"The wind on each liquid",
                     {"liquid", "density kg/m3", "viscosity Pa s", "yield Pa", "kept at 0.5 m",
                      "kept at 5 m", "drift m/s (2 m deep)"},
                     {}};
  for (const gw::NamedLiquid& n : gw::liquids()) {
    const gw::Stuff& s = n.stuff;
    auto keep_at = [&](double lambda) {
      const double k = 2 * 3.141592653589793 / lambda;
      const double w = std::sqrt(kG * k + s.liquid.tension / s.density * k * k * k);
      return gw::wind_keep(s.liquid, s.density, gale, k, w);
    };
    // The sea and the crusted lava draw on the water's and the lava's lines.
    if (n.name != "sea" && n.name != "lava-crusted") {
      gl::Trace tr{std::string(n.name), {}, {}};
      for (int i = 0; i <= 80; ++i) {
        const double lg = -1.5 + 2.8 * i / 80.0;
        tr.x.push_back(lg);
        tr.y.push_back(keep_at(ge::core::libm::pow(10.0, lg)));
      }
      line.traces.push_back(tr);
    }
    const double dr = gw::drift(s.liquid, gale, 2);
    table.rows.push_back({std::string(n.name), fixed(s.density, 0), fixed(s.liquid.viscosity, 4),
                          fixed(s.liquid.yield, 2), fixed(keep_at(0.5), 2), fixed(keep_at(5), 2),
                          fixed(dr, 3)});
    lab.metric("drift." + std::string(n.name), dr, "m/s");
  }
  chart_shot(lab, "wind", gl::line_chart(line),
             "Under a gale, each wavelength's growth from the wind (its rate rising as the wind "
             "outruns the wave, and falling with the liquid's density) against its loss to "
             "viscosity (4 nu k^2, the short waves first). Water keeps nearly everything; the "
             "runny mucus loses its ripples under a few tens of cm; the toxic slime keeps only "
             "swells longer than 15 m, which its pit has no room for; the thicker slimes and the "
             "lava keep nothing.",
             "water flat at 1; the mucus rising toward 1 past half a metre; the toxic slime "
             "lifting off 0 only at the right edge; every other liquid at 0");
  lab.table(table);
}

// THE SWIM: the scales each liquid gives a swimmer, and how far a stroke takes him.
void swim(gl::Lab& lab) {
  gl::LabTable table{
      "The swimmer in each liquid",
      {"liquid", "stroke m/s", "speed share", "friction scale", "sink scale", "metres in 10 s"},
      {}};
  for (const gw::NamedLiquid& n : gw::liquids()) {
    const gw::Stuff& s = n.stuff;
    const double v = gw::stroke_speed(s.density, s.liquid.viscosity, s.liquid.yield);
    const gw::SwimScale& sc = s.liquid.swim;
    table.rows.push_back({std::string(n.name), fixed(v, 3), fixed(sc.speed, 3),
                          fixed(sc.friction, 2), fixed(sc.sink, 2), fixed(v * 10, 2)});
    lab.metric("stroke." + std::string(n.name), v, "m/s");
  }
  lab.table(table);
}

// THE HARM: the damage taken over time at each immersion, and the heat over a lava pool.
void harm(gl::Lab& lab) {
  gl::LineChart burns, ticks;
  burns.frame = {"WHAT LAVA DOES TO A BODY", "damage taken (100 is a life)", "t (s)", "damage"};
  ticks.frame = {"WHAT THE SLIMES DO TO A BODY", "damage taken (100 is a life)", "t (s)", "damage"};
  burns.spikes = ticks.spikes = false;
  gl::LabTable table{"Damage by immersion",
                     {"liquid", "level", "damage in 1 s", "damage in 10 s", "s to 100"},
                     {}};
  for (const gw::NamedLiquid& n : gw::liquids()) {
    const gw::Stuff& s = n.stuff;
    if (s.liquid.harm.kind == gw::HarmKind::none) continue;
    const bool heat = s.liquid.harm.kind == gw::HarmKind::heat;
    for (const int level : {1, 2, 3}) {
      gw::HarmState st;
      double total = 0, at1 = 0, at10 = 0, dead = -1;
      gl::Trace tr{std::string(n.name) + " level " + std::to_string(level), {}, {}};
      const double seconds = heat ? 6 : 30;
      for (int i = 1; i <= static_cast<int>(seconds * 120); ++i) {
        total += gw::step_harm(s.liquid.harm, s.temperature, level, st, kDt);
        if (i == 120) at1 = total;
        if (i == 1200) at10 = total;
        if (dead < 0 && total >= 100) dead = i * kDt;
        if (i % 6 == 0) {
          tr.x.push_back(i * kDt);
          tr.y.push_back(std::fmin(total, 120.0));
        }
      }
      if (heat) {
        burns.traces.push_back(tr);
      } else if (level > 1) {
        ticks.traces.push_back(tr);
      }
      table.rows.push_back({std::string(n.name), std::to_string(level), fixed(at1, 1),
                            heat ? "-" : fixed(at10, 1), dead < 0 ? "never" : fixed(dead, 2)});
      lab.metric("dead." + std::string(n.name) + "." + std::to_string(level), dead, "s");
    }
  }
  burns.rules.push_back(100);
  ticks.rules.push_back(100);
  chart_shot(lab, "burns", gl::line_chart(burns),
             "A body held at each immersion (1 the feet, 2 the waist, 3 all in) in each lava for "
             "6 s. Lava burns by the thermal dose: the flux into the wetted skin to the 4/3 "
             "power, summed; a life is the dose that kills half of those exposed.",
             "every line past 100 within a second all in and within five seconds feet first; the "
             "crusted lava a little slower than the molten");
  chart_shot(
      lab, "ticks", gl::line_chart(ticks),
      "A body held waist deep and all in in each harmful slime for 30 s: the slimes hurt "
      "on a clock, a tick a second, harder the deeper.",
      "stairs, one step a second, the all-in stairs steeper; the toxic slime's steepest; the "
      "gel all in hidden under the sludge waist deep (the same 6 a tick)");
  lab.table(table);

  gl::LabTable heat{"Radiant heat over a lava pool 20 m across",
                    {"surface", "C", "height m", "flux kW/m2", "s to pain", "s to 2nd degree"},
                    {}};
  const struct {
    const char* name;
    double c, emissivity;
  } faces[] = {{"open melt", 1150, 0.9}, {"crust", 700, 0.95}};
  for (const auto& f : faces) {
    for (const double h : {1.0, 2.0, 5.0, 10.0}) {
      const double q = gw::radiant_flux(f.c, f.emissivity, 10, h);
      const double rate = gw::dose_rate(q);
      heat.rows.push_back({f.name, fixed(f.c, 0), fixed(h, 0), fixed(q / 1000, 1),
                           rate > 0 ? fixed(gw::kDosePain / rate, 1) : "never",
                           rate > 0 ? fixed(gw::kDoseSecond / rate, 1) : "never"});
    }
  }
  lab.table(heat);
}

void run(gl::Lab& lab) {
  wind(lab);
  swim(lab);
  harm(lab);
}

const gl::LabSpec kSpec{
    "liquids",
    gl::LabKind::picture,
    "engine",
    "engine/water/liquids.hpp",
    "water",
    "Does each named liquid keep the ripples, the drift, the swim and the harm its viscosity, "
    "its yield, its density and its heat call for?",
    {{"gale", "18", "the wind, m/s"}},
    "--gale=18",
    run};
GE_LAB(kSpec);

}  // namespace
