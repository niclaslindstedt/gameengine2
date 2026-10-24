// LAB `camera-rigs`: what each rig of the reference ladder (engine/camera/ladders.hpp) does
// through each scripted subject (engine/camera/scenarios.hpp) at a display rate. For every
// scenario: a sheet of what four lenses SEE at four moments (sketched on the CPU raster, the
// subject red, the trunks brown, the ground's grid bent over it) and charts of the eye over
// the ground, the horizon's roll and the subject's place in the chase's frame. For every scenario
// and rig: the least clearance over the ground, the worst jerk of the eye against the subject, how
// far the subject strays in the frame, and any jump of the eye that is not the scenario's own
// teleport. At --motion=0 (the player's comfort, cl_motion) every rig must hold the horizon level.
//
// ge:summary Lab: every reference rig through every scripted subject: sketches and charts
// ge:keywords lab camera rigs scenarios sketch view path chart clearance jerk framing teleport
// ge:skill camera labs
// ge:test tests/camera_scenarios_test.cpp
#include <cmath>
#include <string>
#include <vector>

#include "camera/camera.hpp"
#include "camera/ladders.hpp"
#include "camera/scenarios.hpp"
#include "camera_sketch.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/plot.hpp"
#include "lab/teleport.hpp"

namespace {

namespace gl = ge::lab;
namespace cam = ge::camera;
namespace sk = ge::lab::camera_sketch;
namespace libm = ge::core::libm;

// The rigs a scenario's sheet shows, in its columns.
const char* const kSheetRigs[] = {"eyes", "chase", "helmet", "far"};
// The fastest a lens may move against its subject, m/s (a boom's arm pulled in, a fall look
// climbing): an eye moving faster than the subject's speed and this is a jump.
constexpr double kLensMps = 32;

struct Track {
  std::vector<double> t, eye_x, eye_y, eye_z, over, roll, pitch, fov, place;
  std::vector<cam::Pose> poses;
};

// Where the subject's aim point stands in the frame vertically, -1 bottom .. 1 top.
double place_in_frame(const cam::Pose& p, const cam::Subject& s) {
  const double dx = s.pos.x - p.eye.x, dz = s.pos.z - p.eye.z, dy = s.pos.y + 1.0 - p.eye.y;
  const double to = libm::atan2(dy, ge::core::hypot(dx, dz));
  return (to - p.pitch) / (p.fov_y / 2);
}

void run_scenario(gl::Lab& lab, const cam::Scenario& sc, double hz, double motion,
                  std::vector<std::vector<std::string>>& rows) {
  const auto ladder = cam::reference_ladder();
  const int rigs = static_cast<int>(ladder.size());
  const cam::World w = cam::scenario_world(sc);
  const int frames = static_cast<int>(sc.seconds * hz);
  std::vector<Track> tracks(rigs);
  std::vector<cam::Subject> subjects;
  // One camera a rig, each from its own rung, all through the same script.
  std::vector<cam::Camera> cams;
  for (int i = 0; i < rigs; ++i) {
    cams.push_back(cam::make_camera(ladder.data(), rigs, 1));
    cams.back().rung = i;
    cams.back().settings.motion = motion;
  }
  cam::ScriptRun run = cam::start_script(sc);
  int teleports = 0;
  for (int f = 1; f <= frames; ++f) {
    const double t = f / hz;
    const cam::Subject s = cam::advance_to(run, t);
    for (int e = 0; e < s.event_count; ++e) teleports += s.events[e].kind == cam::Moment::teleport;
    subjects.push_back(s);
    for (int i = 0; i < rigs; ++i) {
      const cam::Pose p = cam::update(cams[i], s, w, 1 / hz);
      Track& tr = tracks[i];
      tr.t.push_back(t);
      tr.eye_x.push_back(p.eye.x);
      tr.eye_y.push_back(p.eye.y);
      tr.eye_z.push_back(p.eye.z);
      tr.over.push_back(p.eye.y - cam::scenario_ground(sc, p.eye.x, p.eye.z));
      tr.roll.push_back(p.roll * 180 / ge::core::kPi);
      tr.pitch.push_back(p.pitch * 180 / ge::core::kPi);
      tr.fov.push_back(p.fov_y * 180 / ge::core::kPi);
      tr.place.push_back(place_in_frame(p, s));
      tr.poses.push_back(p);
    }
  }
  const std::string key(sc.id);
  for (int i = 0; i < rigs; ++i) {
    const Track& tr = tracks[i];
    double least = 1e9, worst_jerk = 0, stray = 0;
    std::vector<gl::TrackSample> eye;
    for (size_t k = 0; k < tr.t.size(); ++k) {
      least = tr.over[k] < least ? tr.over[k] : least;
      stray = std::fabs(tr.place[k]) > stray ? std::fabs(tr.place[k]) : stray;
      const cam::Subject& s = subjects[k];
      eye.push_back({tr.t[k], tr.eye_x[k], tr.eye_y[k], tr.eye_z[k],
                     ge::core::hypot3(s.vel.x, s.vel.y, s.vel.z) + kLensMps});
      if (k >= 2 && subjects[k].event_count == 0 && subjects[k - 1].event_count == 0) {
        auto rel = [&](size_t j, int axis) {
          const cam::Subject& sj = subjects[j];
          return axis == 0   ? tr.eye_x[j] - sj.pos.x
                 : axis == 1 ? tr.eye_y[j] - sj.pos.y
                             : tr.eye_z[j] - sj.pos.z;
        };
        double a2 = 0;
        for (int axis = 0; axis < 3; ++axis) {
          const double a = (rel(k, axis) - 2 * rel(k - 1, axis) + rel(k - 2, axis)) * hz * hz;
          a2 += a * a;
        }
        const double acc = std::sqrt(a2);
        worst_jerk = acc > worst_jerk ? acc : worst_jerk;
      }
    }
    const std::string rk = key + "." + std::string(ladder[i].name);
    lab.metric(rk + ".least-clearance", least, "m");
    lab.metric(rk + ".worst-accel", worst_jerk, "m/s2");
    lab.metric(rk + ".stray", stray, "half-fov");
    const auto jumps = gl::find_teleports(eye, 1.5, 0.2);
    lab.metric(rk + ".jumps", static_cast<double>(jumps.size()), "count");
    rows.push_back({key, std::string(ladder[i].name), sk::num(least), sk::num(worst_jerk, 1),
                    sk::num(stray), std::to_string(jumps.size())});
    if (static_cast<int>(jumps.size()) > teleports) {
      gl::report_teleports(lab, rk, jumps);
      lab.fail(key + " through " + std::string(ladder[i].name) + ": the eye jumped " +
                   std::to_string(jumps.size()) + " times, the script teleported " +
                   std::to_string(teleports),
               "Run tests/camera_scenarios_test.cpp and read the rig's frame in engine/camera/ "
               "(skill: camera).");
    }
    if (motion < 1) {
      // Comfort turned down: every roll a rig lays on the horizon is uncommanded motion.
      double most = 0;
      for (const double r : tr.roll) most = std::fabs(r) > most ? std::fabs(r) : most;
      lab.metric(rk + ".most-roll", most, "deg");
      if (motion == 0 && most > 1e-9) {
        lab.fail(key + " through " + std::string(ladder[i].name) + ": the horizon rolled " +
                     sk::num(most, 3) + " deg at comfort 0",
                 "Read where the rig scales its motion by `motion` in engine/camera/ (skill: "
                 "camera).");
      }
    }
    if (ladder[i].kind == cam::RigKind::boom && stray > ladder[i].boom.frame + 0.02) {
      lab.fail(key + " through " + std::string(ladder[i].name) + ": the subject strayed to " +
                   sk::num(stray) + " of the half-fov, past the rig's frame",
               "Read the composition in engine/camera/boom.cpp, then tests/camera_boom_test.cpp "
               "(skill: camera).");
    }
  }

  // The sheet: four lenses at four moments.
  const int cw = 240, ch = 135;
  std::vector<gl::Picture> cells;
  for (const double share : {0.2, 0.45, 0.7, 0.95}) {
    const size_t k = static_cast<size_t>(share * (frames - 1));
    for (const char* name : kSheetRigs) {
      int r = 0;
      for (int i = 0; i < rigs; ++i) {
        if (ladder[i].name == name) r = i;
      }
      const cam::Pose& p = tracks[r].poses[k];
      cells.push_back(sk::sketch_view(p, sc, subjects[k], cw, ch,
                                      std::string(name) + " T " + sk::num(tracks[r].t[k], 1) +
                                          " FOV " + sk::num(tracks[r].fov[k], 0) + " ROLL " +
                                          sk::num(tracks[r].roll[k], 1)));
    }
  }
  lab.shot(
      {key + "-lenses",
       "WHAT FOUR LENSES SEE: " + key,
       std::string(sc.what) +
           ". Columns: the eyes (first person), the chase boom, the "
           "helmet (mounted) and the far boom; rows: four moments of the run. The subject is "
           "red, the ground's grid grey (bent over the ground), trunks brown, the horizon blue.",
       "the subject in every boom frame, a little under the middle; the eyes' horizon "
       "tipping only on a strafe or a hit; the helmet's with the body's bank",
       {{"scenario", key}, {"hz", sk::num(hz, 0)}}},
      sk::sheet(cells, 4, cw, ch));

  // Charts.
  auto chart = [&](const std::string& id, const std::string& title, const std::string& y,
                   const std::vector<const char*>& names, std::vector<double> Track::*field,
                   const std::string& caption, const std::string& look_for) {
    gl::LineChart lc;
    lc.frame = {title, std::string(sc.what), "t (s)", y};
    lc.spikes = false;
    for (const char* name : names) {
      for (int i = 0; i < rigs; ++i) {
        if (ladder[i].name == name) lc.traces.push_back({name, tracks[i].t, tracks[i].*field});
      }
    }
    const gl::Chart c = gl::line_chart(lc);
    lab.shot({key + "-" + id, c.title, caption, look_for, {{"scenario", key}}},
             gl::to_picture(c.plot));
  };
  chart("over", "EYE OVER THE GROUND: " + key, "m", {"eyes", "chase", "far", "helmet"},
        &Track::over,
        "The height of each lens over the ground under it. The eyes ride at their view height "
        "with the stride's bob; a boom's is sprung.",
        "no line under its rig's clearance; the eyes ramp up a stair, never jump");
  chart("roll", "HORIZON ROLL: " + key, "deg", {"eyes", "chase", "helmet", "tips"}, &Track::roll,
        "The roll each lens lays on the horizon, right side down positive.",
        "the eyes roll on a strafe and a hit only; the helmet and the tips with the bank; the "
        "chase a little");
  chart("place", "SUBJECT IN THE BOOMS' FRAME: " + key, "half-fov", {"chase", "far", "high"},
        &Track::place,
        "Where the subject's chest stands in each boom's frame: 0 the middle, -1 the bottom edge.",
        "every line between -0.66 and 0.66 (the rows' frame), mostly a little under 0");
  chart("fov", "FIELD OF VIEW: " + key, "deg (vertical)", {"chase", "far", "high", "helmet"},
        &Track::fov, "Each lens's vertical field of view: the booms widen with speed.",
        "the chase widest at speed, the high one barely moving");
}

void run(gl::Lab& lab) {
  const double hz = lab.arg_num("hz");
  const double motion = lab.arg_num("motion");
  std::vector<std::vector<std::string>> rows;
  const std::string want = lab.arg("scenarios");
  for (const cam::Scenario& sc : cam::camera_scenarios()) {
    if (want != "all" &&
        ("," + want + ",").find("," + std::string(sc.id) + ",") == std::string::npos) {
      continue;
    }
    run_scenario(lab, sc, hz, motion, rows);
  }
  lab.table(
      {"every rig through every scenario at " + sk::num(hz, 0) + " Hz",
       {"scenario", "rig", "least-clearance-m", "worst-accel-m/s2", "stray-half-fov", "jumps"},
       rows});
  lab.note(
      "worst-accel is the eye's acceleration against the subject's own, m/s^2, frames with a "
      "moment left out: the number a judder or a snap shows in. stray is how far the subject's "
      "chest strays from the middle of the frame, in half-fovs (a boom holds it inside its row's "
      "`frame`). A worn rig's clearance is its eye height.");
}

const gl::LabSpec kSpec{
    "camera-rigs",
    gl::LabKind::scenario,
    "engine",
    "engine/camera/ (the rigs and the stack, through engine/camera/scenarios.hpp)",
    "camera",
    "What does each camera rig do, and see, through each scripted subject?",
    {
        {"scenarios", "all", "comma-separated scenario ids (camera_scenarios), or all"},
        {"hz", "60", "the display rate the cameras run at"},
        {"motion", "1",
         "comfort (cl_motion), 0 .. 1; under 1 each rig's most roll is measured, and at 0 any "
         "roll fails"},
    },
    "--scenarios=stairs,cliff",
    run,
};
GE_LAB(kSpec);

}  // namespace
