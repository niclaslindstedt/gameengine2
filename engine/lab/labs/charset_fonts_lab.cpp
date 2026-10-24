// LAB `charsets`: what each of the engine's ten bitmap fonts (engine/charset/charset.hpp) looks
// like: its whole atlas as committed, and a specimen of it in use at the sizes a game draws
// text at (a pangram, the digits and punctuation, the UI symbols, a run of sizes), plus one
// sheet with every font side by side. The numbers are each atlas's ink and its packed size at
// 256, 512 and 1024 px, and how long the builder takes.
//
// ge:summary Lab: every charset's atlas and a specimen of it in use, side by side
// ge:keywords lab charset font atlas specimen pangram sizes glyphs symbols sheet
// ge:skill labs ui
// ge:test tests/charset_charset_test.cpp
#include <chrono>
#include <cstdio>
#include <string>

#include "charset/charset.hpp"
#include "charset/effects.hpp"
#include "charset/text.hpp"
#include "lab/lab.hpp"
#include "lab/png.hpp"

namespace {

namespace cs = ge::charset;
using ge::lab::Det;
using ge::lab::Picture;

constexpr ge::lab::Rgba kPaper{24, 27, 34, 255};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
  return s;
}

std::vector<const cs::FontInfo*> chosen(ge::lab::Lab& lab) {
  std::vector<const cs::FontInfo*> out;
  const std::string& list = lab.arg("fonts");
  if (list == "all") {
    for (const cs::FontInfo& f : cs::fonts()) out.push_back(&f);
    return out;
  }
  size_t i = 0;
  while (i <= list.size()) {
    size_t j = list.find(',', i);
    if (j == std::string::npos) j = list.size();
    const std::string name = list.substr(i, j - i);
    if (const cs::FontInfo* f = cs::find_font(name)) {
      out.push_back(f);
    } else if (!name.empty()) {
      lab.fail("no font named " + name, "pick from `make lab LAB=charsets ARGS=--help`'s list");
    }
    i = j + 1;
  }
  return out;
}

// Text in a font: plain light ink, drawn from the atlas that suits the size.
struct Writer {
  std::vector<cs::Atlas> atlases[cs::kFontCount];
  cs::LayerCache layers;
  const cs::Atlas& atlas(cs::FontId f, double cell_h) {
    auto& list = atlases[static_cast<int>(f)];
    const int size = cs::atlas_size_for(cell_h);
    for (const cs::Atlas& a : list) {
      if (a.size == size) return a;
    }
    list.push_back(cs::build_atlas(f, size));
    return list.back();
  }
  void write(Picture& p, cs::FontId f, const std::string& text, double x, double y, double cell_h,
             const cs::Look& look = cs::look(cs::LookId::plain)) {
    cs::TextPlace place;
    place.x = x;
    place.y = y;
    place.cell_h = cell_h;
    cs::paint_text(p.rgba.data(), p.width, p.height, atlas(f, cell_h), text, place, look, layers);
  }
};

Picture atlas_picture(const cs::Atlas& a) {
  Picture p = ge::lab::create_picture(a.size, a.size, {0, 0, 0, 255});
  for (int y = 0; y < a.size; ++y) {
    for (int x = 0; x < a.size; ++x) {
      const uint8_t v = a.at(x, y);
      ge::lab::set_pixel(p, x, y, {v, v, v, 255});
    }
  }
  // The cell grid, faint, so a glyph's place and its margins read.
  for (int c = 0; c <= cs::kColumns; ++c) {
    const double x = c * a.cell_w() - (c == cs::kColumns ? 1 : 0);
    ge::lab::draw_line(p, x, 0, x, a.size - 1, {60, 110, 200, 110});
  }
  for (int r = 0; r <= cs::kRows; ++r) {
    const double y = r * a.cell_h() - (r == cs::kRows ? 1 : 0);
    ge::lab::draw_line(p, 0, y, a.size - 1, y, {60, 110, 200, 110});
  }
  return p;
}

void run(ge::lab::Lab& lab) {
  const int size = static_cast<int>(lab.arg_num("size"));
  const std::vector<const cs::FontInfo*> list = chosen(lab);
  Writer w;
  ge::lab::LabTable table{
      "fonts", {"font", "for", "inspired by", "kind", "png 256", "png 512", "png 1024"}, {}};
  for (const cs::FontInfo* f : list) {
    const auto t0 = std::chrono::steady_clock::now();
    const cs::Atlas a = cs::build_atlas(f->id, size);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double lit = 0;
    for (uint8_t v : a.coverage) lit += v / 255.0;
    const std::string key = f->name;
    lab.metric(key + ".ink", 100.0 * lit / static_cast<double>(a.coverage.size()), "%");
    lab.metric(key + ".build-" + std::to_string(size), ms, "ms", Det::measured);
    std::vector<std::string> row{f->name, f->look, f->inspired, f->pixel ? "pixel" : "drawn"};
    for (const int s : cs::kAtlasSizes) {
      const size_t bytes =
          s == size ? ge::lab::encode_png_grey(s, s, a.coverage).size()
                    : ge::lab::encode_png_grey(s, s, cs::build_atlas(f->id, s).coverage).size();
      lab.metric(key + ".png-" + std::to_string(s), static_cast<double>(bytes) / 1024.0, "KiB");
      row.push_back(std::to_string((bytes + 512) / 1024) + " KiB");
    }
    table.rows.push_back(row);

    lab.shot(
        {key + "-atlas",
         upper(key) + " ATLAS, " + std::to_string(size) + " PX",
         "The committed atlas (" + cs::atlas_path(f->id, size) +
             "): 16 x 8 cells of 1:2, "
             "code c at column c % 16 and row c / 16. Row 0-1: the UI symbols (cursor, arrows, "
             "checkbox, slider, box lines, shades, degree, star...); rows 2-7: ASCII 32-126; "
             "the last cell: the missing glyph. Blue: the cell grid.",
         "every cell but the space and code 0 has a glyph; no glyph touches its cell's edge "
         "but the box lines and shades (they tile); one design across all 128",
         {{"font", key}, {"size", std::to_string(size)}}},
        atlas_picture(a));

    Picture p = ge::lab::create_picture(1280, 720, kPaper);
    w.write(p, f->id, upper(key), 40, 24, 96);
    w.write(p, f->id, std::string(f->look), 40, 130, 28);
    w.write(p, f->id, "The quick brown fox jumps over the lazy dog.", 40, 190, 48);
    w.write(p, f->id, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG!", 40, 250, 48);
    w.write(p, f->id, "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", 40, 320, 48);
    std::string syms;
    for (int c = 1; c < 32; ++c) {
      if (c != '\n') syms += static_cast<char>(c);
    }
    syms += static_cast<char>(127);
    w.write(p, f->id, syms, 40, 390, 48);
    double y = 470;
    for (const double h : {16.0, 24.0, 32.0, 48.0, 64.0}) {
      w.write(p, f->id, std::to_string(static_cast<int>(h)) + "px Fall Line 2:41.07", 40, y, h);
      y += h + 8;
    }
    // A settings row, a framed button and a readout, set from the UI symbols.
    w.write(p, f->id, "\x08 SOUND  \x09 MUSIC", 800, 470, 32);
    w.write(p, f->id, "\x0d\x0e\x0e\x10\x0e\x0e\x0e\x0f", 800, 510, 32);
    w.write(p, f->id,
            "\x13\x11\x11\x11\x11\x11\x11\x11\x14\n\x12 \x03 OK \x04\x12\n"
            "\x15\x11\x11\x11\x11\x11\x11\x11\x16",
            800, 552, 32);
    w.write(p, f->id,
            "-12\x1a"
            "C \x1b\x1b\x1b\x0c \x1e\x1f\x0b",
            800, 660, 32);
    lab.shot(
        {key + "-specimen",
         upper(key) + " IN USE",
         std::string(
             "The font at work: its name at 96 px, a pangram in both cases at 48 px, the "
             "digits and punctuation, the UI symbols, a run of sizes from 16 to 64 px, and "
             "a checkbox, a slider, a box and a degree sign set from the symbols. Inspired by ") +
             f->inspired + "; every glyph drawn here.",
         "legible at 16 px; even spacing on the monospace grid; the box lines join into a "
         "frame; no glyph clipped or reading a neighbour",
         {{"font", key}, {"atlases", "by size (atlas_size_for)"}}},
        p);
  }
  lab.table(table);

  if (list.size() > 1) {
    Picture p = ge::lab::create_picture(1280, 40 + 66 * static_cast<int>(list.size()), kPaper);
    double y = 24;
    for (const cs::FontInfo* f : list) {
      w.write(p, f->id, upper(f->name), 30, y, 48);
      w.write(p, f->id, "Fall Line 0123 Ag &?!", 380, y, 48);
      y += 66;
    }
    lab.shot({"overview",
              "EVERY FONT",
              "Each font's name and a sample in itself at 48 px.",
              "ten different looks; all legible at this size",
              {{"fonts", lab.arg("fonts")}}},
             p);
  }
}

const ge::lab::LabSpec kSpec{
    "charsets",
    ge::lab::LabKind::picture,
    "engine",
    "engine/charset/charset.hpp",
    "ui",
    "What does each of the engine's bitmap fonts look like, as an atlas and in use?",
    {
        {"fonts", "all", "the fonts, comma-separated, or all"},
        {"size", "512", "the atlas drawn whole, px (256, 512 or 1024)"},
    },
    "--fonts=mono,terminal,segment --size=256",
    run,
};
GE_LAB(kSpec);

}  // namespace
