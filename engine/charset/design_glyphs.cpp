// The skeletons themselves: one source string a code in design.hpp's language, for the UI
// symbols (1..31), printable ASCII (32..126) and the missing glyph (127). Capitals and digits
// stand 0..100 between x 22 and 78 (M and W 14..86), lower case 0..74 with ascenders to 108
// and descenders to -32, the box-drawing symbols through the cell's centre (y 44) to its edges.
// Code 10 stays empty: a run of text reads it as a newline.
// A change here moves every drawn font's atlas: rewrite them (tests/charset_charset_test.cpp,
// GE_GOLDEN_WRITE) and look at the `charsets` lab's sheets.
//
// ge:summary The glyph skeletons: a design string for every symbol and printable ASCII code
// ge:keywords glyph design skeleton ascii letters digits punctuation symbols box-drawing
// ge:keywords design_source
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#include "charset/design.hpp"

namespace ge::charset {
namespace {

// A glyph's source may run over several lines as adjacent literals, which is not a missing
// comma.
// NOLINTBEGIN(bugprone-suspicious-missing-comma)
constexpr const char* kSymbols[32] = {
    "",
    // 1 block, 2 underline
    "F 4 14 -32 86 -32 86 108 14 108",
    "F 4 14 -32 86 -32 86 -18 14 -18",
    // 3..6 arrows
    "F 3 22 50 74 84 74 16",
    "F 3 78 50 26 84 26 16",
    "F 3 50 88 16 26 84 26",
    "F 3 50 12 16 74 84 74",
    // 7 bullet, 8 box, 9 box checked
    "E 50 50 15 15",
    "M 20 16 L 80 16 L 80 84 L 20 84 Z",
    "M 20 16 L 80 16 L 80 84 L 20 84 Z M 32 52 L 46 32 L 70 72",
    // 10 is the newline: no glyph
    "",
    // 11 cross, 12 tick
    "M 28 72 L 72 28 M 72 72 L 28 28",
    "M 22 48 L 42 20 L 80 84",
    // 13..16 slider: left end, track, right end, knob
    "M 100 66 L 36 66 L 36 34 L 100 34",
    "M 0 66 L 100 66 M 0 34 L 100 34",
    "M 0 66 L 64 66 L 64 34 L 0 34",
    "F 4 30 8 70 8 70 92 30 92",
    // 17..23 box drawing
    "M 0 44 L 100 44",
    "M 50 -56 L 50 144",
    "M 100 44 L 50 44 L 50 -56",
    "M 0 44 L 50 44 L 50 -56",
    "M 50 144 L 50 44 L 100 44",
    "M 50 144 L 50 44 L 0 44",
    "M 0 44 L 100 44 M 50 -56 L 50 144",
    // 24, 25 shades (drawn by the dither, not a path)
    "",
    "",
    // 26 degree
    "O 50 86 12 13",
    // 27 star: five points and the pentagon between them
    "F 3 50 86 41.8 61.3 58.2 61.3 F 3 17.7 60.5 36.7 45.7 41.8 61.3 "
    "F 3 30 22.5 50 36 36.7 45.7 F 3 70 22.5 63.3 45.7 50 36 "
    "F 3 82.3 60.5 58.2 61.3 63.3 45.7 "
    "F 5 58.2 61.3 41.8 61.3 36.7 45.7 50 36 63.3 45.7",
    // 28 heart, 29 diamond
    "E 36 64 16 16 E 64 64 16 16 F 4 21 58 79 58 64 34 50 16 F 3 21 58 50 16 36 34",
    "F 4 50 92 82 50 50 8 18 50",
    // 30 radio, 31 radio on
    "O 50 50 24 24",
    "O 50 50 24 24 E 50 50 11 11",
};

constexpr const char* kAscii[95] = {
    "",                                                                            // space
    "M 50 100 L 50 30 D 50 6",                                                     // !
    "M 38 102 L 38 74 M 62 102 L 62 74",                                           // "
    "M 40 100 L 34 0 M 66 100 L 60 0 M 20 66 L 80 66 M 20 34 L 80 34",             // #
    "A 50 68 23 18 25 270 A 50 32 25 18 90 -155 M 50 100 L 50 0",                  // $
    "O 30 78 11 17 O 70 22 11 17 M 78 100 L 22 0",                                 // %
    "M 80 0 L 36 66 A 46 80 13 17 220 -40 L 24 30 A 44 24 20 24 165 330 L 78 42",  // &
    "M 50 102 L 50 74",                                                            // '
    "A 90 46 46 70 120 240",                                                       // (
    "A 10 46 46 70 60 -60",                                                        // )
    "M 50 96 L 50 44 M 28 84 L 72 56 M 72 84 L 28 56",                             // *
    "M 50 78 L 50 22 M 22 50 L 78 50",                                             // +
    "M 54 12 L 42 -22",                                                            // ,
    "M 28 46 L 72 46",                                                             // -
    "D 50 6",                                                                      // .
    "M 74 106 L 26 -12",                                                           // /
    "O 50 50 27 50",                                                // 0 (its mark is the variant's)
    "M 30 80 L 52 100 L 52 0 M 30 0 L 74 0",                        // 1
    "A 50 73 25 27 160 -35 L 24 0 L 78 0",                          // 2
    "A 50 77 24 23 155 -90 A 50 27 27 27 90 -155 M 40 54 L 50 54",  // 3
    "M 64 0 L 64 100 L 20 30 L 82 30",                              // 4
    "M 74 100 L 30 100 L 28 56 A 50 32 27 32 135 -150",             // 5
    "O 50 31 27 31 A 60 31 37 68 78 180",                           // 6
    "M 22 100 L 78 100 L 40 0",                                     // 7
    "O 50 77 23 23 O 50 27 27 27",                                  // 8
    "O 50 69 27 31 A 40 69 37 68 0 -102",                           // 9
    "D 50 70 D 50 6",                                               // :
    "D 50 70 M 54 12 L 42 -22",                                     // ;
    "M 76 86 L 26 50 L 76 14",                                      // <
    "M 24 62 L 76 62 M 24 34 L 76 34",                              // =
    "M 24 86 L 74 50 L 24 14",                                      // >
    "A 50 76 24 24 160 -55 L 50 42 L 50 30 D 50 6",                 // ?
    "O 52 48 12 18 M 64 66 L 64 36 A 72 36 8 10 180 360 A 50 50 30 50 0 290",  // @
    "M 20 0 L 50 100 L 80 0 M 31 36 L 69 36",                                  // A
    "M 24 0 L 24 100 L 54 100 A 54 76 20 24 90 -90 L 24 52 M 24 52 L 56 52 "
    "A 56 26 22 26 90 -90 L 24 0",                                              // B
    "A 52 50 28 50 42 318",                                                     // C
    "M 24 0 L 24 100 L 44 100 A 44 50 34 50 90 -90 L 24 0",                     // D
    "M 76 100 L 24 100 L 24 0 L 76 0 M 24 52 L 68 52",                          // E
    "M 76 100 L 24 100 L 24 0 M 24 52 L 68 52",                                 // F
    "A 52 50 28 50 42 340 L 79 46 L 56 46",                                     // G
    "M 22 0 L 22 100 M 78 0 L 78 100 M 22 52 L 78 52",                          // H
    "M 30 100 L 70 100 M 50 100 L 50 0 M 30 0 L 70 0",                          // I
    "M 36 100 L 72 100 M 64 100 L 64 30 A 44 30 20 30 0 -165",                  // J
    "M 24 0 L 24 100 M 76 100 L 24 38 M 40 57 L 78 0",                          // K
    "M 24 100 L 24 0 L 76 0",                                                   // L
    "M 16 0 L 16 100 L 50 36 L 84 100 L 84 0",                                  // M
    "M 22 0 L 22 100 L 78 0 L 78 100",                                          // N
    "O 50 50 28 50",                                                            // O
    "M 24 0 L 24 100 L 54 100 A 54 74 24 26 90 -90 L 24 48",                    // P
    "O 50 50 28 50 M 56 22 L 80 -12",                                           // Q
    "M 24 0 L 24 100 L 54 100 A 54 75 22 25 90 -90 L 24 50 M 50 50 L 78 0",     // R
    "A 50 76 25 24 25 270 A 50 26 27 26 90 -155",                               // S
    "M 20 100 L 80 100 M 50 100 L 50 0",                                        // T
    "M 22 100 L 22 34 A 50 34 28 34 180 360 L 78 100",                          // U
    "M 20 100 L 50 0 L 80 100",                                                 // V
    "M 14 100 L 30 0 L 50 66 L 70 0 L 86 100",                                  // W
    "M 22 100 L 78 0 M 78 100 L 22 0",                                          // X
    "M 20 100 L 50 50 L 80 100 M 50 50 L 50 0",                                 // Y
    "M 24 100 L 76 100 L 24 0 L 78 0",                                          // Z
    "M 66 108 L 40 108 L 40 -18 L 66 -18",                                      // [
    "M 26 106 L 74 -12",                                                        // backslash
    "M 34 108 L 60 108 L 60 -18 L 34 -18",                                      // ]
    "M 26 66 L 50 100 L 74 66",                                                 // ^
    "M 14 -26 L 86 -26",                                                        // _
    "M 40 106 L 58 86",                                                         // `
    "M 75 0 L 75 54 A 51 54 24 20 0 150 M 75 44 L 50 44 A 50 22 25 22 90 350",  // a
    "M 24 108 L 24 0 O 50 37 26 37",                                            // b
    "A 52 37 27 37 45 315",                                                     // c
    "M 76 108 L 76 0 O 50 37 26 37",                                            // d
    "M 23 38 L 77 38 A 50 37 27 37 2 318",                                      // e
    "M 42 0 L 42 86 A 62 86 20 20 180 60 M 22 74 L 74 74",                      // f
    "O 48 40 25 34 M 74 74 L 74 -4 A 50 -4 24 26 0 -160",                       // g
    "M 24 108 L 24 0 M 24 52 A 50 48 26 26 165 0 L 76 0",                       // h
    "M 50 74 L 50 0 M 32 74 L 50 74 M 30 0 L 70 0 D 50 100",                    // i
    "D 58 100 M 38 74 L 58 74 L 58 -6 A 38 -6 20 26 0 -150",                    // j
    "M 26 108 L 26 0 M 74 74 L 26 26 M 44 44 L 76 0",                           // k
    "M 30 108 L 50 108 L 50 0 M 28 0 L 72 0",                                   // l
    "M 18 0 L 18 74 M 18 54 A 34 54 16 20 170 0 L 50 0 M 50 54 A 66 54 16 20 180 0 L 82 0",  // m
    "M 24 0 L 24 74 M 24 52 A 50 48 26 26 165 0 L 76 0",                                     // n
    "O 50 37 27 37",                                                                         // o
    "M 24 74 L 24 -32 O 50 37 26 37",                                                        // p
    "M 76 74 L 76 -32 O 50 37 26 37",                                                        // q
    "M 28 0 L 28 74 M 28 42 A 58 42 30 32 180 55",                                           // r
    "A 50 56 22 18 20 270 A 50 19 25 19 90 -160",                                            // s
    "M 22 74 L 74 74 M 42 100 L 42 22 A 62 22 20 22 180 300",                                // t
    "M 24 74 L 24 28 A 50 28 26 28 180 360 M 76 74 L 76 0",                                  // u
    "M 22 74 L 50 0 L 78 74",                                                                // v
    "M 14 74 L 32 0 L 50 54 L 68 0 L 86 74",                                                 // w
    "M 24 74 L 76 0 M 76 74 L 24 0",                                                         // x
    "M 22 74 L 52 2 M 78 74 L 40 -32 L 28 -32",                                              // y
    "M 26 74 L 74 74 L 26 0 L 76 0",                                                         // z
    "A 66 92 14 16 90 180 L 52 60 A 38 60 14 14 0 -90 A 38 32 14 14 90 0 L 52 -2 "
    "A 66 -2 14 16 180 270",  // {
    "M 50 112 L 50 -24",      // |
    "A 34 92 14 16 90 0 L 48 60 A 62 60 14 14 180 270 A 62 32 14 14 90 180 L 48 -2 "
    "A 34 -2 14 16 0 -90",                        // }
    "A 38 46 12 10 180 0 A 62 46 12 10 180 360",  // ~
};

// NOLINTEND(bugprone-suspicious-missing-comma)

constexpr const char* kMissingSource =
    "M 18 -24 L 82 -24 L 82 106 L 18 106 Z M 18 -24 L 82 106 M 82 -24 L 18 106";

}  // namespace

std::string_view design_source(uint8_t code, const Variant& variant) {
  if (code < 32) return kSymbols[code];
  if (code == 127) return kMissingSource;  // the missing glyph
  if (code > 126) return "";
  if (code == '0') {
    switch (variant.zero) {
      case Zero::plain:
        return "O 50 50 27 50";
      case Zero::slashed:
        return "O 50 50 27 50 M 36 22 L 64 78";
      case Zero::dotted:
        return "O 50 50 27 50 D 50 50";
    }
  }
  if (code == 'a' && variant.single_storey_a) return "O 48 37 26 37 M 75 74 L 75 0";
  return kAscii[code - 32];
}

}  // namespace ge::charset
