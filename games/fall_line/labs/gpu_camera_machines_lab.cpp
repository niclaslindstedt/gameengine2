// LAB `camera-machines`: what each rung of the free ride's machines' ladders
// (games/fall_line/camera/machines.hpp) SHOWS, and how the picture is handed over as he boards a
// machine and leaves it. The bot takes each outing on seed --seed's map (the snowmobile up a face,
// the helicopter to a drop, the paramotor off the summit: the sim lab's outings) and four ride
// cameras follow him at 60 frames a second, one put on each rung of the machine's ladder through
// its skis' counterpart (helmet, chase, far, high), so every one makes the hand-over on and off as
// the game makes it.
//
// Every frame of every camera is checked against the mountain: how far the eye is over the snow
// under it and how near it comes to the bark of a trunk. A lens inside a trunk or under the snow
// fails. Through every hand-over the lens's largest step between two frames past his own is
// measured: a hand-over is a move, never a jump.
//
// THE MOMENTS, picked off each outing:
//   sled  BOARD (half a second on), CLIMB (the steepest pitch up on the snow), AIR (the most air
//         under it), OFF (the last frame before he hops off)
//   heli  LIFT-OFF (a second after the skids leave the snow), CRUISE (the most speed), BANK (the
//         most roll), DROP (the last frame on the skid)
//   para  LAUNCH (half a second after his skis leave the snow), TURN (the most bank), HIGH (the
//         most air under him), LANDING (back on the snow)
// Each is a sheet of the ladder's four pictures; each hand-over (on and off) a strip of six
// frames through it on the worn rung and the chase. The machines are not drawn in the game's
// frame yet: the pictures show him, his line and the mountain round him. Without an adapter the
// pictures are placeholders and every number above is still measured, so the digest is the same
// on every row. Built only with GE_GPU.
//
// ge:summary Lab: the machines' lenses at an outing's moments, the hand-overs, no lens in a trunk
// ge:keywords lab camera camera-machines machine sled snowmobile heli helicopter para paramotor
// ge:keywords hand-over board hop drop seat chase far high down wing pilot contact-sheet
// ge:skill camera labs
// ge:test tests/fall_line_camera_test.cpp
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "camera/to_view.hpp"
#include "core/math.hpp"
#include "fall_line/app/frame.hpp"
#include "fall_line/app/views.hpp"
#include "fall_line/camera/machines.hpp"
#include "fall_line/camera/world.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/heli_outing.hpp"
#include "fall_line/sim/para_outing.hpp"
#include "fall_line/sim/para_ride.hpp"
#include "fall_line/sim/simulate.hpp"
#include "fall_line/sim/sled_outing.hpp"
#include "gpu/device.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "skier/trees.hpp"

namespace {

namespace gl = ge::lab;
namespace cam = ge::camera;
namespace fc = fall_line::camera;
namespace sim = fall_line::sim;
using gl::Det;

constexpr double kHz = 60;
constexpr int kLenses = fc::kMachineRungs;
constexpr int kStrip = 6;
constexpr double kStripEvery = 0.2;  // s between a strip's frames
constexpr double kSnowFloor = 0.05;  // m: a lens nearer the snow than a near plane is in it
constexpr double kAfter = 3;         // s followed after he leaves the machine
constexpr double kJumpM = 1.5;       // m: a lens step against him in one frame that is a jump

// One display frame of an outing: what the pictures and the moments read.
struct Frame {
  double t = 0;
  ge::skier::SkierState skier;
  fc::Ride ride = fc::Ride::skis;
  double over = 0;     // the machine's (or his) height over the snow, m
  double speed = 0;    // m/s
  double pitch = 0;    // rad, nose up
  double roll = 0;     // rad: the machine's roll, the wing's bank
  bool ground = true;  // the machine (or he) on the snow
  std::array<cam::Pose, kLenses> lens{};
};

bool env_on(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool on = _dupenv_s(&v, &n, name) == 0 && v && v[0] == '1';
  std::free(v);
  return on;
#else
  const char* v = std::getenv(name);
  return v && v[0] == '1';
#endif
}

std::string fixed(double v, int places) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

double bark_gap(const ge::skier::World& w, const cam::Vec3& p) {
  double gap = 1e9;
  ge::skier::for_each_tree_near(*w.trees, p.x, p.z, [&](int32_t i) {
    const auto& t = w.level->trees[static_cast<size_t>(i)];
    if (p.y < t.y || p.y > t.y + t.height) return;
    const double d = std::sqrt((p.x - t.x) * (p.x - t.x) + (p.z - t.z) * (p.z - t.z)) - t.radius;
    gap = d < gap ? d : gap;
  });
  return gap;
}

struct Worst {
  double gap = 1e9, over = 1e9, jump = 0;
};

// What a machine's outing is: its ride, the bot that takes it and how it reads off the state.
struct Outing {
  fc::Ride ride;
  std::vector<Frame> frames;
  std::array<Worst, kLenses> worst{};
  std::vector<size_t> changes;  // the frames the ride changed on
};

void read_machine(Frame& f, const sim::GameState& s, fc::Ride r) {
  const auto& sk = s.racers[0].skier;
  switch (r) {
    case fc::Ride::sled: {
      const auto& b = s.sled.body;
      f.over = b.y - s.level->ground_at(b.x, b.z);
      f.speed = b.speed;
      f.pitch = b.pitch;
      f.roll = b.roll;
      f.ground = !b.airborne;
      return;
    }
    case fc::Ride::heli: {
      const auto& b = s.heli.body;
      f.over = b.y - s.level->ground_at(b.x, b.z);
      f.speed = std::sqrt(b.vx * b.vx + b.vy * b.vy + b.vz * b.vz);
      f.pitch = b.pitch;
      f.roll = b.roll;
      f.ground = b.grounded;
      return;
    }
    default:
      f.over = sk.y - s.level->ground_at(sk.x, sk.z);
      f.speed = sk.speed;
      f.roll = s.para.present ? s.para.body.bank : 0.0;
      f.ground = sk.on_snow;
      return;
  }
}

// Drive one outing to `seconds`, or until he has been off the machine `kAfter` s.
template <class Pilot>
void follow(Outing& o, sim::Driver& drive, const Pilot& pilot, const ge::skier::World& sw,
            const cam::World& world, double seconds) {
  std::array<fc::RideCamera, kLenses> cams;
  for (int i = 0; i < kLenses; ++i) {
    cams[static_cast<size_t>(i)] = fc::make_ride_camera(fc::skis_rung_of(i), 7);
  }
  fc::Ride last = fc::ride_of(drive.state());
  double left_at = -1;
  std::array<cam::Vec3, kLenses> prev{};
  cam::Vec3 prev_him{};
  while (!drive.over() && drive.state().t < seconds) {
    // The first frame is before the first step: every camera framed on him as he stands.
    if (!o.frames.empty()) {
      for (int k = 0; k < 2 && !drive.over(); ++k) drive.step(pilot(drive.state()));
    }
    const sim::GameState& s = drive.state();
    Frame f;
    f.t = s.t;
    f.skier = s.racers[0].skier;
    f.ride = fc::ride_of(s);
    read_machine(f, s, o.ride);
    if (f.ride != last && !o.frames.empty()) o.changes.push_back(o.frames.size());
    if (last == o.ride && f.ride != o.ride) left_at = f.t;
    last = f.ride;
    const cam::Vec3 him{f.skier.x, f.skier.y, f.skier.z};
    bool handing = false;
    for (size_t i = 0; i < cams.size(); ++i) {
      f.lens[i] = fc::frame_ride(cams[i], s, sw, world, 1 / kHz);
      handing = handing || fc::handing_over(cams[i]);
      Worst& w = o.worst[i];
      const double gap = bark_gap(sw, f.lens[i].eye);
      const double over = f.lens[i].eye.y - sw.level->ground_at(f.lens[i].eye.x, f.lens[i].eye.z);
      w.gap = std::min(w.gap, gap);
      w.over = std::min(w.over, over);
      if (fc::handing_over(cams[i]) && !o.frames.empty()) {
        // The lens's step past his own (boarding puts his feet on the boards in one step).
        const cam::Vec3& e = f.lens[i].eye;
        const double eye = ge::core::hypot3(e.x - prev[i].x, e.y - prev[i].y, e.z - prev[i].z);
        const double moved =
            ge::core::hypot3(him.x - prev_him.x, him.y - prev_him.y, him.z - prev_him.z);
        w.jump = std::max(w.jump, eye - moved);
      }
      prev[i] = f.lens[i].eye;
    }
    prev_him = him;
    o.frames.push_back(f);
    if (left_at >= 0 && f.t - left_at > kAfter && !handing) break;
  }
}

// A moment to shoot: its name, what it is, and the frame.
struct Moment {
  std::string id, what;
  size_t frame;
};

// The frame of `o` on the machine where `score` is largest (none: -1).
long best_on(const Outing& o, const std::function<double(const Frame&)>& score) {
  long best = -1;
  double most = -1e300;
  for (size_t i = 0; i < o.frames.size(); ++i) {
    if (o.frames[i].ride != o.ride) continue;
    const double v = score(o.frames[i]);
    if (v > most) {
      most = v;
      best = static_cast<long>(i);
    }
  }
  return best;
}

// The first frame on the machine (from `from`) where `pred` holds, `delay` s on; -1: none.
long first_on(const Outing& o, size_t from, const std::function<bool(const Frame&)>& pred,
              double delay) {
  for (size_t i = from; i < o.frames.size(); ++i) {
    if (o.frames[i].ride != o.ride || !pred(o.frames[i])) continue;
    size_t j = i;
    while (j + 1 < o.frames.size() && o.frames[j].t < o.frames[i].t + delay) ++j;
    return static_cast<long>(j);
  }
  return -1;
}

long last_on(const Outing& o) {
  for (size_t i = o.frames.size(); i-- > 0;) {
    if (o.frames[i].ride == o.ride) return static_cast<long>(i);
  }
  return -1;
}

std::vector<Moment> moments_of(gl::Lab& lab, const Outing& o) {
  std::vector<Moment> m;
  auto add = [&](const char* id, const std::string& what, long at) {
    if (at < 0) {
      lab.note(std::string(fc::ride_name(o.ride)) + ": no " + id + " in this outing.");
      return;
    }
    m.push_back({id, what, static_cast<size_t>(at)});
  };
  switch (o.ride) {
    case fc::Ride::sled: {
      add("board", "half a second after he stands on its boards",
          first_on(o, 0, [](const Frame&) { return true; }, 0.5));
      const long climb = best_on(o, [](const Frame& f) { return f.ground ? f.pitch : -1e9; });
      add("climb",
          "the steepest pitch up it climbs on the snow" +
              (climb >= 0
                   ? ", " + fixed(o.frames[static_cast<size_t>(climb)].pitch * 57.3, 0) + " degrees"
                   : std::string()),
          climb);
      const long air = best_on(o, [](const Frame& f) { return f.ground ? -1e9 : f.over; });
      if (air >= 0 && !o.frames[static_cast<size_t>(air)].ground) {
        add("air",
            "the most air under it, " + fixed(o.frames[static_cast<size_t>(air)].over, 1) + " m",
            air);
      } else {
        lab.note("sled: no air in this outing.");
      }
      add("off", "the last frame before he hops off", last_on(o));
      break;
    }
    case fc::Ride::heli: {
      add("lift-off", "a second after its skids leave the snow",
          first_on(o, 0, [](const Frame& f) { return !f.ground; }, 1.0));
      const long fast = best_on(o, [](const Frame& f) { return f.speed; });
      add("cruise",
          "its most speed" +
              (fast >= 0 ? ", " + fixed(o.frames[static_cast<size_t>(fast)].speed, 0) + " m/s"
                         : std::string()),
          fast);
      const long bank =
          best_on(o, [](const Frame& f) { return f.ground ? -1 : std::fabs(f.roll); });
      add("bank",
          "its most roll" +
              (bank >= 0
                   ? ", " + fixed(o.frames[static_cast<size_t>(bank)].roll * 57.3, 0) + " degrees"
                   : std::string()),
          bank);
      add("drop", "the last frame on the skid, before he pushes off", last_on(o));
      break;
    }
    default: {
      // The take-off into the flight: off the snow and staying off for `kFlight` s (a hop under
      // the wing on the run-up is not it).
      constexpr double kFlight = 3;
      long launch = -1;
      for (size_t i = 0; i < o.frames.size() && launch < 0; ++i) {
        size_t j = i;
        while (j < o.frames.size() && !o.frames[j].ground && o.frames[j].ride == o.ride &&
               o.frames[j].t < o.frames[i].t + kFlight) {
          ++j;
        }
        if (j > i && j < o.frames.size() && o.frames[j].t >= o.frames[i].t + kFlight) {
          launch = static_cast<long>(i);
        }
      }
      if (launch >= 0)
        launch = first_on(o, static_cast<size_t>(launch), [](const Frame&) { return true; }, 0.5);
      add("launch", "half a second after his skis leave the snow for the flight", launch);
      const long turn =
          best_on(o, [](const Frame& f) { return f.ground ? -1 : std::fabs(f.roll); });
      add("turn",
          "the wing's most bank" +
              (turn >= 0
                   ? ", " + fixed(o.frames[static_cast<size_t>(turn)].roll * 57.3, 0) + " degrees"
                   : std::string()),
          turn);
      const long high = best_on(o, [](const Frame& f) { return f.over; });
      add("high",
          "the most air under him" +
              (high >= 0 ? ", " + fixed(o.frames[static_cast<size_t>(high)].over, 0) + " m"
                         : std::string()),
          high);
      long land = high < 0 ? -1
                           : first_on(
                                 o, static_cast<size_t>(high),
                                 [](const Frame& f) { return f.ground; }, 0.3);
      if (land < 0) land = last_on(o);
      add("landing", "back on the snow under the wing (or the last frame under it)", land);
      break;
    }
  }
  std::stable_sort(m.begin(), m.end(),
                   [](const Moment& a, const Moment& b) { return a.frame < b.frame; });
  return m;
}

void run(gl::Lab& lab) {
  const double seed = lab.arg_num("seed");
  const double hour = lab.arg_num("hour");
  const double seconds = lab.arg_num("seconds");
  const int width = static_cast<int>(lab.arg_num("width"));
  const int height = static_cast<int>(lab.arg_num("height"));
  const int settle = static_cast<int>(lab.arg_num("settle"));
  const bool see_through = lab.arg_num("see-through") != 0;
  if (width < 32 || height < 18 || width > 1920 || height > 1080 || settle < 1) {
    lab.fail("--width and --height must be 32..1920 and 18..1080, --settle at least 1",
             "next: pass a size such as --width=320 --height=180.");
    return;
  }
  // The free ride's map: the machines wait in its ski area.
  const auto shared = std::make_shared<const ge::mountain::mapgen::Level>(
      sim::free_map(seed, static_cast<int>(lab.arg_num("version"))));
  const ge::mountain::mapgen::Level& level = *shared;

  // THE OUTINGS, each followed by its four cameras.
  std::vector<Outing> outings;
  {
    sim::GameState state = sim::create_free_ride(shared, 0);
    sim::stand_by_sled(state, 0);
    const sim::SledSpot spot{state.sled.body.x, state.sled.body.z, state.sled.body.heading};
    const int32_t climb = sim::sled_climb_run(level, spot);
    if (climb < 0) {
      lab.note("sled: this map has no run for the snowmobile to climb.");
    } else {
      const sim::SledOutingPilot pilot(state, climb);
      sim::Driver drive(std::move(state), 0, 1);
      const ge::skier::World sw = drive.state().world();
      Outing o{fc::Ride::sled, {}, {}, {}};
      follow(
          o, drive, [&](const sim::GameState& s) { return pilot(s, 0); }, sw, fc::camera_world(sw),
          seconds);
      outings.push_back(std::move(o));
    }
  }
  {
    sim::GameState state = sim::create_free_ride(shared, 0);
    sim::stand_by_heli(state, 0);
    const int32_t drop = sim::heli_drop_run(level);
    if (drop < 0) {
      lab.note("heli: this map has no run to drop him on.");
    } else {
      const sim::HeliOutingPilot pilot(state, drop);
      sim::Driver drive(std::move(state), 0, 0, 1);
      const ge::skier::World sw = drive.state().world();
      Outing o{fc::Ride::heli, {}, {}, {}};
      follow(
          o, drive, [&](const sim::GameState& s) { return pilot(s, 0); }, sw, fc::camera_world(sw),
          seconds);
      outings.push_back(std::move(o));
    }
  }
  {
    sim::GameState state = sim::create_para_ride(shared);
    const sim::ParaOutingPilot pilot(state);
    sim::Driver drive(std::move(state));
    const ge::skier::World sw = drive.state().world();
    Outing o{fc::Ride::para, {}, {}, {}};
    follow(
        o, drive, [&](const sim::GameState& s) { return pilot(s, 0); }, sw, fc::camera_world(sw),
        seconds);
    outings.push_back(std::move(o));
  }
  const ge::skier::World sw = sim::create_free_ride(shared, 0).world();

  // Every lens's worst over each outing.
  std::vector<std::vector<std::string>> rows;
  for (const Outing& o : outings) {
    const std::string ride(fc::ride_name(o.ride));
    lab.metric(ride + ".frames", static_cast<double>(o.frames.size()), "count");
    lab.metric(ride + ".changes", static_cast<double>(o.changes.size()), "count");
    if (o.frames.empty() || last_on(o) < 0) {
      lab.fail(ride + ": the bot never took the machine",
               "Read the outing's pilot in games/fall_line/sim (skill: sim), or try another seed.");
      continue;
    }
    for (int i = 0; i < kLenses; ++i) {
      const Worst& w = o.worst[static_cast<size_t>(i)];
      const std::string id = ride + "." + std::string(fc::lens_name(o.ride, i));
      lab.metric(id + ".least-over-snow", w.over, "m");
      lab.metric(id + ".least-bark-gap", w.gap > 1e8 ? -1 : w.gap, "m");
      lab.metric(id + ".hand-over-step", w.jump, "m");
      rows.push_back(
          {id, fixed(w.over, 2), w.gap > 1e8 ? "none near" : fixed(w.gap, 2), fixed(w.jump, 2)});
      if (w.gap < 0) {
        lab.fail(id + ": a lens stood " + fixed(-w.gap, 2) + " m inside a trunk",
                 "Read the push-off in engine/camera/boom.cpp and settle_lens in "
                 "games/fall_line/camera/subject.cpp (skill: camera).");
      }
      if (w.over < kSnowFloor) {
        lab.fail(id + ": a lens came " + fixed(w.over, 2) + " m over the snow (under it or in it)",
                 "Read the clearance in engine/camera/boom.cpp and settle_lens in "
                 "games/fall_line/camera/subject.cpp (skill: camera).");
      }
      if (w.jump > kJumpM) {
        lab.fail(id + ": a hand-over moved the lens " + fixed(w.jump, 2) +
                     " m in one frame past his own move",
                 "Read the hand-over in games/fall_line/camera/machines.cpp (skill: camera).");
      }
    }
  }
  lab.table(
      {"every lens's worst frame over its outing (the step: the most a hand-over moved the "
       "lens in one frame past his own move)",
       {"lens", "least-over-snow-m", "least-bark-gap-m", "hand-over-step-m"},
       rows});

  // THE SHOTS, in a fixed order: per outing, its moments' sheets, then its hand-over strips.
  struct Strip {
    size_t frame;  // the change's frame
    std::string id, what;
  };
  const std::string seed_s = std::to_string(static_cast<long long>(seed));
  auto sheet_shot = [&](const Outing& o, const Moment& m) {
    const std::string ride(fc::ride_name(o.ride));
    const Frame& f = o.frames[m.frame];
    std::string lenses;
    for (int i = 0; i < kLenses; ++i) {
      lenses += std::string(i ? (i == kLenses - 1 ? " and " : ", ") : "") +
                std::string(fc::lens_name(o.ride, i));
    }
    return gl::Shot{ride + "-" + m.id,
                    "SEED " + seed_s + ": THE " + upper(ride) + "'S LENSES AT " + upper(m.id),
                    "The " + ride + " outing " + fixed(f.t, 1) + " s in, " + m.what +
                        ", seen from each rung of its ladder (" + lenses +
                        "), each camera following him all outing. The machine itself is not drawn "
                        "yet: he is.",
                    "him in every outside lens, placed a little under the middle of the frame; "
                    "the worn lens at his eyes with the horizon rolled by the machine; no picture "
                    "of bark, the inside of a crown or the underside of the snow",
                    {{"seed", seed_s}, {"ride", ride}, {"moment", m.id}}};
  };
  auto strip_shot = [&](const Outing& o, const Strip& s) {
    const std::string ride(fc::ride_name(o.ride));
    return gl::Shot{ride + "-" + s.id,
                    upper(ride) + " " + upper(s.id),
                    "The " + ride + " outing as he " + s.what + " at " +
                        fixed(o.frames[s.frame].t, 1) + " s: from the frame before it, every " +
                        fixed(kStripEvery, 1) + " s through the " + fixed(fc::kRideHandOverS, 1) +
                        " s hand-over, the worn lens on the top row and the chase under it.",
                    "a move, never a jump between two frames; him in every chase frame",
                    {{"seed", seed_s}, {"ride", ride}, {"edit", s.id}}};
  };
  std::vector<std::vector<Moment>> moments;
  std::vector<std::vector<Strip>> strips;
  for (const Outing& o : outings) {
    moments.push_back(o.frames.empty() ? std::vector<Moment>{} : moments_of(lab, o));
    std::vector<Strip> st;
    for (size_t c : o.changes) {
      const bool on = o.frames[c].ride == o.ride;
      st.push_back({c, on ? "hand-over-on" : "hand-over-off", on ? "boards it" : "leaves it"});
    }
    strips.push_back(st);
    for (const Moment& m : moments.back()) {
      lab.metric(std::string(fc::ride_name(o.ride)) + "." + m.id + ".t", o.frames[m.frame].t, "s");
    }
  }

  auto device = ge::gpu::create_device({.label = "camera-machines", .validation = 1});
  if (!device) {
    if (env_on("GE_GPU_REQUIRE")) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    lab.note("No GPU adapter on this machine: the pictures are placeholders.");
    gl::Picture none = gl::create_picture(width, height, {40, 40, 48});
    gl::draw_text(none, 4, 4, "NO GPU ADAPTER", {230, 230, 230});
    size_t shots = 0;
    for (size_t k = 0; k < outings.size(); ++k) {
      for (const Moment& m : moments[k]) lab.shot(sheet_shot(outings[k], m), none, Det::measured);
      for (const Strip& s : strips[k]) lab.shot(strip_shot(outings[k], s), none, Det::measured);
      shots += moments[k].size() + strips[k].size();
    }
    lab.metric("shots", static_cast<double>(shots), "count");
    lab.metric("gpu-errors", 0, "count", Det::measured);
    return;
  }
  fall_line::app::FrameOptions options;
  auto frame = fall_line::app::create_frame(*device, level, static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            wgpu::TextureFormat::RGBA8Unorm, options);
  if (!frame) {
    lab.fail("Fall Line's frame did not build", "Read the gpu: lines above. Skill: render.");
    return;
  }
  const ge::gpu::Target target =
      ge::gpu::create_target(*device, "camera-machines.final", static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height));
  auto picture = [&](const Frame& f, const cam::Pose& pose) {
    ge::render::FrameView v;
    fall_line::app::day_view(level, hour, v);
    cam::to_frame_view(pose, v);
    v.time_s = f.t;
    fall_line::app::cut(*frame);
    fall_line::app::FrameScene scene{std::span<const ge::skier::SkierState>(&f.skier, 1)};
    if (see_through) {
      const cam::Vec3 at = fc::framed_point(f.skier);
      scene.subject = std::array<double, 3>{at.x, at.y, at.z};
    }
    for (int i = 0; i < settle; ++i) {
      v.frame = static_cast<uint64_t>(i);
      fall_line::app::draw(*device, *frame, v, scene, target.view);
    }
    const ge::gpu::Image img = ge::gpu::read_now(*device, target);
    if (img.rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
      return gl::create_picture(width, height, {255, 0, 255});
    }
    return gl::Picture{width, height, img.rgba};
  };
  const int label_h = 14;
  auto labelled = [&](gl::Picture& sheet, const gl::Picture& p, int col, int row,
                      const std::string& label) {
    const int x = col * width, y = row * (height + label_h);
    gl::blit(sheet, p, x, y + label_h);
    gl::draw_text(sheet, x + 4, y + 3, label, {235, 235, 235});
  };
  size_t shots = 0;
  for (size_t k = 0; k < outings.size(); ++k) {
    const Outing& o = outings[k];
    for (const Moment& m : moments[k]) {
      gl::Picture sheet = gl::create_picture(2 * width, 2 * (height + label_h), {20, 22, 26});
      const Frame& f = o.frames[m.frame];
      for (int i = 0; i < kLenses; ++i) {
        labelled(sheet, picture(f, f.lens[static_cast<size_t>(i)]), i % 2, i / 2,
                 upper(std::string(fc::lens_name(o.ride, i))));
      }
      lab.shot(sheet_shot(o, m), sheet, Det::measured);
      ++shots;
    }
    for (const Strip& s : strips[k]) {
      gl::Picture sheet = gl::create_picture(kStrip * width, 2 * (height + label_h), {20, 22, 26});
      for (int c = 0; c < kStrip; ++c) {
        const long at = static_cast<long>(s.frame) + std::lround((c - 1) * kStripEvery * kHz);
        const size_t fi =
            static_cast<size_t>(std::clamp(at, 0L, static_cast<long>(o.frames.size()) - 1));
        const Frame& f = o.frames[fi];
        const double since = f.t - o.frames[s.frame].t;
        const std::string when = since < 0 ? std::string("BEFORE") : "+" + fixed(since, 2) + " S";
        labelled(sheet, picture(f, f.lens[0]), c, 0, when);
        labelled(sheet, picture(f, f.lens[1]), c, 1, when);
      }
      lab.shot(strip_shot(o, s), sheet, Det::measured);
      ++shots;
    }
  }
  lab.metric("shots", static_cast<double>(shots), "count");
  lab.metric("gpu-errors", device->errors.load(), "count", Det::measured);
  if (device->errors.load() != 0) {
    lab.fail(std::to_string(device->errors.load()) + " GPU errors while drawing",
             "Read the gpu: lines above (each names its object's label). Skill: render.");
  }
  (void)sw;
}

const gl::LabSpec kSpec{
    "camera-machines",
    gl::LabKind::picture,
    "game:fall_line",
    "games/fall_line/camera/machines (the machines' ladders, subjects and hand-overs) over "
    "engine/camera",
    "camera",
    "What does each rung of the snowmobile's, the helicopter's and the paramotor's ladders show "
    "at the moments of an outing, is the picture handed over on and off without a jump, and does "
    "any lens stand inside a trunk or the snow?",
    {
        {"seed", "38", "the map's seed"},
        {"version", "10", "the generator version (the ski area's)"},
        {"hour", "-1", "the solar hour to light the map at; negative: the map's own"},
        {"seconds", "240", "the most of each outing to follow, s"},
        {"width", "320", "each lens's picture's width, px"},
        {"height", "180", "each lens's picture's height, px"},
        {"settle", "6", "frames drawn after each picture's cut before the capture"},
        {"see-through", "1", "the trees between a lens and him seen through; 0: drawn whole"},
    },
    "--seconds=60 --width=64 --height=36 --settle=1",
    run,
};
GE_LAB(kSpec);

}  // namespace
