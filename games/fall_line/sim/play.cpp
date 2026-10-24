// Fall Line's Run on the play contract (play.hpp): the Driver behind it, the observation, the
// state, the report, the bot as the pilot and the views `ge shot` draws: the map from above with
// the field on it, and the piste round the player.
//
// ge:summary FallLineRun: the Driver behind the play contract; observation, state, report, pilot
// ge:keywords play run observation state report pilot driver events shoot views map near
// ge:skill sim agent-interface
// ge:test tests/fall_line_play_test.cpp
#include "fall_line/sim/play.hpp"

#include "fall_line/cfg/cvars.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num_text.hpp"
#include "fall_line/sim/bot.hpp"
#include "fall_line/sim/course.hpp"
#include "fall_line/sim/field.hpp"
#include "fall_line/sim/game.hpp"
#include "fall_line/sim/simulate.hpp"
#include "mountain/mapgen/generate.hpp"
#include "mountain/mapgen/query.hpp"
#include "mountain/picture/map_picture.hpp"
#include "play/inputs.hpp"
#include "play/session.hpp"

namespace fall_line::sim {

namespace {

namespace mg = ge::mountain::mapgen;
namespace pic = ge::mountain::picture;
namespace core = ge::core;
namespace num = ge::core::num;
using ge::play::quote;

// A still's side in pixels, and the near view's side in metres.
constexpr int kShotPx = 512;
constexpr double kNearMetres = 160;

std::string fixed(double v) { return num::to_fixed(v, 3); }
std::string exact(double v) { return num::to_string(v); }
std::string flag(bool b) { return b ? "true" : "false"; }

class FallLineRun final : public ge::play::Run {
 public:
  FallLineRun(std::shared_ptr<const mg::Level> level, double seed, int32_t rivals,
              HandsProfile hands)
      : driver_(std::move(level), rivals), hands_(hands, hands_seed(seed, 0)), seed_(seed) {
    driver_.set_hands(hands, seed);
    backlog_.reserve(kMostEvents);
  }

  void step(std::span<const double> in) override {
    // The pilot's hands move on only with a step taken: asking it (the `why` op) works on a copy.
    if (asked_at_ == driver_.state().steps) hands_ = asked_;
    asked_at_ = -1;
    Controls c;
    c.ski.steer = in[0];
    c.ski.tuck = in[1];
    c.ski.brake = in[2];
    c.ski.jump = in[3] != 0;
    c.reset = in[4] != 0;
    driver_.step(c);
    collect();
  }

  bool over() const override { return driver_.over(); }
  const GameState& game() const { return driver_.state(); }
  uint64_t digest() const override { return driver_.digest(); }

  std::string observe() override {
    const GameState& s = driver_.state();
    const mg::Level& level = *s.level;
    const Racer& me = s.racers[0];
    const auto& k = me.skier;
    const Progress& p = me.progress;
    std::string out = "{\"x\":" + fixed(k.x) + ",\"y\":" + fixed(k.y) + ",\"z\":" + fixed(k.z) +
                      ",\"heading\":" + fixed(k.heading) + ",\"speed\":" + fixed(k.speed) +
                      ",\"speed_kmh\":" + fixed(k.speed * 3.6) + ",\"edge\":" + fixed(k.edge) +
                      ",\"on_snow\":" + flag(k.on_snow) + ",\"air_time\":" + fixed(k.air_time) +
                      ",\"down\":" + flag(k.down);
    const auto gates = static_cast<int32_t>(level.checkpoints.size());
    out += ",\"gate\":" + std::to_string(p.next) + ",\"gates\":" + std::to_string(gates);
    if (!p.finished && p.next < gates) {
      const mg::Checkpoint& g = level.checkpoints[static_cast<size_t>(p.next)];
      const double bearing = bearing_to_next(level, p, k.x, k.z);
      out += ",\"gate_bearing\":" + fixed(core::angle_diff(k.heading, bearing)) +
             ",\"gate_distance\":" + fixed(core::hypot(g.x - k.x, g.z - k.z));
    }
    const mg::TrackHit hit = mg::nearest_track_point(level.track, k.x, k.z);
    const mg::TrackPoint here = mg::track_point_at(level.track, hit.s);
    const mg::TrackPoint ahead = mg::track_point_at(level.track, hit.s + kBendAhead);
    out += ",\"piste_lateral\":" + fixed(hit.lateral) + ",\"piste_width\":" + fixed(here.width) +
           ",\"piste_heading\":" + fixed(core::angle_diff(k.heading, here.heading)) +
           ",\"piste_bend\":" + fixed(core::angle_diff(here.heading, ahead.heading)) +
           ",\"piste_s\":" + fixed(hit.s) + ",\"piste_length\":" + fixed(level.track.length);
    out += ",\"finished\":" + flag(p.finished);
    if (p.finished) out += ",\"finish_time\":" + fixed(p.finish_time);
    out += ",\"place\":" + std::to_string(place_of(s, 0)) +
           ",\"racers\":" + std::to_string(s.racers.size()) + ",\"events\":[";
    for (size_t i = 0; i < backlog_.size(); i++) {
      const GameEvent& e = backlog_[i];
      out += std::string(i ? "," : "") + "{\"kind\":" + quote(event_name(e.kind)) +
             ",\"racer\":" + std::to_string(e.racer) + ",\"gate\":" + std::to_string(e.gate) +
             ",\"value\":" + fixed(e.value) + "}";
    }
    out += "]";
    if (dropped_ > 0) out += ",\"events_dropped\":" + std::to_string(dropped_);
    backlog_.clear();
    dropped_ = 0;
    return out + "}";
  }

  std::string state() const override {
    const GameState& s = driver_.state();
    std::string out =
        "{\"steps\":" + std::to_string(s.steps) + ",\"t\":" + exact(s.t) + ",\"racers\":[";
    for (size_t i = 0; i < s.racers.size(); i++) {
      const Racer& r = s.racers[i];
      const auto& k = r.skier;
      const Progress& p = r.progress;
      out += std::string(i ? "," : "") + "{\"x\":" + exact(k.x) + ",\"y\":" + exact(k.y) +
             ",\"z\":" + exact(k.z) + ",\"vx\":" + exact(k.vx) + ",\"vy\":" + exact(k.vy) +
             ",\"vz\":" + exact(k.vz) + ",\"heading\":" + exact(k.heading) +
             ",\"yaw_rate\":" + exact(k.yaw_rate) + ",\"edge\":" + exact(k.edge) +
             ",\"tuck\":" + exact(k.tuck) + ",\"lean\":" + exact(k.lean) +
             ",\"sink\":" + exact(k.sink) + ",\"packed\":" + exact(k.packed) +
             ",\"speed\":" + exact(k.speed) + ",\"g_lat\":" + exact(k.g_lat) +
             ",\"jump_held\":" + exact(k.jump_held) + ",\"air_time\":" + exact(k.air_time) +
             ",\"air_peak\":" + exact(k.air_peak) + ",\"down_time\":" + exact(k.down_time) +
             ",\"on_snow\":" + flag(k.on_snow) + ",\"down\":" + flag(k.down) +
             ",\"cause\":" + std::to_string(static_cast<int>(k.cause)) +
             ",\"gate\":" + std::to_string(p.next) + ",\"missed\":" + flag(p.missed) +
             ",\"finished\":" + flag(p.finished) + ",\"finish_time\":" + exact(p.finish_time) +
             ",\"resets\":" + std::to_string(p.resets) +
             ",\"wipeouts\":" + std::to_string(p.wipeouts) +
             ",\"missed_gates\":" + std::to_string(p.missed_gates) +
             ",\"stuck\":" + exact(p.stuck) + "}";
    }
    return out + "]}";
  }

  std::string report() const override {
    const RunReport r = driver_.report(seed_);
    return "{\"finished\":" + flag(r.finished) + ",\"time\":" + fixed(r.time) +
           ",\"place\":" + std::to_string(r.place) +
           ",\"racers\":" + std::to_string(r.field.size()) +
           ",\"gates\":" + std::to_string(r.gates) +
           ",\"gate_count\":" + std::to_string(r.gate_count) +
           ",\"missed\":" + std::to_string(r.missed) + ",\"resets\":" + std::to_string(r.resets) +
           ",\"wipeouts\":" + std::to_string(r.wipeouts) +
           ",\"flights\":" + std::to_string(r.flights) + ",\"top_speed\":" + fixed(r.top_speed) +
           ",\"mean_speed\":" + fixed(r.mean_speed) +
           ",\"max_landing_g\":" + fixed(r.max_landing_g) + "}";
  }

  bool pilot(std::span<double> out) override {
    const GameState& st = driver_.state();
    ge::ai::Why* why = why_ ? why_->follow(0, st.steps) : nullptr;
    asked_ = hands_;
    asked_at_ = st.steps;
    const Controls c = asked_.press(bot_controls(st, 0, driver_.plan(), why, asked_.lead()), why);
    out[0] = c.ski.steer;
    out[1] = c.ski.tuck;
    out[2] = c.ski.brake;
    out[3] = c.ski.jump ? 1 : 0;
    out[4] = c.reset ? 1 : 0;
    return true;
  }

  bool trace(ge::ai::Why* why) override {
    why_ = why;
    driver_.set_why(why);
    return true;
  }

  // The whole run copied: the driver (the state, the plan, the tape so far, the rivals' hands),
  // the pilot's hands and the events not yet observed; the map and the runs' lines are shared.
  std::unique_ptr<ge::play::Run> clone() const override {
    auto copy = std::make_unique<FallLineRun>(*this);
    copy->trace(nullptr);
    return copy;
  }

  bool shoot(std::string_view view, ge::lab::Picture& out, std::string& caption,
             std::string& error) override {
    const GameState& s = driver_.state();
    const mg::Level& level = *s.level;
    const Racer& me = s.racers[0];
    const bool bot = view == "bot";
    const bool near = view == "near" || bot;
    if (!near && view != "map") {
      error = "no view `" + std::string(view) + "`";
      return false;
    }
    const pic::MapWindow window = near ? pic::MapWindow{me.skier.x, me.skier.z, kNearMetres}
                                       : pic::MapWindow{level.size / 2, level.size / 2, level.size};
    out = pic::draw_map(level, kShotPx, window);
    const double k = kShotPx / window.metres;
    const double x0 = window.x - window.metres / 2, z0 = window.z - window.metres / 2;
    // The field: each rival a blue disc, the player white ringed in red with a line for the
    // heading (0 = +z, clockwise from above: the way is (sin h, cos h)).
    for (size_t i = s.racers.size(); i-- > 0;) {
      const auto& k2 = s.racers[i].skier;
      const double px = (k2.x - x0) * k, pz = (k2.z - z0) * k;
      const double r = near ? 5 : 3;
      if (i == 0) {
        const double len = near ? 18 : 10;
        ge::lab::draw_line(out, px, pz, px + core::libm::sin(k2.heading) * len,
                           pz + core::libm::cos(k2.heading) * len, {200, 20, 20}, 2);
        ge::lab::fill_disc(out, px, pz, r + 2, {200, 20, 20});
        ge::lab::fill_disc(out, px, pz, r, {255, 255, 255});
      } else {
        ge::lab::fill_disc(out, px, pz, r, {40, 80, 220});
      }
    }
    if (bot) {
      caption = draw_bot(out, window);
      return true;
    }
    const Progress& p = me.progress;
    const auto gates = static_cast<int32_t>(level.checkpoints.size());
    caption =
        (near ? "The piste " + num::to_fixed(kNearMetres, 0) + " m round the player"
              : "The whole map with the field") +
        std::string(" at ") + num::to_fixed(s.t, 1) + " s: " +
        (p.finished ? "finished in " + num::to_fixed(p.finish_time, 2) + " s"
                    : "next gate " + std::to_string(p.next) + " of " + std::to_string(gates)) +
        ", " + num::to_fixed(me.skier.speed * 3.6, 1) + " km/h, " +
        (me.skier.down      ? "down"
         : me.skier.on_snow ? "on the snow"
                            : "in the air") +
        ", place " + std::to_string(place_of(s, 0)) + " of " + std::to_string(s.racers.size()) +
        ". The player is white ringed in red, the "
        "rivals blue, the piste's line red, the gates numbered.";
    return true;
  }

 private:
  // The pilot's plan and its decisions this step, drawn over `out` (the near window): its line
  // coloured by the plan's speed at its pace (red slow, green fast), the stretch it reads its cap
  // off thicker, the point it aims at in yellow; the caption says what it decided and why.
  std::string draw_bot(ge::lab::Picture& out, const pic::MapWindow& window) const {
    const GameState& s = driver_.state();
    const BotPlan& plan = driver_.plan();
    ge::ai::Why why;
    bot_controls(s, 0, plan, why.follow(0, 0), hands_.lead());
    const double k = kShotPx / window.metres;
    const double x0 = window.x - window.metres / 2, z0 = window.z - window.metres / 2;
    const double pace = s.racers[0].rider.pace;
    const auto& pts = plan.track->points;
    // The colours span the speeds planned inside the window.
    double slowest = INFINITY, fastest = 0;
    for (size_t i = 0; i < pts.size(); i++) {
      const bool in = std::fabs(pts[i].x - window.x) < window.metres / 2 &&
                      std::fabs(pts[i].z - window.z) < window.metres / 2;
      if (!in || !(plan.speed[i] < 1e6)) continue;
      slowest = num::min(slowest, plan.speed[i] * pace);
      fastest = num::max(fastest, plan.speed[i] * pace);
    }
    const ge::ai::Decision* line = nullptr;
    const ge::ai::Decision* speed = nullptr;
    for (const ge::ai::Decision& d : why.decisions()) {
      if (std::string_view(d.name) == "line") line = &d;
      if (std::string_view(d.name) == "speed") speed = &d;
    }
    const double here = line ? why.fact_of(*line, "s_m") : -1;
    const double reads = speed ? (bot::kReadTime * s.racers[0].skier.speed + bot::kReadLeast) : 0;
    for (size_t i = 1; i < pts.size(); i++) {
      const double f =
          fastest > slowest
              ? core::clamp((plan.speed[i] * pace - slowest) / (fastest - slowest), 0.0, 1.0)
              : 1.0;
      const ge::lab::Rgba c{static_cast<uint8_t>(230 * (1 - f)), static_cast<uint8_t>(200 * f), 40,
                            255};
      const bool read = pts[i].s >= here && pts[i].s <= here + reads;
      ge::lab::draw_line(out, (pts[i - 1].x - x0) * k, (pts[i - 1].z - z0) * k, (pts[i].x - x0) * k,
                         (pts[i].z - z0) * k, c, read ? 5 : 2);
    }
    const auto& me = s.racers[0].skier;
    std::string caption = "The pilot at " + num::to_fixed(s.t, 1) + " s, " +
                          num::to_fixed(me.speed * 3.6, 1) + " km/h: ";
    if (line) {
      const double ax = why.fact_of(*line, "aim_x"), az = why.fact_of(*line, "aim_z");
      ge::lab::draw_line(out, (me.x - x0) * k, (me.z - z0) * k, (ax - x0) * k, (az - z0) * k,
                         {240, 210, 0, 255}, 1);
      ge::lab::fill_disc(out, (ax - x0) * k, (az - z0) * k, 5, {240, 210, 0, 255});
      caption += "line " + std::string(line->chosen) + " (aims " +
                 num::to_fixed(why.fact_of(*line, "look_m"), 0) + " m ahead, " +
                 num::to_fixed(why.fact_of(*line, "lane_m"), 1) + " m in its lane, steer " +
                 num::to_fixed(why.fact_of(*line, "steer"), 2) + ")";
    } else {
      caption += "no line decision this step";
    }
    if (speed) {
      caption += ", speed " + std::string(speed->chosen) + " (cap " +
                 num::to_fixed(why.fact_of(*speed, "cap") * 3.6, 1) + " km/h, by " +
                 num::to_fixed(speed->margin, 2) + ")";
    }
    return caption + ". The line is the plan's speed at its pace, red slow to green fast (" +
           num::to_fixed(slowest * 3.6, 0) + " to " + num::to_fixed(fastest * 3.6, 0) +
           " km/h in view), thick where it reads its cap; the yellow disc is where it aims; the "
           "player white ringed in red.";
  }

  void collect() {
    for (const GameEvent& e : driver_.state().events) {
      const bool mine = e.racer == 0 || (e.kind == EventKind::shoulder && e.gate == 0);
      if (!mine) continue;
      if (backlog_.size() == kMostEvents) {
        backlog_.erase(backlog_.begin());
        dropped_++;
      }
      backlog_.push_back(e);
    }
  }

  Driver driver_;
  BotHands hands_, asked_;  // the pilot's hands, and as they are once the asked step is taken
  int64_t asked_at_ = -1;
  ge::ai::Why* why_ = nullptr;
  double seed_;
  std::vector<GameEvent> backlog_;
  int32_t dropped_ = 0;
};

// The setup's rivals and hands, checked: false with `error` when either is out of range.
bool read_field(const ge::play::Setup& setup, int32_t& rivals, HandsProfile& hands,
                std::string& error) {
  const double n = setup.number("rivals");
  const auto given = setup.options.find("hands");
  const auto profile = hands_profile(given == setup.options.end() ? "none" : given->second);
  if (!profile) {
    error = "hands must be `none` or `human` (fall_line/sim/hands.hpp)";
    return false;
  }
  if (n < 0 || n > 7 || n != num::round(n)) {
    error = "rivals must be a whole number from 0 to 7";
    return false;
  }
  rivals = static_cast<int32_t>(n);
  hands = *profile;
  return true;
}

std::unique_ptr<ge::play::Run> open_run(const ge::play::Setup& setup, std::string& error) {
  const double version = setup.number("version");
  int32_t rivals = 0;
  HandsProfile hands = HandsProfile::none;
  if (!read_field(setup, rivals, hands, error)) return nullptr;
  try {
    // The last map built, kept: `ge diverge` and a replay open the same map twice.
    static double last_seed = 0;
    static double last_version = -1;
    static std::shared_ptr<const mg::Level> last;
    if (!last || last_seed != setup.seed || last_version != version) {
      last = std::make_shared<const mg::Level>(
          race_map(setup.seed, static_cast<int>(num::round(version))));
      last_seed = setup.seed;
      last_version = version;
    }
    std::shared_ptr<const mg::Level> level = last;
    return std::make_unique<FallLineRun>(std::move(level), setup.seed, rivals, hands);
  } catch (const mg::GenerateError& e) {
    error = "seed " + exact(setup.seed) + " built no map: " + e.what() +
            "; pick another seed, or run `make lab LAB=analyze ARGS=--seeds=" + exact(setup.seed) +
            " --refusals`";
    return nullptr;
  }
}

constexpr std::array<ge::play::Axis, 5> kAxes{{
    {"steer", ge::play::AxisKind::signed_unit, "-1 full left .. 1 full right"},
    {"tuck", ge::play::AxisKind::lever, "0 upright .. 1 folded into the tuck: less drag"},
    {"brake", ge::play::AxisKind::lever, "0 .. 1 the skid that scrubs speed"},
    {"jump", ge::play::AxisKind::toggle, "held on the snow loads the jump; release to pop"},
    {"reset", ge::play::AxisKind::press, "stand back on the piste past the last gate taken"},
}};

constexpr std::array<ge::play::View, 3> kViews{{
    {"near", "the piste 160 m round the player from above, the field on it"},
    {"map", "the whole map from above: the face, the woods, the piste, the gates, the field"},
    {"bot",
     "the pilot's mind 160 m round the player: the speed plan along its line, the stretch "
     "it reads its cap off, the point it aims at, its speed and line decisions"},
}};

constexpr std::array<ge::play::Option, 3> kOptions{{
    {"rivals", "0", "bots on the grid beside the player (0..7)"},
    {"version", "1", "the generator's version the race map is built by"},
    {"hands", "none",
     "every bot's hands: none (as decided) or human (a person's reaction, "
     "slew and wander)"},
}};

}  // namespace

const GameState* state_of(const ge::play::Run& run) {
  const auto* r = dynamic_cast<const FallLineRun*>(&run);
  return r ? &r->game() : nullptr;
}

std::unique_ptr<ge::play::Run> open_run_on(std::shared_ptr<const mg::Level> level,
                                           const ge::play::Setup& setup, std::string& error) {
  int32_t rivals = 0;
  HandsProfile hands = HandsProfile::none;
  if (!read_field(setup, rivals, hands, error)) return nullptr;
  return std::make_unique<FallLineRun>(std::move(level), setup.seed, rivals, hands);
}

const ge::play::GameSpec kGame{
    "fall_line",
    "a downhill race through the gates of a generated mountain, the field beside you",
    kHz,
    600,
    kAxes,
    kOptions,
    open_run,
    kViews,
    cfg::register_cvars,
};

}  // namespace fall_line::sim
