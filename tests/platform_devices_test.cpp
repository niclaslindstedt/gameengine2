// The OS's input devices (engine/platform/devices.hpp) through SDL3's virtual joystick driver,
// with no device and no display: a gamepad, a wheel and a flight stick are plugged, moved,
// felt and unplugged, and what reaches engine/input is held to what was done. SDL treats the
// virtual devices as hardware, so its gamepad mapping, its events, the translation and the
// feedback going back out are all the real path.
//
// ge:summary Tests for the platform's input backend: virtual pads, wheels and sticks end to end
// ge:keywords test platform input-backend sdl virtual-joystick gamepad wheel flight-stick hot-plug
// ge:keywords rumble lightbar player-lights gyro touchpad haptic-level
// ge:skill input platform testing
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>

#include "check.hpp"
#include "gpu_support.hpp"
#include "input/feedback.hpp"
#include "input/names.hpp"
#include "input/state.hpp"
#include "platform/devices.hpp"
#include "platform/window.hpp"

namespace in = ge::input;
namespace pl = ge::platform;

namespace {

struct Rig {
  pl::DevicesPtr dev = pl::open_devices({.sensors = true, .haptics = false, .keyboard = false});
  in::Queue q;
  in::InputState s;
  // One frame: the devices' events read and folded into the state.
  void frame() {
    q.clear();
    pl::pump_devices(*dev, q);
    in::update(s, q);
  }
  const in::Slot* slot(uint32_t handle) const {
    return in::find_slot(s, pl::virtual_id(*dev, handle));
  }
};

bool near(float a, float b, float tol = 0.002f) { return std::fabs(a - b) <= tol; }

}  // namespace

TEST(a_virtual_gamepad_plugs_in_as_a_gamepad_and_reads_positionally) {
  Rig r;
  CHECK(r.dev != nullptr);
  if (!r.dev) return;
  const uint32_t h = pl::attach_virtual(
      *r.dev,
      {.kind = in::DeviceKind::gamepad, .name = "test pad", .vendor = 0x045e, .product = 0x0b13});
  CHECK(h != 0);
  r.frame();
  const in::Slot* pad = r.slot(h);
  CHECK(pad != nullptr);
  if (!pad) return;
  CHECK(pad->info.kind == in::DeviceKind::gamepad);
  CHECK(pad->info.key == "045e:0b13");
  CHECK(in::has(pad->info, in::kRumble));
  CHECK(r.s.added_n == 1);

  pl::virtual_button(*r.dev, h, static_cast<int>(in::PadButton::south), true);
  pl::virtual_button(*r.dev, h, static_cast<int>(in::PadButton::dpad_left), true);
  pl::virtual_axis(*r.dev, h, static_cast<int>(in::PadAxis::left_x), -0.5f);
  pl::virtual_axis(*r.dev, h, static_cast<int>(in::PadAxis::right_y), 1.0f);
  pl::virtual_axis(*r.dev, h, static_cast<int>(in::PadAxis::right_trigger), 0.75f);
  r.frame();
  pad = r.slot(h);
  CHECK(pad->buttons.went_down(static_cast<int>(in::PadButton::south)));
  CHECK(pad->buttons.down(static_cast<int>(in::PadButton::dpad_left)));
  CHECK(!pad->buttons.down(static_cast<int>(in::PadButton::east)));
  CHECK_MSG(near(pad->axes[static_cast<int>(in::PadAxis::left_x)], -0.5f), "left_x reads -0.5");
  CHECK_MSG(near(pad->axes[static_cast<int>(in::PadAxis::right_y)], 1.0f), "right_y reads 1");
  CHECK_MSG(near(pad->axes[static_cast<int>(in::PadAxis::right_trigger)], 0.75f, 0.01f),
            "a trigger reads 0..1 off the axis's whole range");

  pl::virtual_button(*r.dev, h, static_cast<int>(in::PadButton::south), false);
  r.frame();
  CHECK(r.slot(h)->buttons.went_up(static_cast<int>(in::PadButton::south)));

  pl::detach_virtual(*r.dev, h);
  r.frame();
  CHECK(r.s.removed_n == 1);
  CHECK(r.slot(h) == nullptr);
}

TEST(a_wheel_and_a_flight_stick_plug_in_as_their_kinds_with_axes_hats_and_buttons) {
  Rig r;
  if (!r.dev) return;
  const uint32_t wheel = pl::attach_virtual(*r.dev, {.kind = in::DeviceKind::wheel,
                                                     .name = "test wheel",
                                                     .vendor = 0xfff2,
                                                     .product = 0x0001,
                                                     .axes = 4,
                                                     .buttons = 24,
                                                     .hats = 1});
  const uint32_t stick = pl::attach_virtual(*r.dev, {.kind = in::DeviceKind::flight_stick,
                                                     .name = "test stick",
                                                     .axes = 3,
                                                     .buttons = 12,
                                                     .hats = 1});
  r.frame();
  const in::Slot* w = r.slot(wheel);
  const in::Slot* fs = r.slot(stick);
  CHECK(w != nullptr && fs != nullptr);
  if (!w || !fs) return;
  CHECK(w->info.kind == in::DeviceKind::wheel);
  CHECK(w->info.key == "fff2:0001");
  CHECK(w->info.axes == 4 && w->info.buttons == 24 && w->info.hats == 1);
  CHECK(fs->info.kind == in::DeviceKind::flight_stick);
  CHECK(!fs->info.key.empty());

  pl::virtual_axis(*r.dev, wheel, 0, 0.25f);
  pl::virtual_axis(*r.dev, wheel, 2, -1.0f);
  pl::virtual_button(*r.dev, wheel, 4, true);
  pl::virtual_hat(*r.dev, stick, 0, in::kHatUp | in::kHatRight);
  r.frame();
  w = r.slot(wheel);
  fs = r.slot(stick);
  CHECK(near(w->axes[0], 0.25f));
  CHECK(near(w->axes[2], -1.0f));
  CHECK(w->buttons.went_down(4));
  CHECK(fs->hats[0] == (in::kHatUp | in::kHatRight));
  CHECK(fs->hats_pressed[0] == (in::kHatUp | in::kHatRight));
}

TEST(rumble_the_light_and_the_player_lights_reach_the_device) {
  Rig r;
  if (!r.dev) return;
  const uint32_t h =
      pl::attach_virtual(*r.dev, {.kind = in::DeviceKind::gamepad, .name = "test pad"});
  r.frame();
  const in::DeviceId id = pl::virtual_id(*r.dev, h);
  CHECK(id != 0);
  in::Feedback fb;
  in::rumble(fb, id, 1.0f, 0.5f, 0.2, 0.0);
  in::set_light(fb, id, 10, 20, 30);
  fb.get(id)->player = 2;
  pl::play_feedback(*r.dev, fb, r.s, 0.0);
  pl::VirtualFeedback told = pl::virtual_feedback(*r.dev, h);
  CHECK(told.low == 65535 && told.high == 32768);
  CHECK(told.light && told.r == 10 && told.g == 20 && told.b == 30);
  CHECK(told.player == 2);
  const int calls = told.rumbles;
  pl::play_feedback(*r.dev, fb, r.s, 0.05);  // nothing changed: nothing sent
  CHECK(pl::virtual_feedback(*r.dev, h).rumbles == calls);
  pl::play_feedback(*r.dev, fb, r.s, 0.3);  // the rumble ran out: the motors are stopped
  told = pl::virtual_feedback(*r.dev, h);
  CHECK(told.low == 0 && told.high == 0);
}

TEST(a_pads_gyro_and_touchpad_arrive_as_sensor_and_pad_touch_events) {
  Rig r;
  if (!r.dev) return;
  const uint32_t h = pl::attach_virtual(
      *r.dev, {.kind = in::DeviceKind::gamepad, .name = "test pad", .gyro = true, .touchpads = 1});
  r.frame();
  const in::Slot* pad = r.slot(h);
  CHECK(pad != nullptr);
  if (!pad) return;
  CHECK(in::has(pad->info, in::kGyro) && in::has(pad->info, in::kAccel));
  CHECK(in::has(pad->info, in::kTouchpad) && pad->info.touchpads == 1);
  pl::virtual_sensor(*r.dev, h, true, 0.5f, -1.0f, 2.0f);
  pl::virtual_touch(*r.dev, h, 0, true, 0.25f, 0.75f);
  r.frame();
  pad = r.slot(h);
  CHECK(near(pad->gyro[0], 0.5f) && near(pad->gyro[1], -1.0f) && near(pad->gyro[2], 2.0f));
  CHECK(pad->pad_fingers[0].active);
  CHECK(near(pad->pad_fingers[0].x, 0.25f) && near(pad->pad_fingers[0].y, 0.75f));
}

TEST(a_force_becomes_a_haptic_level_that_is_silent_at_rest) {
  CHECK(pl::haptic_level(0) == 0);
  CHECK(pl::haptic_level(1e-5) == 0);
  CHECK(pl::haptic_level(1) == 32767);
  CHECK(pl::haptic_level(-2) == -32767);
  CHECK(pl::haptic_level(0.5) == 16384);
}

TEST(keys_and_a_relative_mouse_are_read_through_the_windows_pump) {
  if (!pl::display_available()) {
    if (ge::test::env_on("GE_GPU_REQUIRE")) {
      ge::test::fail(__FILE__, __LINE__, "no display, and GE_GPU_REQUIRE=1: run under xvfb-run -a");
    }
    return;  // the rest of this file needs no display; only this case does
  }
  std::unique_ptr<pl::Window> window;
  for (int attempt = 0; attempt < 5 && !window; ++attempt) {
    if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    window = pl::open_window({.title = "platform_devices_test", .width = 320, .height = 200});
  }
  CHECK(window != nullptr);
  if (!window) return;
  pl::DevicesPtr dev = pl::open_devices({.sensors = false, .haptics = false});
  CHECK(dev != nullptr);
  if (!dev) return;
  in::Queue q;
  in::InputState s;
  pl::InputSnapshot snap;
  pl::poll_input(*window, snap, *dev, q);
  in::update(s, q);
  CHECK(s.keyboard != 0 && s.mouse_id != 0);  // the pooled keyboard and mouse plugged

  pl::set_relative_mouse(*window, true);
  // The OS may hold relative mode back until the window has the focus, and a shared virtual
  // display (CI runs tests side by side on one) can move the pointer under it: settle the window
  // for a moment, then look for this test's own deltas among whatever motion arrives.
  for (int i = 0; i < 20 && !pl::relative_mouse(*window); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    q.clear();
    pl::poll_input(*window, snap, *dev, q);
    in::update(s, q);
  }
  const bool relative = pl::relative_mouse(*window);
  pl::virtual_key(*dev, in::key::w, true);
  pl::virtual_mouse_motion(*dev, 3.5f, -2.0f);
  pl::virtual_mouse_motion(*dev, 1.0f, 0.5f);
  q.clear();
  pl::poll_input(*window, snap, *dev, q);
  in::update(s, q);
  CHECK(in::key_pressed(s, in::key::w) && in::key_held(s, in::key::w));
  CHECK(snap.down(pl::Key::w));  // the window's own snapshot still reads the same pump
  const float want[2][2] = {{3.5f, -2.0f}, {1.0f, 0.5f}};
  int found = 0;
  for (const in::Event& e : q.events) {
    if (e.type != in::EventType::mouse_motion || found == 2) continue;
    if (e.device != s.mouse_id || e.v[0] != want[found][0] || e.v[1] != want[found][1]) continue;
    CHECK_MSG((e.aux != 0) == relative,
              "a motion is flagged raw exactly while relative mode is in force");
    ++found;
  }
  CHECK_MSG(found == 2, "both deltas arrive, in order, as the mouse's motion");
  if (!relative) std::fprintf(stderr, "note: relative mode was not in force (no focus)\n");
  pl::set_relative_mouse(*window, false);
}

TEST(the_prefs_dir_is_the_players_own_and_is_made) {
  const std::string dir = pl::prefs_dir("gameengine2-test", "prefs");
  if (dir.empty()) return;  // a platform with no per-user place: a game keeps nothing
  CHECK_MSG(dir.back() == '/' || dir.back() == '\\', "the directory ends in a separator");
  CHECK_MSG(std::filesystem::is_directory(dir), "the directory is made when missing");
  const std::filesystem::path app = std::filesystem::path(dir).parent_path();
  std::error_code ec;
  std::filesystem::remove(app, ec);
  std::filesystem::remove(app.parent_path(), ec);
}
