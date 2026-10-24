// The export's sync fixture (sync_fixture.hpp).
//
// ge:summary sync_fixture's definitions: the run, its flash view and its click
// ge:keywords sync fixture flash click run sound
// ge:skill replay
// ge:test tests/capture_av_export_test.cpp
#include "capture/sync_fixture.hpp"

#include <array>
#include <string>

#include "audio/synth.hpp"

namespace ge::capture {
namespace {

constexpr double kHz = 120;
constexpr int64_t kLitSteps = 6;  // hz / 20: a frame at 20 fps or more lands inside it

constexpr std::array<play::Axis, 1> kAxes{{{"idle", play::AxisKind::toggle, "does nothing"}}};
constexpr std::array<play::Option, 2> kOptions{{
    {"period", "150", "steps between flashes"},
    {"first", "37", "the step of the first flash"},
}};
constexpr std::array<play::View, 1> kViews{{{"flash", "white on a flash, black between"}}};

class SyncRun final : public play::Run {
 public:
  SyncRun(int64_t period, int64_t first) : period_(period), first_(first) {}

  void step(std::span<const double> input) override {
    (void)input;
    steps_++;
    if (sync_clicks_at(steps_, period_, first_)) lit_since_ = steps_;
    digest_ = (digest_ ^ static_cast<uint64_t>(steps_)) * 0x100000001B3ull;
  }
  bool over() const override { return false; }
  uint64_t digest() const override { return digest_; }
  std::string observe() override { return state(); }
  std::string state() const override {
    return "{\"steps\":" + std::to_string(steps_) + ",\"lit\":" + (lit() ? "true" : "false") + "}";
  }
  std::string report() const override { return state(); }
  bool shoot(std::string_view view, lab::Picture& out, std::string& caption,
             std::string& error) override {
    if (view != "flash") {
      error = "the sync fixture draws only `flash`";
      return false;
    }
    const uint8_t c = lit() ? 255 : 0;
    out = lab::create_picture(64, 36, {c, c, c, 255});
    caption = lit() ? "flash" : "dark";
    return true;
  }
  std::unique_ptr<play::Run> clone() const override { return std::make_unique<SyncRun>(*this); }

  // A click on the step just taken.
  bool clicked() const { return lit_since_ == steps_ && steps_ > 0; }

 private:
  bool lit() const { return lit_since_ >= 0 && steps_ - lit_since_ < kLitSteps; }

  int64_t period_, first_;
  int64_t steps_ = 0;
  int64_t lit_since_ = -1;
  uint64_t digest_ = 0xcbf29ce484222325ull;
};

std::unique_ptr<play::Run> open_sync(const play::Setup& setup, std::string& error) {
  const auto period = static_cast<int64_t>(setup.number("period"));
  const auto first = static_cast<int64_t>(setup.number("first"));
  if (period < 1 || first < 1) {
    error = "period and first must be at least 1";
    return nullptr;
  }
  return std::make_unique<SyncRun>(period, first);
}

class SyncSound final : public play::Sound {
 public:
  explicit SyncSound(audio::Synth& synth) : synth_(synth) {}
  void step(const play::Run& run) override {
    const auto* r = dynamic_cast<const SyncRun*>(&run);
    if (!r || !r->clicked()) return;
    audio::Tone click;
    click.wave = audio::Wave::square;
    click.from_hz = 1000;
    click.duration_ms = 20;
    click.volume = 0.5f;
    synth_.tone(click);
  }
  void frame(const play::Run& run, double dt) override {
    (void)run;
    (void)dt;
  }

 private:
  audio::Synth& synth_;
};

std::unique_ptr<play::Sound> open_sync_sound(audio::Synth& synth, std::string& error) {
  (void)error;
  synth.bus_gain(0, 1, 0);
  return std::make_unique<SyncSound>(synth);
}

}  // namespace

const play::GameSpec kSyncGame{
    .name = "sync_fixture",
    .summary = "flashes and clicks on the same steps: the export's sync fixture",
    .hz = kHz,
    .most_seconds = 600,
    .axes = kAxes,
    .options = kOptions,
    .open = open_sync,
    .views = kViews,
    .version = 1,
};

const play::SoundSpec kSyncSound{open_sync_sound};

bool sync_clicks_at(int64_t step, int64_t period, int64_t first) {
  return step >= first && (step - first) % period == 0;
}

play::InputLog sync_log(int64_t steps, int64_t period, int64_t first) {
  play::InputLog log;
  log.game = kSyncGame.name;
  log.seed = 0;
  log.hz = kHz;
  log.axes = {"idle"};
  log.options = {{"period", std::to_string(period)}, {"first", std::to_string(first)}};
  log.runs = {{steps, {0.0}}};
  return log;
}

}  // namespace ge::capture
