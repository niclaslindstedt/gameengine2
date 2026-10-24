// Fall Line's GPU renderer on the play contract (play_render.hpp): the device, the frame made for
// the run's map on its first picture, each view's camera, and the four modes read back.
//
// ge:summary FrameRenderer: a play Run's GameState drawn lit, wire, depth or overlay
// ge:keywords play renderer frame-renderer readback depth-grey overlay project gates piste-line
// ge:keywords velocity-arrow labels
// ge:skill render agent-interface
#include "fall_line/app/play_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "camera/to_view.hpp"
#include "core/num_text.hpp"
#include "fall_line/app/frame.hpp"
#include "fall_line/app/views.hpp"
#include "fall_line/camera/machines.hpp"
#include "fall_line/camera/subject.hpp"
#include "fall_line/camera/world.hpp"
#include "fall_line/sim/play.hpp"
#include "fall_line/sim/state.hpp"
#include "gpu/device.hpp"
#include "gpu/target.hpp"
#include "lab/raster.hpp"
#include "render/view.hpp"

namespace fall_line::app {

namespace {

namespace num = ge::core::num;
namespace r = ge::render;
using ge::lab::Picture;
using ge::lab::Rgba;

constexpr double kFarGrey = 4000;    // m: the depth mode's black
constexpr double kNearGrey = 1;      // m: its white
constexpr double kArrowAhead = 0.5;  // s of velocity a racer's arrow reaches
constexpr double kMarkReach = 1500;  // m: the overlay marks nothing farther

constexpr Rgba kPiste{230, 40, 40, 255};
constexpr Rgba kGate{255, 210, 40, 255};
constexpr Rgba kPlayer{90, 255, 120, 255};
constexpr Rgba kRival{120, 190, 255, 255};
constexpr Rgba kLabelBack{0, 0, 0, 160};

// A point on the screen, or none behind the eye.
struct Screen {
  double x, y;
};

// The frame's camera on the CPU: a world point to the picture's pixels, and whether the frame's
// depth (each pixel's distance, `distance` at the picture's size) shows it or something nearer.
class Projector {
 public:
  Projector(const r::FrameView& view, int width, int height, const std::vector<float>& distance)
      : m_(r::camera_matrices(view, static_cast<double>(width) / height)),
        eye_{view.eye[0], view.eye[1], view.eye[2]},
        w_(width),
        h_(height),
        distance_(distance) {}

  std::optional<Screen> at(double x, double y, double z) const {
    const float in[4] = {static_cast<float>(x - eye_[0]), static_cast<float>(y - eye_[1]),
                         static_cast<float>(z - eye_[2]), 1.0f};
    float c[4];
    r::transform(m_.view_proj, in, c);
    if (c[3] < 0.05f) return std::nullopt;
    return Screen{(c[0] / c[3] + 1) * 0.5 * w_, (1 - c[1] / c[3]) * 0.5 * h_};
  }

  double distance(double x, double y, double z) const {
    return std::hypot(x - eye_[0], y - eye_[1], z - eye_[2]);
  }

  // On the screen and not behind the ground or a tree (a little slack: a mark lies on the snow).
  bool seen(double x, double y, double z) const {
    const auto s = at(x, y, z);
    if (!s || s->x < 0 || s->y < 0 || s->x >= w_ || s->y >= h_) return false;
    const size_t i =
        static_cast<size_t>(s->y) * static_cast<size_t>(w_) + static_cast<size_t>(s->x);
    if (i >= distance_.size()) return true;
    const double shown = distance_[i];
    return !std::isfinite(shown) || distance(x, y, z) <= shown * 1.03 + 1.0;
  }

 private:
  r::CameraMatrices m_;
  std::array<double, 3> eye_;
  int w_, h_;
  const std::vector<float>& distance_;
};

// A segment drawn when both its ends are seen (`always`: in front of the eye is enough).
void line3(Picture& p, const Projector& pr, const double a[3], const double b[3], Rgba c,
           double thick, bool always = false) {
  if (!always && (!pr.seen(a[0], a[1], a[2]) || !pr.seen(b[0], b[1], b[2]))) return;
  const auto sa = pr.at(a[0], a[1], a[2]);
  const auto sb = pr.at(b[0], b[1], b[2]);
  if (sa && sb) ge::lab::draw_line(p, sa->x, sa->y, sb->x, sb->y, c, thick);
}

void label(Picture& p, double x, double y, const std::string& text, Rgba c, int scale) {
  const int tw = ge::lab::text_width(text, scale);
  const int th = ge::lab::kGlyphH * scale;
  const int lx = static_cast<int>(x) - tw / 2, ly = static_cast<int>(y) - th - 4;
  ge::lab::fill_rect(p, lx - 2, ly - 2, tw + 4, th + 4, kLabelBack);
  ge::lab::draw_text(p, lx, ly, text, c, scale);
}

// The race drawn over the lit picture: the piste's line and the gates where the frame shows them,
// the racers' velocities always. `distance` is the frame's, a pixel each of the picture.
void draw_overlay(Picture& p, const sim::GameState& s, const r::FrameView& view,
                  const std::vector<float>& distance) {
  const auto& level = *s.level;
  const Projector pr(view, p.width, p.height, distance);
  const int scale = p.width >= 960 ? 2 : 1;
  const double thick = scale;
  const auto& pts = level.track.points;
  for (size_t i = 5; i < pts.size(); i += 5) {
    const auto& a = pts[i - 5];
    const auto& b = pts[i];
    if (pr.distance(a.x, a.y, a.z) > kMarkReach) continue;
    const double pa[3] = {a.x, a.y + 0.15, a.z}, pb[3] = {b.x, b.y + 0.15, b.z};
    line3(p, pr, pa, pb, kPiste, thick);
  }
  for (size_t g = 0; g < level.checkpoints.size(); ++g) {
    const auto& c = level.checkpoints[g];
    if (pr.distance(c.x, c.y, c.z) > kMarkReach) continue;
    // Across the piste: heading 0 = +z, clockwise from above, so right is (cos h, -sin h).
    const double rx = std::cos(c.heading) * c.width / 2, rz = -std::sin(c.heading) * c.width / 2;
    const double l0[3] = {c.x - rx, c.y + 0.2, c.z - rz}, r0[3] = {c.x + rx, c.y + 0.2, c.z + rz};
    const double l1[3] = {l0[0], c.y + 1.8, l0[2]}, r1[3] = {r0[0], c.y + 1.8, r0[2]};
    line3(p, pr, l0, r0, kGate, thick);
    line3(p, pr, l0, l1, kGate, thick * 2);
    line3(p, pr, r0, r1, kGate, thick * 2);
    const bool last = g + 1 == level.checkpoints.size();
    if (!pr.seen(c.x, c.y + 1.0, c.z)) continue;
    if (const auto top = pr.at(c.x, c.y + 2.2, c.z)) {
      label(p, top->x, top->y,
            g == 0 ? "START"
            : last ? "FINISH"
                   : "G" + std::to_string(g),
            kGate, scale);
    }
  }
  for (size_t i = s.racers.size(); i-- > 0;) {
    const auto& k = s.racers[i].skier;
    const Rgba c = i == 0 ? kPlayer : kRival;
    const double a[3] = {k.x, k.y + 1.0, k.z};
    const double b[3] = {k.x + k.vx * kArrowAhead, k.y + 1.0 + k.vy * kArrowAhead,
                         k.z + k.vz * kArrowAhead};
    line3(p, pr, a, b, c, thick * 2, true);
    if (const auto tip = pr.at(b[0], b[1], b[2]))
      ge::lab::fill_disc(p, tip->x, tip->y, 3 * thick, c);
    if (const auto head = pr.at(k.x, k.y + 1.9, k.z)) {
      label(p, head->x, head->y,
            (i == 0 ? std::string("P ") : "R" + std::to_string(i) + " ") +
                num::to_fixed(k.speed * 3.6, 0) + " KM/H",
            c, scale);
    }
  }
}

// How far each pixel is, as grey: near white, kFarGrey black on a log scale, the sky black.
Picture depth_picture(const std::vector<float>& distance, int width, int height) {
  Picture p = ge::lab::create_picture(width, height, {0, 0, 0, 255});
  const double span = std::log(kFarGrey / kNearGrey);
  for (size_t i = 0; i < distance.size() && i * 4 + 3 < p.rgba.size(); ++i) {
    const double d = distance[i];
    if (!std::isfinite(d)) continue;
    const double v = 1 - std::clamp(std::log(std::max(d, kNearGrey) / kNearGrey) / span, 0.0, 1.0);
    const auto g = static_cast<uint8_t>(std::lround(v * 255));
    p.rgba[i * 4] = p.rgba[i * 4 + 1] = p.rgba[i * 4 + 2] = g;
  }
  return p;
}

// A rung's view also says the point it frames (`subject`); a view of the mountain frames none.
bool camera(const sim::GameState& s, std::string_view name, r::FrameView& view,
            std::optional<std::array<double, 3>>& subject) {
  subject.reset();
  const auto& level = *s.level;
  day_view(level, -1, view);
  view.time_s = s.t;
  // A rung of the ladder of what he is on (his skis or a machine), settled on the player (a still
  // has no frames before it).
  fall_line::camera::RideCamera cam = fall_line::camera::make_ride_camera({}, 1);
  if (fall_line::camera::set_lens(cam, s, name)) {
    const ge::skier::World sw = s.world();
    ge::camera::to_frame_view(
        fall_line::camera::frame_ride(cam, s, sw, fall_line::camera::camera_world(sw), 1.0 / 60),
        view);
    const ge::camera::Vec3 p = fall_line::camera::framed_point(s.racers[0].skier);
    subject = std::array<double, 3>{p.x, p.y, p.z};
    return true;
  }
  return make_view(level, name, view);
}

class FrameRenderer final : public ge::play::Renderer {
 public:
  explicit FrameRenderer(std::unique_ptr<ge::gpu::Device> device) : device_(std::move(device)) {}

  bool draw(const ge::play::Run& run, const ge::play::RenderShot& shot, Picture& out,
            std::string& caption, std::string& error) override {
    const sim::GameState* s = sim::state_of(run);
    if (!s || !s->level || s->racers.empty()) {
      error = "the run is not a Fall Line run with a racer";
      return false;
    }
    const auto w = static_cast<uint32_t>(shot.width), h = static_cast<uint32_t>(shot.height);
    if (!frame_) {
      FrameOptions options;
      options.taa = true;
      options.scale = 1.0;
      options.wire_probe = true;
      frame_ = create_frame(*device_, *s->level, w, h, wgpu::TextureFormat::RGBA8Unorm, options);
      if (!frame_) {
        error = "Fall Line's frame did not build; the gpu: lines above say why";
        return false;
      }
      level_ = s->level.get();
    } else if (level_ != s->level.get()) {
      error = "this renderer drew another map; open a new one for this run";
      return false;
    }
    if (target_.width != w || target_.height != h) {
      if (frame_->output_width != w || frame_->output_height != h) resize(*device_, *frame_, w, h);
      target_ = ge::gpu::create_target(*device_, "play.final", w, h);
      wire_ = ge::gpu::create_target(*device_, "play.wire", frame_->targets.width,
                                     frame_->targets.height, r::kWireFormat);
    }
    r::FrameView view;
    std::optional<std::array<double, 3>> subject;
    if (!camera(*s, shot.view, view, subject)) {
      error = "no view `" + std::string(shot.view) + "`";
      return false;
    }
    std::vector<ge::skier::SkierState> skiers;
    skiers.reserve(s->racers.size());
    for (const auto& racer : s->racers) skiers.push_back(racer.skier);
    if (shot.cut) cut(*frame_);
    const int frames = shot.cut ? kSettle : 1;
    for (int i = 0; i < frames; ++i) {
      view.frame = drawn_++;
      fall_line::app::draw(*device_, *frame_, view, {skiers, 0, true, {}, subject}, target_.view);
    }
    const int pw = shot.width, ph = shot.height;
    if (shot.mode == "wireframe") {
      draw_wire(*device_, *frame_, wire_.view);
      const ge::gpu::Image img = ge::gpu::read_now(*device_, wire_);
      out = Picture{static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
    } else if (shot.mode == "depth") {
      out = depth_picture(r::read_distance(*device_, frame_->targets, view),
                          static_cast<int>(frame_->targets.width),
                          static_cast<int>(frame_->targets.height));
    } else if (shot.mode == "lit" || shot.mode == "overlay") {
      const ge::gpu::Image img = ge::gpu::read_now(*device_, target_);
      out = Picture{static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
      if (shot.mode == "overlay") {
        // At the scale the frame renders at 1, the depth's pixels are the picture's.
        std::vector<float> distance = r::read_distance(*device_, frame_->targets, view);
        if (frame_->targets.width != w || frame_->targets.height != h) distance.clear();
        draw_overlay(out, *s, view, distance);
      }
    } else {
      error = "no mode `" + std::string(shot.mode) + "`";
      return false;
    }
    if (out.width != pw || out.height != ph) out = ge::lab::resized(out, pw, ph);
    const auto& me = s->racers[0];
    const auto gates = s->level->checkpoints.size();
    caption = std::string(shot.view) + " " + std::string(shot.mode) + " at " +
              num::to_fixed(s->t, 2) + " s: the player at " +
              num::to_fixed(me.skier.speed * 3.6, 1) + " km/h, " +
              (me.progress.finished
                   ? "finished"
                   : "gate " + std::to_string(me.progress.next) + " of " + std::to_string(gates)) +
              ", " + std::to_string(s->racers.size()) + " racer" +
              (s->racers.size() == 1 ? "" : "s") + " on the GPU (" + device_->report.name + ")";
    return true;
  }

 private:
  std::unique_ptr<ge::gpu::Device> device_;
  std::unique_ptr<GameFrame> frame_;
  const ge::mountain::mapgen::Level* level_ = nullptr;
  ge::gpu::Target target_, wire_;
  uint64_t drawn_ = 0;
};

std::unique_ptr<ge::play::Renderer> open_renderer(std::string& error) {
  auto device = ge::gpu::create_device({.label = "play.render"});
  if (!device) {
    error =
        "no GPU adapter on this machine (install Mesa's Vulkan drivers: apt install "
        "mesa-vulkan-drivers)";
    return nullptr;
  }
  return std::make_unique<FrameRenderer>(std::move(device));
}

// The rungs name the lens of what he is on: on a machine, a skis' rung is its counterpart on the
// machine's ladder (camera/machines.hpp), and the machines' own rungs follow the skis'.
constexpr std::array<ge::play::View, 15> kViews{{
    {"tips", "low over the player's ski tips: the snow coming straight at the lens"},
    {"helmet", "the player's own eyes, tipped with his lean"},
    {"chase", "behind and over the player along his way, the camera a run is watched from"},
    {"far", "the chase from further back and higher, heavier"},
    {"high", "high over and behind the player: the line through the gates ahead"},
    {"orbit", "the menu's drone, off to the side of the player"},
    {"seat", "on a snowmobile or a helicopter: the player's own eyes on its boards or its skid"},
    {"pilot", "under the paramotor: the player's own eyes in the harness, rolled with the wing"},
    {"wing", "under the paramotor: from up by the canopy, behind, looking down on the player"},
    {"down", "on a helicopter or under the paramotor: the long fall under the player"},
    {"start", "from behind the start line at a skier's eye height, down the piste"},
    {"piste", "halfway down the piste at a skier's eye height, looking down it"},
    {"lookback", "from the finish, looking back up the piste to the summit"},
    {"vista", "high over the summit, looking down the mountain to the valley"},
    {"aerial", "from high over a corner of the map, the whole mountain"},
}};

constexpr std::array<ge::play::View, 4> kModes{{
    {"lit", "the picture as the game draws it"},
    {"wireframe",
     "the meshes' edges over black: the ground's tiles banded by distance, the trees "
     "and figures orange"},
    {"depth", "how far each pixel is: near white, 4 km black (log scale), the sky black"},
    {"overlay",
     "the lit picture with the piste's line, the gates and each racer's velocity, "
     "number and speed drawn over it"},
}};

}  // namespace

const ge::play::RendererSpec kRenderer{kViews, kModes, 640, 360, open_renderer};

}  // namespace fall_line::app
