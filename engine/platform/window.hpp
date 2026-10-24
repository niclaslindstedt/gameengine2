// THE WINDOW AND ITS INPUT: the one place a native window is opened, its events read and its
// native handles handed to the GPU's surface. Everything above this sees plain structs: a
// `NativeWindow` (what engine/gpu needs to make a surface), an `InputSnapshot` (what the
// frame's input was) and a monotonic clock for the frame's delta. SDL3 is the implementation
// (window.cpp) and nothing else in the tree includes it.
//
// Fall Line's needs set the scope: one window, resizable, high pixel density, keyboard and
// pointer, focus (which pauses the run clock, engine/loop/run_clock.hpp) and quit. Every input
// device as engine/input events (gamepads, joysticks, wheels, raw mice, touch) is devices.hpp's,
// read in the same pump.
//
// HEADLESS: a test or a lab never opens a window. `display_available()` says whether this
// process can; a test that needs one skips when it cannot.
//
// ge:summary The window, its native handles, per-frame input snapshot and the frame clock
// ge:keywords platform window sdl native-window surface-handles input keyboard pointer mouse
// ge:keywords focus quit resize pixel-size hidpi input-snapshot poll clock ticks monotonic
// ge:keywords headless display_available open_window poll_input now_ns prefs_dir prefs user-dir
// ge:skill platform
// ge:test tests/platform_window_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ge::platform {

// What the GPU's surface is made from. Exactly the handles of `kind` are set.
struct NativeWindow {
  enum class Kind : uint8_t { none, x11, wayland, metal, win32 };
  Kind kind = Kind::none;
  void* display = nullptr;      // x11: Display*; wayland: wl_display*
  uint64_t x11_window = 0;      // x11: the Window id
  void* surface = nullptr;      // wayland: wl_surface*; win32: HWND
  void* instance = nullptr;     // win32: HINSTANCE
  void* metal_layer = nullptr;  // metal: CAMetalLayer*
};

// A key, numbered by its USB HID usage (the physical key, whatever the layout): the index into
// `InputSnapshot::held`. Only the keys something reads are named.
enum class Key : uint8_t {
  a = 4,
  c = 6,
  d = 7,
  s = 22,
  w = 26,
  enter = 40,
  escape = 41,
  space = 44,
  right = 79,
  left = 80,
  down = 81,
  up = 82,
};
inline constexpr int kKeyCount = 512;

// One frame's input, rebuilt by `poll_input`. `held` is the state after this frame's events;
// `pressed` is true for a key that went down during this frame.
struct InputSnapshot {
  std::array<bool, kKeyCount> held{};
  std::array<bool, kKeyCount> pressed{};
  float pointer_x = 0, pointer_y = 0;  // logical units (WindowDesc), from the top left
  uint32_t pointer_buttons = 0;        // bit 0 left, 1 middle, 2 right
  bool quit = false;                   // the window was closed or the app asked to quit
  bool focus_lost = false;             // the window lost focus this frame: pause the run clock
  bool focus_gained = false;           // it came back: resume
  bool resized = false;                // the pixel size changed: reconfigure the surface
  int width_px = 0, height_px = 0;     // the drawable size in pixels, after this frame's events

  bool down(Key k) const { return held[static_cast<uint8_t>(k)]; }
  bool tapped(Key k) const { return pressed[static_cast<uint8_t>(k)]; }
};

struct WindowDesc {
  std::string_view title = "gameengine2";
  int width = 1280;  // logical size; the pixel size may be larger on a high-density display
  int height = 720;
  bool resizable = true;
};

// Whether this process can open a window (a display server, a desktop session). False in a
// container with no display: a test that needs a window skips.
bool display_available();

class Window {
 public:
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  NativeWindow native() const;
  // The drawable size in pixels (what a surface is configured to).
  void pixel_size(int& width, int& height) const;

  struct Impl;

 private:
  explicit Window(std::unique_ptr<Impl> impl);
  friend std::unique_ptr<Window> open_window(const WindowDesc& desc);
  friend void poll_input(Window& window, InputSnapshot& input);
  friend void set_relative_mouse(Window& window, bool on);
  friend bool relative_mouse(const Window& window);
  friend void set_text_input(Window& window, bool on);
  std::unique_ptr<Impl> impl_;
};

// Open a window. Returns nullptr after an agent-actionable error line when it cannot.
std::unique_ptr<Window> open_window(const WindowDesc& desc);

// Read every pending event, once per frame on the main thread, into `input` (its `pressed`
// and per-frame flags are cleared first; `held` carries over).
void poll_input(Window& window, InputSnapshot& input);

// A monotonic clock, ns. For the frame's delta only: the simulation never sees it.
uint64_t now_ns();

// The player's own directory for what a game keeps between sessions (the bindings file, the
// runs), made when missing and ending in a separator: the platform's per-user place for an app's
// data under `org` and `app`. Empty when there is none (then a game keeps nothing, or asks for a
// path).
std::string prefs_dir(std::string_view org, std::string_view app);

}  // namespace ge::platform
