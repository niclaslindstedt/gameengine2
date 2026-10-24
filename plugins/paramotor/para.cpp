// The paramotor's step (para.hpp).
//
// ge:summary The paramotor's step: the hands, the engine, the wing on its lines, the fold, the hang
// ge:keywords paramotor para step_para ready_para drop_para polar lines swing fold hang collapse
// ge:keywords crown edge-wind para_hash acro dive loop tangle
// ge:skill paramotor
// ge:test tests/paramotor_scenarios_test.cpp
#include "paramotor/para.hpp"

#include <cmath>
#include <cstring>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "paramotor/acro.hpp"
#include "paramotor/spec.hpp"

namespace ge::paramotor {

namespace {

namespace mg = mountain::mapgen;
namespace lm = core::libm;
namespace num = core::num;
using core::Vec3;

// A body of the rig: where, m, and how fast, m/s.
struct Body {
  double x, y, z, vx, vy, vz;
};

Body wing_of(const ParaState& p) { return Body{p.x, p.y, p.z, p.vx, p.vy, p.vz}; }
void put_wing(ParaState& p, const Body& b) {
  p.x = b.x, p.y = b.y, p.z = b.z, p.vx = b.vx, p.vy = b.vy, p.vz = b.vz;
}
// The pilot's harness, where the lines meet him.
Body pilot_of(const skier::SkierState& s) {
  return Body{s.x, s.y + spec::kHarness, s.z, s.vx, s.vy, s.vz};
}
void put_pilot(skier::SkierState& s, const Body& b) {
  s.x = b.x, s.y = b.y - spec::kHarness, s.z = b.z, s.vx = b.vx, s.vy = b.vy, s.vz = b.vz;
  s.speed = core::hypot3(s.vx, s.vy, s.vz);
}

// THE LINES' DIRECTION, the pilot to the wing, unit; straight up when the two stand together.
Vec3 line_dir(const Body& wing, const Body& pilot) {
  const double ux = wing.x - pilot.x;
  const double uy = wing.y - pilot.y;
  const double uz = wing.z - pilot.z;
  const double len = core::hypot3(ux, uy, uz);
  if (len > 1e-6) return Vec3{ux / len, uy / len, uz / len};
  return Vec3{0, 1, 0};
}

// THE AIR'S FORCE on the wing, N: `lift` (a coefficient) square to the air `a` (of speed `v`) on
// the lines' (`u`) side, `drag` down the air, both times `qs`. Nothing below a crawl of air.
Vec3 lift_drag(Vec3 a, double v, Vec3 u, double qs, double lift, double drag) {
  if (v <= 0.1) return Vec3{0, 0, 0};
  const double vx = a.x / v, vy = a.y / v, vz = a.z / v;
  const double along = u.x * vx + u.y * vy + u.z * vz;
  double lx = u.x - along * vx, ly = u.y - along * vy, lz = u.z - along * vz;
  const double ll = core::hypot3(lx, ly, lz);
  if (ll > 1e-6) lx /= ll, ly /= ll, lz /= ll;
  return Vec3{qs * (lift * lx - drag * vx), qs * (lift * ly - drag * vy),
              qs * (lift * lz - drag * vz)};
}

// THE SWING DAMPED: the wing's motion about the pilot square to the lines, against the air; the
// force on the wing returned, its opposite taken off the pilot (of mass `m`) now.
Vec3 swing_damp(const Body& wing, Body& pilot, Vec3 u, double m, double dt) {
  const double rvx = wing.vx - pilot.vx, rvy = wing.vy - pilot.vy, rvz = wing.vz - pilot.vz;
  const double rl = rvx * u.x + rvy * u.y + rvz * u.z;
  const Vec3 d{-spec::kDamping * (rvx - rl * u.x), -spec::kDamping * (rvy - rl * u.y),
               -spec::kDamping * (rvz - rl * u.z)};
  pilot.vx -= d.x / m * dt;
  pilot.vy -= d.y / m * dt;
  pilot.vz -= d.z / m * dt;
  return d;
}

// THE LINES PULLED TIGHT: the stretch past their length taken out of both bodies by their masses,
// and their speed apart along the lines. The pull, N (0 slack).
double pull_lines(Body& wing, Body& pilot, double m, double dt) {
  const double iw = 1 / spec::wing::kMass;
  const double ip = 1 / m;
  const double sum = iw + ip;
  double nx = wing.x - pilot.x, ny = wing.y - pilot.y, nz = wing.z - pilot.z;
  const double d = core::hypot3(nx, ny, nz);
  if (d <= spec::wing::kLines) return 0;
  nx /= d, ny /= d, nz /= d;
  const double err = d - spec::wing::kLines;
  wing.x -= nx * err * (iw / sum);
  wing.y -= ny * err * (iw / sum);
  wing.z -= nz * err * (iw / sum);
  pilot.x += nx * err * (ip / sum);
  pilot.y += ny * err * (ip / sum);
  pilot.z += nz * err * (ip / sum);
  const double apart =
      (wing.vx - pilot.vx) * nx + (wing.vy - pilot.vy) * ny + (wing.vz - pilot.vz) * nz;
  if (apart <= 0) return 0;
  const double j = apart / sum;
  wing.vx -= nx * j * iw;
  wing.vy -= ny * j * iw;
  wing.vz -= nz * j * iw;
  pilot.vx += nx * j * ip;
  pilot.vy += ny * j * ip;
  pilot.vz += nz * j * ip;
  return j / dt;
}

// THE EDGE: how deep into the band along the map's edge the wing is, the wind there blowing it
// back in, and the heading home to the middle.
struct Edge {
  double depth = 0, wx = 0, wz = 0, home = 0;
};

Edge edge_at(const mg::Level& level, double x, double z, double agl) {
  const double band = spec::edge::kBand;
  const double size = level.size;
  const auto into = [band](double lo, double hi) {
    return num::max(0.0, (band - lo) / band) - num::max(0.0, (band - hi) / band);
  };
  const double ax = core::clamp(into(x, size - x), -2.0, 2.0);
  const double az = core::clamp(into(z, size - z), -2.0, 2.0);
  const double high =
      core::clamp((agl - spec::edge::kLow) / (spec::edge::kHigh - spec::edge::kLow), 0.0, 1.0);
  Edge e;
  e.depth = num::max(std::fabs(ax), std::fabs(az)) * high;
  e.wx = spec::edge::kWind * ax * std::fabs(ax) * high;
  e.wz = spec::edge::kWind * az * std::fabs(az) * high;
  e.home = lm::atan2(size / 2 - x, size / 2 - z);
  return e;
}

// The air at the wing, the edge's wind with it, and what the rig reads of it.
ParaAir read_air(ParaState& p, const ParaWorld& w) {
  ParaAir air = para_air_at(*w.level, *w.field, w.wind, w.now, p.x, p.y, p.z);
  const Edge e = edge_at(*w.level, p.x, p.z, p.agl);
  air.x += e.wx;
  air.z += e.wz;
  p.wind = air.mean;
  p.lift = air.lift;
  p.rough = air.rough;
  return air;
}

// Whether the cloth at (x, y, z) is in a crown: under the tree's top, within `kCrown` m of its
// edge.
bool in_a_crown(const ParaWorld& w, double x, double y, double z) {
  bool in = false;
  skier::for_each_tree_near(*w.trees, x, z, [&](int32_t i) {
    const mg::TreeDef& t = w.level->trees[static_cast<size_t>(i)];
    if (y < t.y + t.height && core::hypot(x - t.x, z - t.z) < t.crown + spec::down::kCrown) {
      in = true;
    }
  });
  return in;
}

// THE WING HELD OVERHEAD on the snow: inflated over him, a little back of plumb, moving with him.
void hold_overhead(ParaState& p, const skier::SkierState& s) {
  const double lean = spec::launch::kLean;
  const double h = s.speed > 1 ? lm::atan2(s.vx, s.vz) : s.heading;
  const double fx = lm::sin(h);
  const double fz = lm::cos(h);
  const double back = lm::sin(lean) * spec::wing::kLines;
  p.heading = h;
  p.x = s.x - fx * back;
  p.y = s.y + spec::kHarness + lm::cos(lean) * spec::wing::kLines;
  p.z = s.z - fz * back;
  p.vx = s.vx, p.vy = s.vy, p.vz = s.vz;
  p.bank = 0;
  p.pitch = 0;
  p.way = Vec3{fx, 0, fz};
  p.airspeed = s.speed;
  p.tension = 0;
}

// A FOLD: the leading edge pushed under its angle tucks, all of it or the tip pushed under alone,
// and the wing refills by itself, the faster with the brakes pumped; one fold at a time, the
// deeper taking over. Only once he flies (on the snow the pilot holds it up), and never while the
// lines are pulled past `acro::kLoaded` g (a wing flung round him hard is held full by the pull);
// his hands on the risers hold its angle off (`reflex_of`).
void fold_in(ParaState& p, double alpha, double alpha_l, double alpha_r, double v, double dt,
             ParaEvents& ev) {
  namespace F = spec::fold;
  if (p.fold > 0) {
    const double rate = (1 + F::kPump * p.controls.brake) * (v > F::kSlow ? 1 : 0.3);
    p.fold = num::max(0.0, p.fold - rate * dt / F::kRefill);
    if (p.fold < 0.02) p.fold = 0;
  }
  if (!p.flying || loaded(p) > 0) return;
  const double deepest = num::min(alpha, num::min(alpha_l, alpha_r));
  const double under = F::kAlpha - reflex_of(p, p.controls.bar);
  if (deepest >= under) return;
  const double depth = core::clamp(F::kLeast + (under - deepest) * F::kDeep, F::kLeast, F::kMost);
  if (depth <= p.fold) return;
  if (p.fold == 0) ev.folded = true;
  p.fold = depth;
  // A frontal when the middle went under; else the side that did.
  p.fold_side = alpha < under ? 0 : (alpha_l < alpha_r ? -1 : 1);
}

// THE WING ON ITS LINES for one step: the air on it, the risers (`acro.hpp`), the turn, the swing
// damped, its weight, the lines pulled tight on both bodies; the swing counted and the slack read.
void fly(ParaState& p, Body& pilot, const ParaWorld& w, double m, double dt, ParaEvents& ev) {
  namespace P = spec::polar;
  namespace B = spec::brakes;
  namespace F = spec::fold;
  namespace T = spec::turn;
  const ParaControls& c = p.controls;
  Body wing = wing_of(p);
  // The lines as they stand at the step's end: the pilot has already moved this step (his skis'
  // step comes first), so the wing is carried on with him at its own speed before the lines are
  // read; read against where it stood, they would lean back by his speed times the step.
  const Vec3 u = line_dir(Body{wing.x + wing.vx * dt, wing.y + wing.vy * dt, wing.z + wing.vz * dt,
                               wing.vx, wing.vy, wing.vz},
                          pilot);
  const ParaAir air = read_air(p, w);
  // The pilot's own air, and the rears' bite and hold off its speed: a wing held in its pitch flies
  // his path, the air through it his.
  const Vec3 ca{pilot.vx - air.x, pilot.vy - air.y, pilot.vz - air.z};
  const double vc = core::hypot3(ca.x, ca.y, ca.z);
  bite_rears(p, c.bar, vc);
  const double held = held_share(p, c.bar, vc);
  const Vec3 a{wing.vx - air.x + (ca.x - wing.vx + air.x) * held,
               wing.vy - air.y + (ca.y - wing.vy + air.y) * held,
               wing.vz - air.z + (ca.z - wing.vz + air.z) * held};
  const double v = core::hypot3(a.x, a.y, a.z);
  const double down = a.x * u.x + a.y * u.y + a.z * u.z;
  // Its way: the air square to the lines (the canopy noses into the flow), else where it last
  // pointed; met by the air from behind it is flying backwards, and collapses whole.
  Vec3 fw;
  if (kept_way(a, down, u, p.way, fw) && p.flying) {
    if (p.fold == 0) ev.folded = true;
    p.fold = 1;
    p.fold_side = 0;
  }
  // THE POLAR: the angle of attack off the trim, the brakes and the risers.
  const double bar = c.bar >= 0 ? c.bar * spec::bar::kAccelerator : -c.bar * spec::bar::kTrimmers;
  const double rig = spec::kRig + B::kRig * c.brake + bar + riser_rig(p, c.bar);
  const double alpha = rig + lm::atan2(-down, num::max(1e-3, a.x * fw.x + a.y * fw.y + a.z * fw.z));
  const double stall = P::kStall - B::kStall * c.brake + held_of(p, c.bar);
  p.stalled = alpha > stall;
  // THE TIPS: the eddies across the span meet each tip at its own angle.
  const Vec3 r0{u.y * fw.z - u.z * fw.y, u.z * fw.x - u.x * fw.z, u.x * fw.y - u.y * fw.x};
  const double half = F::kSpan / 2;
  const mg::Level& level = *w.level;
  const double up_r = eddy_up(level, *w.field, w.wind, w.now, p.x + r0.x * half, p.y + r0.y * half,
                              p.z + r0.z * half, air.rough);
  const double up_l = eddy_up(level, *w.field, w.wind, w.now, p.x - r0.x * half, p.y - r0.y * half,
                              p.z - r0.z * half, air.rough);
  const double up_c = air.y - air.lift;
  const double vs = num::max(4.0, v);
  const double alpha_r = alpha + (up_r - up_c) / vs;
  const double alpha_l = alpha + (up_l - up_c) / vs;
  fold_in(p, alpha, alpha_l, alpha_r, v, dt, ev);
  const double folded = 1 - F::kLift * (p.fold_side == 0 ? 1 : F::kSideLift) * p.fold;
  const double base =
      core::clamp(P::kSlope * (alpha - P::kZero) + B::kLift * c.brake, 0.0, P::kMost);
  const double lift = (p.stalled ? P::kStalled : base) * folded;
  // The rears' lift (`riser_lift`): the wing held in its pitch by the rigging and its path bent,
  // so it bends the pair's path, put on the pilot at the speed he carries.
  const double rears =
      p.stalled ? 0 : (core::clamp(base + riser_lift(p, c.bar), 0.0, P::kMost) - base) * folded;
  const double drag = P::kDrag0 + P::kInduced * lift * lift + B::kDrag * c.brake * c.brake +
                      T::kDrag * std::fabs(c.steer) + F::kDrag * p.fold +
                      (p.stalled ? P::kStallDrag : 0);
  const double qs = 0.5 * spec::kAirDensity * v * v * spec::wing::kArea;
  // The rears' lift bends the pair's path as one: shared by their masses, so the lines between them
  // carry none of it; and the rears hold the wing over him through it. What they put on him he
  // feels (`felt`, N).
  Vec3 b{0, 0, 0};
  double felt = 0;
  if (rears > 0) {
    const Vec3 af =
        lift_drag(ca, vc, u, 0.5 * spec::kAirDensity * vc * vc * spec::wing::kArea, rears, 0);
    const double share = spec::wing::kMass / (spec::wing::kMass + m);
    b = Vec3{af.x * share, af.y * share, af.z * share};
    pilot.vx += (af.x - b.x) / m * dt;
    pilot.vy += (af.y - b.y) / m * dt;
    pilot.vz += (af.z - b.z) / m * dt;
    felt = core::hypot3(af.x - b.x, af.y - b.y, af.z - b.z);
  }
  Vec3 pv{pilot.vx, pilot.vy, pilot.vz};
  const Vec3 hp = hold_pitch(p, Vec3{pilot.x, pilot.y, pilot.z}, pv, held, ca, u, m, dt);
  pilot.vx = pv.x, pilot.vy = pv.y, pilot.vz = pv.z;
  felt -= hp.x * u.x + hp.y * u.y + hp.z * u.z;
  const Vec3 ld = lift_drag(a, v, u, qs, lift, drag);
  double fx = ld.x + b.x + hp.x, fy = ld.y + b.y + hp.y, fz = ld.z + b.z + hp.z;
  // THE TURN: toward the toggle pulled and the weight shifted, and the engine's torque, along the
  // canopy's right; righted by its arc as it banks; rolled by the tips' difference and dragged
  // round toward a folded side.
  const double wh = core::hypot(fw.x, fw.z);
  const double banked = wh > 0.2 ? (u.x * fw.z - u.z * fw.x) / wh
                                 : (-u.y * r0.y) / num::max(0.2, core::hypot(r0.x, r0.z));
  const double side =
      qs *
      ((T::kSide + T::kWeight) * c.steer + T::kTorque * (p.thrust / spec::engine::kThrust) -
       T::kRighting * banked + F::kRoll * (alpha_l - alpha_r) + F::kTurn * p.fold_side * p.fold);
  fx += side * r0.x;
  fy += side * r0.y;
  fz += side * r0.z;
  // THE SURGE: stalled and the brakes let up, the canopy refills and shoots forward over him.
  if (p.stalled) {
    const double surge = qs * P::kSurge * (1 - c.brake);
    fx += surge * fw.x;
    fy += surge * fw.y;
    fz += surge * fw.z;
  }
  const Vec3 d = swing_damp(wing, pilot, u, m, dt);
  fx += d.x, fy += d.y, fz += d.z;
  wing.vx += fx / spec::wing::kMass * dt;
  wing.vy += (fy / spec::wing::kMass - spec::kGravity) * dt;
  wing.vz += fz / spec::wing::kMass * dt;
  wing.x += wing.vx * dt;
  wing.y += wing.vy * dt;
  wing.z += wing.vz * dt;
  p.tension = pull_lines(wing, pilot, m, dt);
  read_load(p, p.tension + num::max(0.0, felt), m, dt);
  put_wing(p, wing);
  p.way = fw;
  // THE SWING counted, a whole turn round him a loop; and the lines gone slack with the wing level
  // with him or below: he falls into it.
  if (p.flying) {
    const Vec3 un = line_dir(wing, pilot);
    if (count_swing(p, un)) ev.looped = true;
    const double apart = core::hypot3(wing.x - pilot.x, wing.y - pilot.y, wing.z - pilot.z);
    if (fallen_in(p, p.tension, m, un, apart, dt)) {
      p.mode = ParaMode::tangled;
      p.t = 0;
      p.controls = ParaControls{};
      ev.tangled = true;
    }
  }
  // What it is drawn by: its heading, its bank over him, its pitch.
  p.heading = lm::atan2(fw.x, fw.z);
  const double hx = lm::cos(p.heading);
  const double hz = -lm::sin(p.heading);
  p.bank = lm::atan2(u.x * hx + u.z * hz, u.y);
  p.pitch = alpha - spec::kRig;
  p.airspeed = v;
  p.alpha = alpha;
}

// THE PILOT IN THE HARNESS, off the snow: turned to the wing's heading and banked under it; near
// the snow stood up, his skis turned to his way over it to land square.
void hang(ParaState& p, skier::SkierState& s, double dt) {
  namespace L = spec::flare;
  const double sq = core::clamp((L::kStand - p.agl) / (L::kStand - L::kSquare), 0.0, 1.0);
  const double travel = core::hypot(s.vx, s.vz) > 1 ? lm::atan2(s.vx, s.vz) : p.heading;
  const double want = p.heading + core::angle_diff(p.heading, travel) * sq;
  const double k = 1 - lm::exp(-dt / L::kSwing);
  s.heading = core::angle_diff(0, s.heading + core::angle_diff(s.heading, want) * k);
  s.yaw_rate = 0;
  s.edge = 0;
  s.lean = core::clamp(p.bank, -1.15, 1.15) * (1 - sq);
}

// The release's pieces: the canopy where the wing is, the motor shoved off his back.
void let_go(ParaState& p, const skier::SkierState& s) {
  p.canopy = ParaPiece{p.x, p.y, p.z, p.vx, p.vy, p.vz, p.heading, false};
  const double fx = lm::sin(s.heading);
  const double fz = lm::cos(s.heading);
  namespace D = spec::dropped;
  p.motor =
      ParaPiece{s.x - fx * 0.35, s.y + spec::kHarness + 0.1, s.z - fz * 0.35, s.vx - fx * D::kShove,
                s.vy + D::kLift, s.vz - fz * D::kShove,      s.heading,       false};
}

// A released piece falling under its drag area `area`, m2, of mass `mass`, kg, lying where it
// lands.
void fall_piece(const mg::Level& level, ParaPiece& b, double mass, double area, double dt) {
  if (b.down) return;
  const double v = core::hypot3(b.vx, b.vy, b.vz);
  const double k = 0.5 * spec::kAirDensity * area * v / mass;
  b.vx -= b.vx * k * dt;
  b.vy -= (b.vy * k + spec::kGravity) * dt;
  b.vz -= b.vz * k * dt;
  b.x += b.vx * dt;
  b.y += b.vy * dt;
  b.z += b.vz * dt;
  const double ground = level.ground_at(b.x, b.z);
  if (b.y <= ground) {
    b.y = ground;
    b.vx = b.vy = b.vz = 0;
    b.down = true;
  }
}

// The propeller turned at the engine's rpm.
void spin(ParaState& p, double dt) {
  p.prop = num::mod(p.prop + p.rpm / 60 * core::kTau * dt, core::kTau);
}

}  // namespace

const char* para_mode_name(ParaMode m) {
  switch (m) {
    case ParaMode::ready:
      return "ready";
    case ParaMode::flown:
      return "flown";
    case ParaMode::dropped:
      return "dropped";
    case ParaMode::tangled:
      return "tangled";
  }
  return "?";
}

const char* para_cut_name(ParaCut c) {
  switch (c) {
    case ParaCut::none:
      return "none";
    case ParaCut::drop:
      return "drop";
    case ParaCut::ground:
      return "ground";
    case ParaCut::crown:
      return "crown";
    case ParaCut::rider:
      return "rider";
  }
  return "?";
}

double pilot_mass(const skier::SkiSpec& spec) { return spec.mass + spec::kMotorMass; }

ParaState ready_para(const skier::SkierState& s) {
  ParaState p;
  p.rpm = spec::engine::kIdle;
  hold_overhead(p, s);
  return p;
}

ParaState aloft_para(skier::SkierState& s, double heading, double speed, double throttle) {
  const double fx = lm::sin(heading);
  const double fz = lm::cos(heading);
  s.heading = heading;
  s.vx = fx * speed;
  s.vy = 0;
  s.vz = fz * speed;
  s.speed = speed;
  s.on_snow = false;
  ParaState p;
  p.mode = ParaMode::flown;
  p.heading = heading;
  p.x = s.x;
  p.y = s.y + spec::kHarness + spec::wing::kLines;
  p.z = s.z;
  p.vx = s.vx, p.vy = 0, p.vz = s.vz;
  p.way = Vec3{fx, 0, fz};
  p.airspeed = speed;
  p.controls.throttle = throttle;
  p.rpm = spec::engine::kIdle + (spec::engine::kFull - spec::engine::kIdle) * throttle;
  p.flying = true;
  p.flights = 1;
  p.air_time = spec::launch::kFlight;
  return p;
}

void drop_para(ParaState& p, const skier::SkierState& s, ParaCut cut, ParaEvents& events) {
  if (p.mode == ParaMode::dropped) return;
  p.mode = ParaMode::dropped;
  p.cut = cut;
  p.t = 0;
  p.flying = false;
  p.tension = 0;
  p.thrust = 0;
  p.fold = 0;
  p.controls = ParaControls{};
  let_go(p, s);
  events.cut = cut;
}

void step_para(ParaState& p, skier::SkierState& s, const ParaControls& in, const ParaWorld& world,
               const skier::SkiSpec& spec, double dt, ParaEvents& events) {
  namespace E = spec::engine;
  const mg::Level& level = *world.level;
  p.t += dt;
  if (p.mode == ParaMode::dropped) {
    // The kill switch let go with the throttle: the propeller runs down.
    p.rpm *= lm::exp(-dt / E::kSpinDown);
    if (p.rpm < 30) p.rpm = 0;
    spin(p, dt);
    fall_piece(level, p.canopy, spec::wing::kMass, spec::dropped::kCanopyDrag, dt);
    fall_piece(level, p.motor, spec::kMotorMass, spec::dropped::kMotorDrag, dt);
    return;
  }
  if (s.down) {
    drop_para(p, s, ParaCut::rider, events);
    return;
  }
  const double m = pilot_mass(spec);
  // FALLEN INTO HIS WING (`acro.hpp`): falling tangled in it, the motor run down, until the press
  // cuts it away or the snow stops him (a meeting hard enough to wipe him out is the skier's own,
  // and cuts it at the next step).
  if (p.mode == ParaMode::tangled) {
    p.rpm *= lm::exp(-dt / E::kSpinDown);
    if (p.rpm < 30) p.rpm = 0;
    p.thrust = 0;
    spin(p, dt);
    step_tangled(p, s, world, m, dt);
    p.agl = num::max(0.0, s.y - level.ground_at(s.x, s.z));
    p.climb = s.vy;
    if (s.on_snow) drop_para(p, s, ParaCut::ground, events);
    return;
  }
  // THE HANDS: the controls follow them; near the map's edge aloft, the toggles turned toward its
  // middle by how deep into the band the wing is.
  ParaControls want{core::clamp(in.throttle, 0.0, 1.0), core::clamp(in.brake, 0.0, 1.0),
                    core::clamp(in.steer, -1.0, 1.0), core::clamp(in.bar, -1.0, 1.0)};
  if (p.flying) {
    const Edge e = edge_at(level, p.x, p.z, p.agl);
    const double wgt = core::clamp(e.depth * 1.5, 0.0, 1.0);
    const double back = core::clamp(1.5 * core::angle_diff(p.heading, e.home), -1.0, 1.0);
    want.steer += (back - want.steer) * wgt;
  }
  const double k = 1 - lm::exp(-dt / spec::kHands);
  ParaControls& c = p.controls;
  c.throttle += (want.throttle - c.throttle) * k;
  c.brake += (want.brake - c.brake) * k;
  c.steer += (want.steer - c.steer) * k;
  c.bar += (want.bar - c.bar) * k;

  // THE ENGINE: the rpm after the throttle; the thrust off it, the square of the rpm, falling
  // with the air through the propeller.
  p.rpm += (E::kIdle + (E::kFull - E::kIdle) * c.throttle - p.rpm) * (1 - lm::exp(-dt / E::kLag));
  spin(p, dt);
  Vec3 along;
  if (s.on_snow) {
    along = Vec3{lm::sin(s.heading), 0, lm::cos(s.heading)};
  } else {
    // Aloft the motor faces the wing's way, its thrust line tilted with his lean in the harness.
    const double tilt = E::kTilt - c.bar * spec::lean::kPitch * spec::lean::kThrust;
    along =
        Vec3{lm::sin(p.heading) * lm::cos(tilt), lm::sin(tilt), lm::cos(p.heading) * lm::cos(tilt)};
  }
  const double through = num::max(0.0, s.vx * along.x + s.vy * along.y + s.vz * along.z);
  const double r = p.rpm / E::kFull;
  p.thrust = E::kThrust * r * r * num::max(0.0, 1 - through / E::kPitchSpeed);
  const double push = p.thrust / m * dt;
  s.vx += along.x * push;
  s.vy += along.y * push;
  s.vz += along.z * push;
  s.speed = core::hypot3(s.vx, s.vy, s.vz);

  if (p.mode == ParaMode::ready) {
    // HELD OVERHEAD until the AIR meeting it from ahead is enough to fly it: a headwind lifts it
    // with him barely moving, a tailwind makes him ski faster than the wind first.
    hold_overhead(p, s);
    const ParaAir air = read_air(p, world);
    const double through_air =
        (s.vx - air.x) * lm::sin(p.heading) + (s.vz - air.z) * lm::cos(p.heading);
    if (through_air >= spec::launch::kRelease || !s.on_snow) {
      p.mode = ParaMode::flown;
      p.t = 0;
      events.launched = true;
    }
    p.agl = num::max(0.0, s.y - level.ground_at(s.x, s.z));
    p.climb = s.vy;
    return;
  }

  Body pilot = pilot_of(s);
  fly(p, pilot, world, m, dt, events);
  put_pilot(s, pilot);
  p.agl = num::max(0.0, s.y - level.ground_at(s.x, s.z));
  p.climb = s.vy;
  // THE FLIGHT, counted.
  if (!s.on_snow) {
    p.air_time += dt;
    if (!p.flying && p.air_time >= spec::launch::kFlight) {
      p.flying = true;
      p.flights++;
      events.took_off = true;
    }
  } else {
    if (p.flying) events.touched = true;
    p.flying = false;
    p.air_time = 0;
  }
  if (p.flying) hang(p, s, dt);
  // THE WING BROUGHT DOWN: onto the snow, or into a crown.
  if (p.y - spec::down::kBelly <= level.ground_at(p.x, p.z) + spec::down::kGround) {
    drop_para(p, s, ParaCut::ground, events);
  } else if (in_a_crown(world, p.x, p.y, p.z)) {
    drop_para(p, s, ParaCut::crown, events);
  }
}

uint64_t para_hash(const ParaState& p, uint64_t h) {
  const auto fold = [&h](double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    for (int b = 0; b < 64; b += 8) {
      h ^= (u >> b) & 0xffu;
      h *= 0x100000001b3ull;
    }
  };
  for (const double v : {p.x,
                         p.y,
                         p.z,
                         p.vx,
                         p.vy,
                         p.vz,
                         p.heading,
                         p.bank,
                         p.pitch,
                         p.airspeed,
                         p.alpha,
                         p.fold,
                         p.fold_side,
                         p.wind,
                         p.lift,
                         p.rough,
                         p.tension,
                         p.way.x,
                         p.way.y,
                         p.way.z,
                         p.controls.throttle,
                         p.controls.brake,
                         p.controls.steer,
                         p.controls.bar,
                         p.rpm,
                         p.thrust,
                         p.prop,
                         p.agl,
                         p.climb,
                         p.t,
                         p.air_time,
                         p.swing,
                         p.slack,
                         p.rears,
                         p.g,
                         p.hold.x,
                         p.hold.y,
                         p.hold.z,
                         p.hold_at,
                         p.canopy.x,
                         p.canopy.y,
                         p.canopy.z,
                         p.motor.x,
                         p.motor.y,
                         p.motor.z}) {
    fold(v);
  }
  fold(static_cast<double>(p.mode));
  fold(static_cast<double>(p.flights));
  fold(static_cast<double>(p.loops));
  fold(static_cast<double>(p.cut));
  fold(p.stalled ? 1 : 0);
  fold(p.flying ? 1 : 0);
  fold(p.canopy.down ? 1 : 0);
  fold(p.motor.down ? 1 : 0);
  return h;
}

}  // namespace ge::paramotor
