// The machines' ladders, their subjects and the hand-over between the skis and a ride.
//
// ge:summary The machines' ladders, their subjects and the hand-over between the skis and a ride
// ge:keywords fall-line camera machine ladder sled heli para hand-over ride
// ge:skill camera
// ge:test tests/fall_line_camera_test.cpp
#include "fall_line/camera/machines.hpp"

#include <array>
#include <cmath>

#include "camera/ladders.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "fall_line/sim/heli_ride.hpp"
#include "fall_line/sim/para_ride.hpp"
#include "fall_line/sim/sled_ride.hpp"

namespace fall_line::camera {

namespace cam = ge::camera;
namespace core = ge::core;
namespace libm = ge::core::libm;

namespace {

using Ladder = std::array<cam::Rig, kMachineRungs>;
using Names = std::array<std::string_view, kMachineRungs>;

constexpr std::array<std::string_view, 4> kRideNames = {"skis", "sled", "heli", "para"};
constexpr std::array<Names, kMachines> kNames = {{
    {"seat", "chase", "far", "high"},
    {"seat", "chase", "far", "down"},
    {"pilot", "chase", "wing", "down"},
}};

// A change of rung on a machine's ladder, s: the skis' own (rungs.cpp).
constexpr double kLensS = 0.6;
// A boom's fall look that never comes in: a craft in the air would sit in it for the whole flight.
constexpr double kNoFallLook = 1e6;

cam::Rig reference(std::string_view name) {
  for (const cam::Rig& r : cam::reference_ladder()) {
    if (r.name == name) return r;
  }
  return cam::Rig{};
}

// The worn eye's row; its eye is a placeholder, carried in off the figure every frame.
cam::Rig worn(std::string_view name, double down, double roll_share, double grain_m,
              double grain_hz) {
  cam::MountedRow r = reference("helmet").mounted;
  r.down = down;
  r.roll_share = roll_share;
  r.grain_m = grain_m;
  r.grain_hz = grain_hz;
  return cam::mounted_rig(name, r);
}

Ladder sled() {
  const Names& n = kNames[0];
  Ladder l{};
  // On the boards: the bars' roll nearly all his, the belt's buzz coarser than skis'.
  l[0] = worn(n[0], 0.06, 0.8, 0.004, 14);
  cam::BoomRow chase;
  chase.dist_m = 5.5;
  chase.height_m = 2.0;
  chase.aim_height_m = 0.5;  // over the CoG: the seat
  chase.tremor_from_mps = 6;
  chase.tremor_full_mps = 20;
  chase.tremor_rad = 0.002;
  chase.bank = 0.15;
  chase.bank_air = 0.1;
  l[1] = cam::boom_rig(n[1], chase);
  cam::BoomRow far = reference("far").boom;
  far.dist_m = 11;
  far.height_m = 4;
  far.aim_height_m = 0.5;
  l[2] = cam::boom_rig(n[2], far);
  cam::BoomRow high = reference("high").boom;
  high.aim_height_m = 0.5;
  l[3] = cam::boom_rig(n[3], high);
  return l;
}

// A boom flying behind a craft: the fall look off, banked with it in the air, the travel's
// direction weighed more than the nose's (a craft crabs and slips), held well off the trees.
cam::BoomRow flier(double dist, double height, double aim, double bank_air, double clearance) {
  cam::BoomRow r;
  r.dist_m = dist;
  r.height_m = height;
  r.aim_height_m = aim;
  r.fov_y = 1.0;
  r.fov_per_mps = 0.006;
  r.fov_max = 1.3;
  r.tremor_rad = 0.0006;
  r.slip = 0.6;
  r.lift_air = cam::Poles{1.2, 0.85, 2};
  r.lag_max_m = dist * 0.4;
  r.incline = 0.25;
  r.incline_steep = 0.3;
  r.bank = 0.05;
  r.bank_air = bank_air;
  r.fall_from_m = kNoFallLook;
  r.clearance_m = clearance;
  return r;
}

// The look down the long fall under a craft: the arm climbs over it and looks down past it.
cam::BoomRow down(double dist, double height, double aim) {
  cam::BoomRow r = flier(dist, height, aim, 0.3, 3);
  r.fall_from_m = 0;
  r.fall_up = 1.2;
  r.fall_reach = 1.2;
  return r;
}

Ladder heli() {
  const Names& n = kNames[1];
  Ladder l{};
  // Sat on the skid, looking down past it, rolled with the airframe; the rotor's fine buzz.
  l[0] = worn(n[0], 0.25, 1.0, 0.0015, 18);
  l[1] = cam::boom_rig(n[1], flier(12, 3.5, 1.4, 0.6, 2.5));
  cam::BoomRow far = flier(30, 10, 1.4, 0.3, 4);
  far.yaw = cam::Poles{0.7, 0.9, 0};
  l[2] = cam::boom_rig(n[2], far);
  l[3] = cam::boom_rig(n[3], down(14, 8, 1.4));
  return l;
}

Ladder para() {
  const Names& n = kNames[2];
  Ladder l{};
  // In the harness: the wing's bank is his.
  l[0] = worn(n[0], 0.1, 1.0, 0.001, 9);
  l[1] = cam::boom_rig(n[1], flier(7, 1.5, 1.0, 0.7, 2));
  // Up by the canopy behind him, looking down on him.
  cam::BoomRow wing = flier(9, 9, 1.0, 0.4, 3);
  wing.place = 0.1;
  l[2] = cam::boom_rig(n[2], wing);
  l[3] = cam::boom_rig(n[3], down(10, 6, 1.0));
  return l;
}

// Built on first use: the reference rows are another file's statics.
const std::array<Ladder, kMachines>& ladders() {
  static const std::array<Ladder, kMachines> l = {sled(), heli(), para()};
  return l;
}

int machine_index(Ride r) { return static_cast<int>(r) - 1; }

cam::Edit lens_edit(int from, int to) {
  return from == 0 || to == 0 ? cam::Edit::blend : cam::Edit::inertialize;
}

// One camera framed and settled: the ride's own subject, its worn eye off the figure.
cam::Pose frame_one(RideCamera& rc, Ride r, const sim::GameState& state, const ge::skier::World& sw,
                    const cam::World& cw, const core::Vec3& head, double dt) {
  const ge::skier::SkierState& sk = state.racers[0].skier;
  if (r == Ride::skis) return frame_camera(rc.skis, sk, sw, cw, dt);
  const int m = machine_index(r);
  cam::Camera& c = rc.machines[m];
  const cam::Subject s = machine_subject(state, r, rc.memory[m]);
  const core::Vec3 eye = helmet_eye(sk, sw, head);
  c.rigs[0].mounted.eye_body =
      core::unrotate(s.q, core::Vec3{eye.x - s.pos.x, eye.y - s.pos.y, eye.z - s.pos.z});
  cam::Pose p = cam::update(c, s, cw, dt);
  const bool on_worn = c.rung == 0 && !cam::in_transition(c);
  settle_lens(p, cw, head, !on_worn, sk.x, sk.z);
  return p;
}

// Two poses blended round `c`: the eye swung round him on the shorter way at a blended reach
// and height, so two arms on either side of him never cross through him on the straight line
// between them. Lenses within `kWornReach` of him (worn) blend on the straight line.
constexpr double kWornReach = 0.6;
cam::Pose blend_round(const cam::Pose& a, const cam::Pose& b, double k, const cam::Vec3& c) {
  cam::Pose p = cam::blend_pose(a, b, k);
  const double ax = a.eye.x - c.x, az = a.eye.z - c.z, bx = b.eye.x - c.x, bz = b.eye.z - c.z;
  const double ra = std::sqrt(ax * ax + az * az), rb = std::sqrt(bx * bx + bz * bz);
  if (ra < kWornReach || rb < kWornReach) return p;
  const double ya = libm::atan2(ax, az), yb = libm::atan2(bx, bz);
  const double y = ya + core::angle_diff(ya, yb) * k;
  const double r = ra + (rb - ra) * k;
  p.eye.x = c.x + r * libm::sin(y);
  p.eye.z = c.z + r * libm::cos(y);
  return p;
}

// Put the ride's camera on `rung` at once, snapped onto its subject on its next frame.
void begin(RideCamera& rc, Ride r, int rung) {
  if (r == Ride::skis) {
    rc.skis.cam.rung = rung;
    cam::snap(rc.skis.cam);
    rc.skis.memory = SubjectMemory{};
    return;
  }
  const int m = machine_index(r);
  rc.machines[m].rung = rung;
  cam::snap(rc.machines[m]);
  rc.memory[m] = SubjectMemory{};
}

}  // namespace

std::string_view ride_name(Ride r) { return kRideNames[static_cast<size_t>(r)]; }

Ride ride_of(const sim::GameState& state) {
  if (sim::on_sled(state, 0)) return Ride::sled;
  if (sim::on_heli(state, 0)) return Ride::heli;
  if (sim::under_para(state, 0)) return Ride::para;
  return Ride::skis;
}

std::span<const cam::Rig> machine_ladder(Ride r) {
  if (r == Ride::skis) return {};
  return ladders()[static_cast<size_t>(machine_index(r))];
}

std::string_view lens_name(Ride r, int rung) {
  if (r == Ride::skis) return rung_name(static_cast<Rung>(rung));
  return kNames[static_cast<size_t>(machine_index(r))][static_cast<size_t>(rung)];
}

bool lens_named(Ride r, std::string_view name, int& out) {
  if (r == Ride::skis) {
    Rung g{};
    if (!rung_named(name, g)) return false;
    out = static_cast<int>(g);
    return true;
  }
  const Names& n = kNames[static_cast<size_t>(machine_index(r))];
  for (size_t i = 0; i < n.size(); ++i) {
    if (n[i] == name) {
      out = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

cam::Subject machine_subject(const sim::GameState& state, Ride r, SubjectMemory& memory) {
  cam::Subject out;
  const ge::skier::SkierState& sk = state.racers[0].skier;
  double vy = 0;
  switch (r) {
    case Ride::sled: {
      const auto& b = state.sled.body;
      out.pos = {b.x, b.y, b.z};
      out.vel = {b.vx, b.vy, b.vz};
      out.q = b.q;
      out.heading = b.heading;
      out.grounded = !b.airborne;
      out.surface = b.airborne ? 0.0 : b.packed;
      vy = b.vy;
      break;
    }
    case Ride::heli: {
      const auto& b = state.heli.body;
      out.pos = {b.x, b.y, b.z};
      out.vel = {b.vx, b.vy, b.vz};
      out.q = core::from_euler(b.heading, b.pitch, b.roll);
      out.heading = b.heading;
      out.grounded = b.grounded;
      vy = b.vy;
      break;
    }
    case Ride::para:
    case Ride::skis: {
      // The pilot hung under the wing (or stood under it on the snow), the wing's way and bank.
      const auto& b = state.para.body;
      out.pos = {sk.x, sk.y, sk.z};
      out.vel = {sk.vx, sk.vy, sk.vz};
      out.q = core::from_euler(b.heading, 0, b.bank);
      out.heading = b.heading;
      out.grounded = sk.on_snow;
      out.surface = sk.on_snow ? sk.packed : 0.0;
      vy = sk.vy;
      break;
    }
  }
  out.look_yaw = out.heading;
  read_moments(out, vy, out.grounded, memory);
  return out;
}

int machine_rung_of(Rung skis) {
  switch (skis) {
    case Rung::tips:
    case Rung::helmet:
      return 0;
    case Rung::chase:
    case Rung::orbit:
      return 1;
    case Rung::far:
      return 2;
    case Rung::high:
      return 3;
  }
  return 1;
}

Rung skis_rung_of(int machine_rung) {
  switch (machine_rung) {
    case 0:
      return Rung::helmet;
    case 2:
      return Rung::far;
    case 3:
      return Rung::high;
    default:
      return Rung::chase;
  }
}

RideCamera make_ride_camera(Rung start, uint32_t seed) {
  RideCamera rc;
  rc.skis = make_skier_camera(start, seed);
  for (int m = 0; m < kMachines; ++m) {
    const Ladder& l = ladders()[static_cast<size_t>(m)];
    rc.machines[m] = cam::make_camera(l.data(), kMachineRungs, seed + 1 + static_cast<uint32_t>(m));
    rc.machines[m].rung = machine_rung_of(start);
  }
  return rc;
}

int active_rung(const RideCamera& rc) {
  if (rc.ride == Ride::skis) return rc.skis.cam.rung;
  return rc.machines[machine_index(rc.ride)].rung;
}

std::string_view active_lens(const RideCamera& rc) { return lens_name(rc.ride, active_rung(rc)); }

bool handing_over(const RideCamera& rc) { return rc.from != rc.ride; }

void next_lens(RideCamera& rc) {
  if (rc.ride == Ride::skis) {
    change_rung(rc.skis.cam, next_rung(rung_of(rc.skis.cam)));
    return;
  }
  cam::Camera& c = rc.machines[machine_index(rc.ride)];
  const int to = (c.rung + 1) % kMachineRungs;
  cam::set_rung(c, to, lens_edit(c.rung, to), kLensS);
}

bool set_lens(RideCamera& rc, const sim::GameState& state, std::string_view name) {
  const Ride r = ride_of(state);
  int rung = 0;
  if (!lens_named(r, name, rung)) {
    Rung g{};
    if (r == Ride::skis || !rung_named(name, g)) return false;
    rung = machine_rung_of(g);  // a skis' rung named on a machine: its counterpart
  }
  rc.ride = r;
  rc.from = r;
  rc.framed = true;
  begin(rc, r, rung);
  return true;
}

void apply_ride_cvars(cam::CameraCvars& v, RideCamera& rc) {
  cam::apply_camera_cvars(v, fall_line_ladder(), rc.skis.cam);
  for (int m = 0; m < kMachines; ++m) {
    cam::apply_camera_cvars(v, ladders()[static_cast<size_t>(m)], rc.machines[m]);
  }
}

cam::Pose frame_ride(RideCamera& rc, const sim::GameState& state, const ge::skier::World& sw,
                     const cam::World& cw, double dt) {
  const Ride now = ride_of(state);
  if (!rc.framed) {
    rc.ride = rc.from = now;
    rc.framed = true;
  } else if (now != rc.ride) {
    if (handing_over(rc) && now == rc.from) {
      // Back the way it came before the hand-over was done: run it backwards from where it is.
      rc.from = rc.ride;
      rc.t = kRideHandOverS - rc.t;
    } else {
      const int rung = active_rung(rc);
      const int to = now == Ride::skis       ? static_cast<int>(skis_rung_of(rung))
                     : rc.ride == Ride::skis ? machine_rung_of(static_cast<Rung>(rung))
                                             : rung;
      begin(rc, now, to);
      rc.from = rc.ride;
      rc.t = 0;
    }
    rc.ride = now;
  }
  const core::Vec3 head = head_of(state.racers[0].skier, sw);
  const cam::Pose arriving = frame_one(rc, rc.ride, state, sw, cw, head, dt);
  if (!handing_over(rc)) return arriving;
  const cam::Pose leaving = frame_one(rc, rc.from, state, sw, cw, head, dt);
  rc.t += dt;
  const double k = rc.t >= kRideHandOverS ? 1.0 : cam::smootherstep(rc.t / kRideHandOverS);
  const ge::skier::SkierState& sk = state.racers[0].skier;
  cam::Pose p = blend_round(leaving, arriving, k, framed_point(sk));
  // Two worn lenses look out of his head on different ways: the turn between them is from inside
  // it, so the head is clipped through the whole hand-over.
  settle_lens(p, cw, head, true, sk.x, sk.z);
  if (rc.t >= kRideHandOverS) rc.from = rc.ride;
  return p;
}

}  // namespace fall_line::camera
