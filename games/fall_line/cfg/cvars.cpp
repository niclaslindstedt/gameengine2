// Fall Line's cvars, registered: each name, default, range, flags and help.
//
// ge:summary Fall Line's cvar table: names, defaults, ranges, flags and help, bound to Settings
// ge:keywords fall-line cvars add_cvars register_cvars
// ge:skill console sim
// ge:test tests/fall_line_cvars_test.cpp
#include "fall_line/cfg/cvars.hpp"

#include <memory>

namespace fall_line::cfg {

namespace con = ge::console;

void add_cvars(con::Console& c, Settings& s) {
  using con::CvarKind;
  constexpr uint32_t kPlayer = con::kArchive;
  constexpr uint32_t kStartup = con::kArchive | con::kLatch;
  constexpr uint32_t kRun = con::kServer | con::kServerInfo;
  con::Cvars& v = c.cvars();

  v.bind({.name = "vid_width",
          .fallback = "1280",
          .flags = kStartup,
          .help = "the window's width, logical pixels",
          .kind = CvarKind::integer,
          .min = 320,
          .max = 7680},
         &s.width);
  v.bind({.name = "vid_height",
          .fallback = "720",
          .flags = kStartup,
          .help = "the window's height, logical pixels",
          .kind = CvarKind::integer,
          .min = 240,
          .max = 4320},
         &s.height);
  v.bind({.name = "vid_vsync",
          .fallback = "1",
          .flags = kStartup,
          .help = "wait for the display's refresh before showing a frame",
          .kind = CvarKind::boolean},
         &s.vsync);

  v.bind({.name = "r_taa",
          .fallback = "1",
          .flags = kStartup,
          .help = "temporal anti-aliasing and upscaling (off: no jitter, no resolve)",
          .kind = CvarKind::boolean},
         &s.taa);
  v.bind({.name = "r_scale",
          .fallback = "1",
          .flags = kStartup,
          .help = "the render size over the window's, with r_taa (0.5: a quarter of the pixels)",
          .min = 0.25,
          .max = 2},
         &s.scale);
  v.bind({.name = "r_occlusion",
          .fallback = "1",
          .flags = kStartup,
          .help = "cull trees and figures hidden behind the terrain (off: the frustum only)",
          .kind = CvarKind::boolean},
         &s.occlusion);
  v.bind({.name = "r_shadowsize",
          .fallback = "2048",
          .flags = kStartup,
          .help = "the sun's shadow maps' texels a side",
          .kind = CvarKind::integer,
          .min = 256,
          .max = 8192},
         &s.shadow_size);
  v.bind({.name = "r_bloom",
          .fallback = "0.04",
          .flags = kStartup,
          .help = "the share of the glow round bright light in the picture; 0: none",
          .min = 0,
          .max = 1},
         &s.bloom);
  v.bind({.name = "r_snow",
          .fallback = "1",
          .flags = kStartup,
          .help = "the snow's material: sparkle, sheen and grooming (off: plain lit white)",
          .kind = CvarKind::boolean},
         &s.snow);
  v.bind({.name = "r_trails",
          .fallback = "1",
          .flags = kStartup,
          .help = "the tracks skis cut in the snow (needs r_snow)",
          .kind = CvarKind::boolean},
         &s.trails);
  v.bind({.name = "r_spray",
          .fallback = "1",
          .flags = kStartup,
          .help = "the snow the skis throw",
          .kind = CvarKind::boolean},
         &s.spray);
  v.bind({.name = "r_snowfall",
          .fallback = "1",
          .flags = kStartup,
          .help = "the falling snow when the sky snows",
          .kind = CvarKind::boolean},
         &s.snowfall);
  v.bind({.name = "r_weather",
          .fallback = "1",
          .flags = kStartup,
          .help = "the map's own sky (off: always clear)",
          .kind = CvarKind::boolean},
         &s.weather);
  v.bind({.name = "r_clouds",
          .fallback = "1",
          .flags = kStartup,
          .help = "the share of the cloud march's steps, 0.25 .. 1 (0: no cloud layer drawn, the "
                  "sky keeps its mean cloud)",
          .min = 0,
          .max = 1},
         &s.clouds);

  v.bind({.name = "cl_view",
          .fallback = "chase",
          .flags = kPlayer,
          .help = "the camera: a rung (tips helmet chase far high orbit) or a still view (start "
                  "piste lookback vista aerial)",
          .kind = CvarKind::text},
         &s.view);

  v.bind({.name = "volume",
          .fallback = "0.8",
          .flags = kPlayer,
          .help = "the master volume, 0 silent .. 1 full",
          .min = 0,
          .max = 1},
         &s.volume);
  v.bind({.name = "s_wind",
          .fallback = "1",
          .flags = kPlayer,
          .help = "the wind and the snow's hiss under the skis, 0 .. 1",
          .min = 0,
          .max = 1},
         &s.wind);
  v.bind({.name = "s_effects",
          .fallback = "1",
          .flags = kPlayer,
          .help = "every other sound: edges, landings, gates, crashes, 0 .. 1",
          .min = 0,
          .max = 1},
         &s.effects);
  v.bind({.name = "s_mute",
          .fallback = "0",
          .flags = kPlayer,
          .help = "no sound at all, and no audio device opened",
          .kind = CvarKind::boolean},
         &s.mute);

  v.bind({.name = "sv_seed",
          .fallback = "38",
          .flags = kRun,
          .help = "the seed the mountain is generated from"},
         &s.seed);
  v.bind({.name = "sv_hour",
          .fallback = "-1",
          .flags = kRun,
          .help = "the hour of the day the run starts at, 0 .. 24 (-1: the map's own)",
          .min = -1,
          .max = 24},
         &s.hour);
  v.bind({.name = "sv_skyrate",
          .fallback = "0",
          .flags = kRun,
          .help = "the sky's hours per hour of the run: 1 real time, 60 an hour a minute (0: the "
                  "hour stands)",
          .min = 0,
          .max = 3600},
         &s.sky_rate);
  v.bind({.name = "sv_weather",
          .fallback = "map",
          .flags = kRun,
          .help = "the sky: clear fair flurries high overcast snow storm fog (map: the map's own)",
          .kind = CvarKind::text},
         &s.sky);
  v.bind({.name = "sv_snowfall",
          .fallback = "-1",
          .flags = kRun,
          .help = "how hard it snows, 0 none .. 1 a blizzard, on a sky that snows (-1: the sky's)",
          .min = -1,
          .max = 1},
         &s.fall);
  v.bind({.name = "sv_fog",
          .fallback = "-1",
          .flags = kRun,
          .help = "how thick the valley fog lies, 0 .. 1, under a fog (-1: the sky's)",
          .min = -1,
          .max = 1},
         &s.fog);
  v.bind({.name = "sv_wind",
          .fallback = "-1",
          .flags = kRun,
          .help = "the mean wind at 10 m, m/s (-1: the sky's)",
          .min = -1,
          .max = 40},
         &s.air);
  v.bind({.name = "sv_winddir",
          .fallback = "-1",
          .flags = kRun,
          .help = "the heading the wind blows from, degrees clockwise from the map's +z (-1: the "
                  "map's)",
          .min = -1,
          .max = 360},
         &s.air_from);
  v.bind({.name = "sv_cover",
          .fallback = "-1",
          .flags = kRun,
          .help = "a broken sky's cloud cover (fair, flurries, high), 0 .. 1 (-1: the map's)",
          .min = -1,
          .max = 1},
         &s.cover);

  v.bind({.name = "sv_lethal",
          .fallback = "1",
          .flags = kRun,
          .help = "the army shoots to kill (0: it chases him down and arrests him)",
          .kind = CvarKind::boolean},
         &s.lethal);
  v.bind({.name = "sv_armyfire",
          .fallback = "1",
          .flags = kRun,
          .help = "the army's guns fire at all (0: it hunts with its guns silent)",
          .kind = CvarKind::boolean},
         &s.army_fire);
  v.bind({.name = "sv_helis",
          .fallback = "1",
          .flags = kRun,
          .help = "the hunt sends the army's helicopters and the men they put down",
          .kind = CvarKind::boolean},
         &s.helis);
  v.bind({.name = "sv_paras",
          .fallback = "1",
          .flags = kRun,
          .help = "the hunt sends the army's paramotor riflemen (from four stars)",
          .kind = CvarKind::boolean},
         &s.paras);
}

void register_cvars(con::Console& c) {
  auto s = std::make_shared<Settings>();
  add_cvars(c, *s);
  c.keep(s);
}

}  // namespace fall_line::cfg
