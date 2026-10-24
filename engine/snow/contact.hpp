// THE CONTACT LAWS (docs/snow.md § Contact): what snow does to anything standing, rolling or
// sliding on it, as pure functions of the snow's kind and column and of the contact. A ski, a
// tread, a tyre and a boot are configurations of these laws, not separate models.
//
//   SINK: Bekker's pressure–sinkage law p = (k_c / b + k_phi) · z^n for a contact b wide, turned
//     round to the sink z a pressure reaches (`bekker_sink`), and capped by what the column can
//     compact: h · (1 − rho / rho_f(p)), rho_f the density that pressure packs snow to
//     (`reach_density`, press.hpp). `column_sink` is the smaller, the one `press` uses.
//   RESISTANCE: the work of pressing a rut, b · ∫ p dz (`compaction_resistance`), and of pushing
//     snow ahead of a sunk front: Rankine's passive pressure over the front plus the momentum
//     given to the snow it sweeps (`bulldozing_resistance`).
//   TRACTION: Mohr–Coulomb strength tau_max = c + p · tan(phi) (`shear_strength`), reached along
//     the slip by Janosi–Hanamoto's 1 − e^(−j/K) (`shear_at`), averaged over a contact `length`
//     long slipping at a ratio i (`tread_traction`): a spinning track digs, a creeping one grips.
//   GLIDE: a base's friction on snow by kind and temperature, lowest near −5 °C, rising in deep
//     cold (dry grains) and towards 0 °C (water); a tyre's on snow and ice (`glide_friction`,
//     `tyre_friction`).
//   PLANING: a fast contact is held up by the snow it does not have time to compact; its sink
//     falls as exp(−(v / v_plane)²) of the static sink (`planing_sink`).
//
// THE ROWS: settled snow's moduli, cohesion, friction angle and shear modulus are the published
// set for natural snow cover (n 1.6, k_c 4.37 kN/m^2.6, k_phi 196.72 kN/m^3.6, c 1.03 kPa, phi
// 19.7°). The other kinds are FITTED, each to the sink a 10 kPa plate 10 cm wide reaches in it
// (`sink10`, in the row's comment; powder's to a standing ski's sink in cold new snow), and their
// strength rises with how packed and bonded the kind is. They are
// starting values a game may override by passing its own `ContactRow`.
//
// Units: lengths m, pressures Pa, forces N, speeds m/s, temperatures °C; the moduli are kept in
// kN as published and converted inside.
//
// ge:summary The snow contact laws: Bekker sinkage, compaction and bulldozing, Mohr–Coulomb and
// ge:summary Janosi traction, glide and tyre friction by temperature, planing
// ge:keywords snow contact bekker sinkage pressure-sinkage compaction bulldozing resistance
// ge:keywords mohr-coulomb janosi hanamoto shear traction slip glide friction tyre planing
// ge:keywords contact_row bekker_sink column_sink tread_traction glide_friction planing_sink
// ge:skill snow
// ge:test tests/snow_contact_test.cpp
#pragma once

#include "snow/kinds.hpp"

namespace ge::snow {

struct ContactRow {
  double n;         // Bekker's sinkage exponent
  double kc;        // kN/m^(n+1): the cohesive modulus (divided by the width)
  double kphi;      // kN/m^(n+2): the frictional modulus
  double cohesion;  // Pa
  double phi;       // degrees: the internal friction angle
  double shear_k;   // m: Janosi–Hanamoto's shear deformation modulus
  double glide;     // a waxed base's friction at −5 °C
  double tyre;      // a winter tyre's peak friction at −5 °C
};

inline constexpr ContactRow kContacts[] = {
    // powder: cold new snow, sink10 0.58 m (a standing ski, 7 cm at 3.5 kPa, sinks 27 cm)
    {1.6, 1.0, 14, 300, 15, 0.050, 0.060, 0.20},
    // settled: the published natural-snow set (sink10 0.137 m)
    {1.6, 4.37, 196.72, 1030, 19.7, 0.040, 0.050, 0.25},
    // wind-packed: sink10 0.02 m
    {1.2, 20, 895, 8000, 22, 0.020, 0.045, 0.30},
    // groomed: sink10 0.01 m
    {1.1, 20, 1385, 10000, 23, 0.015, 0.040, 0.30},
    // spring: wet corn, sink10 0.04 m
    {1.3, 10, 558, 3000, 25, 0.025, 0.055, 0.25},
    // crust: the lid, sink10 0.01 m (breaking through it is M29c's)
    {1.0, 50, 500, 12000, 22, 0.015, 0.042, 0.30},
    // machine: sink10 0.012 m
    {1.1, 20, 1097, 12000, 24, 0.015, 0.047, 0.30},
    // ice: sink10 0.5 mm; no lug bites into it, so it holds by friction alone (phi = atan 0.1)
    {1.0, 0, 20000, 0, 5.7, 0.005, 0.020, 0.10},
};
static_assert(sizeof(kContacts) / sizeof(kContacts[0]) == static_cast<int>(Kind::count),
              "one contact row per kind");

inline const ContactRow& contact_row(Kind k) { return kContacts[static_cast<int>(k)]; }

// The width a shape bears on, m: its narrower dimension (Bekker's b).
double bearing_width(double half_width, double half_length);

// k_c / b + k_phi in Pa/m^n, for a contact `b` m wide.
double bekker_modulus(const ContactRow& row, double b);
// The pressure Pa that sinks a contact `b` wide by `z` m.
double bekker_pressure(const ContactRow& row, double b, double z);
// The sink m a pressure `pa` reaches under a contact `b` wide, with no base in reach.
double bekker_sink(const ContactRow& row, double b, double pa);
// The most a column `depth` m deep at `density` kg/m3 can sink under `pa` before it is packed to
// what that pressure reaches (`cap` the kind's densest).
double sink_cap(double depth, double density, double pa, double cap);
// The sink under `pa`: Bekker's, capped by the column (the smaller of the two).
double column_sink(Kind kind, double depth, double density, double b, double pa);

// The force N to press a rut `z` deep under a contact `b` wide, per unit of travel.
double compaction_resistance(const ContactRow& row, double b, double z);
// The force N on a front `b` wide sunk `z` into snow of `density`, moving at `v`.
double bulldozing_resistance(const ContactRow& row, double density, double b, double z, double v);

// Mohr–Coulomb: the shear strength Pa under a normal pressure `pa`.
double shear_strength(const ContactRow& row, double pa);
// Janosi–Hanamoto: the shear stress Pa developed after a slip of `j` m.
double shear_at(const ContactRow& row, double pa, double j);
// The traction N of a contact of `area` m2 and `length` m at a pressure `pa`, slipping at a
// ratio `slip` (0 none, 1 spinning in place): the shear averaged along the contact.
double tread_traction(const ContactRow& row, double area, double pa, double slip, double length);

// A waxed base's friction coefficient on `kind` at `temp_c`.
double glide_friction(Kind kind, double temp_c);
// A winter tyre's peak friction coefficient on `kind` at `temp_c` (ice near 0 °C is the worst).
double tyre_friction(Kind kind, double temp_c);

// The sink of a contact moving at `v`, given its sink at rest and the speed it planes at.
double planing_sink(double static_sink, double v, double v_plane);

}  // namespace ge::snow
