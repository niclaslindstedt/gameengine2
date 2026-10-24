// The window over SDL3: the video subsystem's lifetime, the native handles by video driver,
// and the event pump. This file and nothing else includes SDL.
//
// ge:summary The window, input and clock implemented on SDL3 (the only file including SDL)
// ge:keywords sdl sdl3 video-driver event-pump scancode x11 wayland cocoa metal-view win32
// ge:skill platform
// ge:test tests/platform_window_test.cpp
#include "platform/window.hpp"

#include <SDL3/SDL.h>

#include "platform/devices_sdl.hpp"

#include <string>

#include "core/output.hpp"

namespace ge::platform {

namespace out = core::output;

struct Window::Impl {
  SDL_Window* window = nullptr;
  SDL_MetalView metal_view = nullptr;
};

bool display_available() {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return true;
}

Window::Window(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Window::~Window() {
  if (impl_->metal_view) SDL_Metal_DestroyView(impl_->metal_view);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

std::unique_ptr<Window> open_window(const WindowDesc& desc) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    out::error(std::string("platform: no display to open a window on (") + SDL_GetError() +
               "). A headless test or lab must not open one; for a window in a container run "
               "under a virtual display (xvfb-run -a <cmd>); skill: platform.");
    return nullptr;
  }
  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
  flags |= SDL_WINDOW_METAL;
#endif
  const std::string title(desc.title);
  auto impl = std::make_unique<Window::Impl>();
  impl->window = SDL_CreateWindow(title.c_str(), desc.width, desc.height, flags);
  if (!impl->window) {
    out::error(std::string("platform: SDL_CreateWindow failed (") + SDL_GetError() +
               "); skill: platform.");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return nullptr;
  }
#if defined(__APPLE__)
  impl->metal_view = SDL_Metal_CreateView(impl->window);
#endif
  return std::unique_ptr<Window>(new Window(std::move(impl)));
}

NativeWindow Window::native() const {
  NativeWindow n;
  const SDL_PropertiesID props = SDL_GetWindowProperties(impl_->window);
  const std::string driver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
  if (impl_->metal_view) {
    n.kind = NativeWindow::Kind::metal;
    n.metal_layer = SDL_Metal_GetLayer(impl_->metal_view);
  } else if (driver == "x11") {
    n.kind = NativeWindow::Kind::x11;
    n.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    n.x11_window =
        static_cast<uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
  } else if (driver == "wayland") {
    n.kind = NativeWindow::Kind::wayland;
    n.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    n.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
  } else if (driver == "windows") {
    n.kind = NativeWindow::Kind::win32;
    n.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    n.instance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
  } else {
    out::error("platform: the video driver '" + driver +
               "' has no surface handles the GPU knows; add its case to Window::native and to "
               "engine/gpu/surface.cpp; skill: platform.");
  }
  return n;
}

void Window::pixel_size(int& width, int& height) const {
  if (!SDL_GetWindowSizeInPixels(impl_->window, &width, &height)) width = height = 0;
}

namespace {

void pump(Window& window, InputSnapshot& in, Devices* devices, input::Queue* queue) {
  in.pressed.fill(false);
  in.quit = in.focus_lost = in.focus_gained = in.resized = false;
  if (devices) {
    devices_begin(*devices, *queue);
    SDL_UpdateJoysticks();
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (devices) devices_event(*devices, e, *queue);
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        in.quit = true;
        break;
      case SDL_EVENT_KEY_DOWN:
        if (e.key.scancode < kKeyCount) {
          if (!e.key.repeat) in.pressed[e.key.scancode] = true;
          in.held[e.key.scancode] = true;
        }
        break;
      case SDL_EVENT_KEY_UP:
        if (e.key.scancode < kKeyCount) in.held[e.key.scancode] = false;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        in.pointer_x = e.motion.x;
        in.pointer_y = e.motion.y;
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const uint32_t bit = e.button.button == SDL_BUTTON_LEFT     ? 1u
                             : e.button.button == SDL_BUTTON_MIDDLE ? 2u
                             : e.button.button == SDL_BUTTON_RIGHT  ? 4u
                                                                    : 0u;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          in.pointer_buttons |= bit;
        } else {
          in.pointer_buttons &= ~bit;
        }
        break;
      }
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        in.focus_lost = true;
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        in.focus_gained = true;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        in.resized = true;
        break;
      default:
        break;
    }
  }
  window.pixel_size(in.width_px, in.height_px);
}

}  // namespace

void poll_input(Window& window, InputSnapshot& in) { pump(window, in, nullptr, nullptr); }

void poll_input(Window& window, InputSnapshot& in, Devices& devices, input::Queue& queue) {
  pump(window, in, &devices, &queue);
}

void set_relative_mouse(Window& window, bool on) {
  SDL_SetWindowRelativeMouseMode(window.impl_->window, on);
}

bool relative_mouse(const Window& window) {
  return SDL_GetWindowRelativeMouseMode(window.impl_->window);
}

void set_text_input(Window& window, bool on) {
  if (on) {
    SDL_StartTextInput(window.impl_->window);
  } else {
    SDL_StopTextInput(window.impl_->window);
  }
}

uint64_t now_ns() { return SDL_GetTicksNS(); }

std::string prefs_dir(std::string_view org, std::string_view app) {
  char* path = SDL_GetPrefPath(std::string(org).c_str(), std::string(app).c_str());
  if (!path) return {};
  std::string dir = path;
  SDL_free(path);
  return dir;
}

}  // namespace ge::platform
