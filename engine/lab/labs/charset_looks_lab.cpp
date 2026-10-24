// LAB `charset-looks`: the atmosphere a game lays over its text (engine/charset/layers.hpp,
// effects.hpp): every noise layer as a swatch, every look on the font it was made for, a grid
// of every font under every look, and a mock game screen that uses them the way a game would
// (a HUD over snow, a timer, a chat, a drop-down console, a title, a menu, a sign, a note).
//
// ge:summary Lab: the noise layers, every look on every font, and a mock screen using them
// ge:keywords lab charset look effects layers noise glow outline bevel shadow erosion mock hud
// ge:skill labs ui
// ge:test tests/charset_effects_test.cpp
#include <string>

#include "charset/charset.hpp"
#include "charset/effects.hpp"
#include "charset/layers.hpp"
#include "charset/text.hpp"
#include "lab/lab.hpp"

namespace {

namespace cs = ge::charset;
using cs::FontId;
using cs::LookId;
using ge::lab::Picture;

struct Writer {
  std::vector<cs::Atlas> atlases[cs::kFontCount];
  cs::LayerCache layers;
  const cs::Atlas& atlas(FontId f, double cell_h) {
    auto& list = atlases[static_cast<int>(f)];
    const int size = cs::atlas_size_for(cell_h);
    for (const cs::Atlas& a : list) {
      if (a.size == size) return a;
    }
    list.push_back(cs::build_atlas(f, size));
    return list.back();
  }
  void write(Picture& p, FontId f, LookId l, const std::string& text, double x, double y,
             double cell_h, double tracking = 0) {
    cs::TextPlace place;
    place.x = x;
    place.y = y;
    place.cell_h = cell_h;
    place.tracking = tracking;
    cs::paint_text(p.rgba.data(), p.width, p.height, atlas(f, cell_h), text, place, cs::look(l),
                   layers);
  }
};

// The font each look was made for.
FontId font_for(LookId l) {
  switch (l) {
    case LookId::rusted:
      return FontId::gothic;
    case LookId::phosphor:
      return FontId::terminal;
    case LookId::neon:
      return FontId::rounded;
    case LookId::worn:
      return FontId::stencil;
    case LookId::chrome:
      return FontId::square;
    case LookId::carved:
      return FontId::gothic;
    case LookId::amber:
      return FontId::matrix;
    case LookId::outlined:
      return FontId::rounded;
    default:
      return FontId::mono;
  }
}

void gradient(Picture& p, int y0, int y1, ge::lab::Rgba top, ge::lab::Rgba bottom) {
  for (int y = y0; y < y1; ++y) {
    const double t = (y - y0) / static_cast<double>(std::max(1, y1 - y0 - 1));
    const ge::lab::Rgba c{static_cast<uint8_t>(top.r + (bottom.r - top.r) * t),
                          static_cast<uint8_t>(top.g + (bottom.g - top.g) * t),
                          static_cast<uint8_t>(top.b + (bottom.b - top.b) * t), 255};
    ge::lab::fill_rect(p, 0, y, p.width, 1, c);
  }
}

void run(ge::lab::Lab& lab) {
  Writer w;
  const std::string sample = lab.arg("text");

  // THE LAYERS
  {
    const int s = static_cast<int>(lab.arg_num("layer-size"));
    Picture p = ge::lab::create_picture(s * 4 + 50, (s + 44) * 2 + 10, {20, 22, 28, 255});
    int k = 0;
    for (const cs::LayerInfo& li : cs::layers()) {
      const cs::Texture t = cs::make_layer(li.id, s);
      const int ox = 10 + (k % 4) * (s + 10), oy = 10 + (k / 4) * (s + 44);
      double mean = 0;
      for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
          const uint8_t v = t.at(x, y);
          mean += v;
          ge::lab::set_pixel(p, ox + x, oy + y, {v, v, v, 255});
        }
      }
      lab.metric(std::string("layer.") + li.name + ".mean", mean / (double(s) * s), "/255");
      w.write(p, FontId::mono, LookId::plain, li.name, ox, oy + s + 4, 28);
      ++k;
    }
    lab.shot({"layers",
              "THE NOISE LAYERS",
              "Every layer at " + std::to_string(s) +
                  " px, black 0 to white 255, as committed "
                  "(engine/charset/layers/): grain, grunge, scratches, scanlines / rust, cracks, "
                  "halftone, brushed.",
              "each wraps at its edges (no seam if tiled); each reads as what it is named",
              {{"size", std::to_string(s)}, {"seed", "1"}}},
             p);
  }

  // EVERY LOOK ON ITS FONT
  {
    const int row = 96;
    Picture p = ge::lab::create_picture(1280, row * cs::kLookCount + 20, {28, 32, 42, 255});
    for (int i = 0; i < cs::kLookCount; ++i) {
      const auto l = static_cast<LookId>(i);
      const int y = 10 + i * row;
      if (l == LookId::shadow) gradient(p, y, y + row, {200, 220, 240, 255}, {250, 252, 255, 255});
      const cs::Look& lk = cs::look(l);
      w.write(p, font_for(l), l, std::string(lk.name) + "  " + sample, 24, y + 14, 64);
      w.write(p, FontId::mono, LookId::plain, lk.use, 730, y + 34, 18);
    }
    lab.shot({"looks",
              "EVERY LOOK",
              "Each look on the font it was made for, at 64 px: plain, shadow (over a snowy sky), "
              "outlined, rusted, phosphor, neon, worn, chrome, carved, amber. Right: what it is "
              "for.",
              "each look reads at a glance; glows and shadows are soft, not banded; erosion "
              "leaves the word legible; the fills (rust, grain, brushed, cracks) show in the ink",
              {{"text", sample}}},
             p);
  }

  // EVERY FONT UNDER EVERY LOOK
  {
    const int cw = 124, ch = 64;
    Picture p = ge::lab::create_picture(130 + cw * cs::kLookCount, 50 + ch * cs::kFontCount,
                                        {28, 32, 42, 255});
    for (int l = 0; l < cs::kLookCount; ++l) {
      w.write(p, FontId::mono, LookId::plain, cs::looks()[l].name, 130 + l * cw + 4, 14, 20);
    }
    for (int f = 0; f < cs::kFontCount; ++f) {
      const auto font = static_cast<FontId>(f);
      w.write(p, FontId::mono, LookId::plain, cs::fonts()[f].name, 8, 50 + f * ch + 20, 20);
      for (int l = 0; l < cs::kLookCount; ++l) {
        w.write(p, font, static_cast<LookId>(l), "Ag42", 130 + l * cw + 12, 50 + f * ch + 8, 48);
      }
    }
    lab.shot({"grid",
              "EVERY FONT UNDER EVERY LOOK",
              "Rows: the ten fonts; columns: the ten looks; \"Ag42\" at 48 px in each.",
              "every pairing draws; the effects keep their size relative to the text",
              {}},
             p);
  }

  // A GAME'S SCREEN
  {
    Picture p = ge::lab::create_picture(1280, 720, {0, 0, 0, 255});
    gradient(p, 0, 430, {92, 140, 200, 255}, {196, 218, 238, 255});
    gradient(p, 430, 720, {236, 242, 248, 255}, {206, 216, 228, 255});
    // The console, dropped down over the top third.
    ge::lab::fill_rect(p, 0, 0, 1280, 220, {8, 14, 10, 235});
    w.write(p, FontId::terminal, LookId::phosphor,
            "] exec autoexec.cfg\nexecing autoexec.cfg\n] sv_gravity 800\n"
            "\"sv_gravity\" is \"800\"\n] say_prox anyone at the lift?\n] _",
            16, 10, 32);
    ge::lab::fill_rect(p, 0, 218, 1280, 2, {60, 200, 90, 255});
    w.write(p, FontId::gothic, LookId::rusted, "FALL LINE", 300, 250, 128, 6);
    w.write(p, FontId::square, LookId::chrome, "\x04 NEW RUN\n  OPTIONS\n  QUIT", 500, 420, 40);
    w.write(p, FontId::rounded, LookId::shadow, "87 km/h", 30, 640, 56);
    w.write(p, FontId::segment, LookId::amber, "01:23.45", 980, 236, 56);
    w.write(p, FontId::matrix, LookId::amber, "GATE 12/27  -0.41", 980, 300, 24);
    w.write(p, FontId::mono, LookId::outlined, "niclas: race you down\nbot_3: \x1b gg", 30, 520,
            28);
    // A painted board for the sign and a slate for the note.
    ge::lab::fill_rect(p, 960, 548, 290, 128, {150, 36, 28, 255});
    w.write(p, FontId::stencil, LookId::worn, "DANGER\nAVALANCHE", 980, 560, 48);
    ge::lab::fill_rect(p, 560, 636, 352, 60, {40, 46, 52, 255});
    w.write(p, FontId::marker, LookId::plain, "meet at the hut!", 576, 646, 40);
    lab.shot({"in-game",
              "THE FONTS AT WORK",
              "A mock screen: the drop-down console (terminal, phosphor), the title (gothic, "
              "rusted), a menu (square, chrome), the speed (rounded, shadow) over snow, the timer "
              "(segment, amber), a split (matrix, amber), the chat (mono, outlined), a sign "
              "(stencil, worn) on a painted board and a note (marker) on a slate.",
              "every line legible against what is behind it; the looks belong together; nothing "
              "clipped at the frame's edge",
              {}},
             p);
  }
}

const ge::lab::LabSpec kSpec{
    "charset-looks",
    ge::lab::LabKind::picture,
    "engine",
    "engine/charset/effects.hpp engine/charset/layers.hpp",
    "ui",
    "What do the noise layers and the text looks do to each font, and how do they read in a game?",
    {
        {"text", "Fall Line 42", "the sample after each look's name"},
        {"layer-size", "256", "the layer swatches' side, px"},
    },
    "--layer-size=64",
    run,
};
GE_LAB(kSpec);

}  // namespace
