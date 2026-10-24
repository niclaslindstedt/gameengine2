// The network's cvars bound to a ReplicateConfig (net/net_cvars.hpp).
//
// ge:summary The network's cvars bound to the replication config, and cl_interp to ticks
// ge:keywords net cvars sv_fps bind replicate config
// ge:skill net
// ge:test tests/net_cvars_test.cpp
#include "net/net_cvars.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ge::net {

namespace con = ge::console;

void add_net_cvars(con::Cvars& v, NetCvars& n) {
  ReplicateConfig& k = *n.config;
  constexpr uint32_t kServerRule = con::kServer | con::kServerInfo;
  constexpr uint32_t kPlayer = con::kArchive | con::kUserInfo;
  // The fallbacks are the config's own values as the game made it, so a game's genre defaults
  // stand until a cfg says otherwise.
  const std::string fps = std::to_string(k.tick_hz);
  const std::string mur = std::to_string(k.max_update_rate);
  const std::string mr = std::to_string(k.max_rate);
  const std::string ur = std::to_string(k.update_rate);
  const std::string cr = std::to_string(k.cmd_rate);
  const std::string r = std::to_string(k.rate);
  const std::string ts = k.sync ? "1" : "0";
  const std::string sm = k.spring && k.cubic ? "1" : "0";
  const std::string ex = std::to_string(k.extrapolate_s);
  const std::string in = std::to_string(n.interp_s);
  const std::string cb = std::to_string(k.redundancy);
  const std::string pw = std::to_string(k.predict_radius);
  const std::string pm = std::to_string(k.predict_max_s);
  v.bind({.name = "sv_fps",
          .fallback = fps,
          .flags = kServerRule | con::kLatch,
          .help = "the simulation's ticks a second, Hz (a new map or a restart)",
          .kind = con::CvarKind::integer,
          .min = 10,
          .max = 240},
         &k.tick_hz);
  v.bind({.name = "sv_maxupdaterate",
          .fallback = mur,
          .flags = kServerRule,
          .help = "the most snapshots a second any client is sent (0: no cap)",
          .kind = con::CvarKind::integer,
          .min = 0,
          .max = 240},
         &k.max_update_rate);
  v.bind({.name = "sv_maxrate",
          .fallback = mr,
          .flags = kServerRule,
          .help = "the most snapshot bytes a second any client is sent (0: no cap)",
          .kind = con::CvarKind::integer,
          .min = 0,
          .max = 1e8},
         &k.max_rate);
  v.bind({.name = "cl_updaterate",
          .fallback = ur,
          .flags = kPlayer,
          .help = "the snapshots a second this client asks for (0: the server's)",
          .kind = con::CvarKind::integer,
          .min = 0,
          .max = 240},
         &k.update_rate);
  v.bind({.name = "cl_cmdrate",
          .fallback = cr,
          .flags = kPlayer,
          .help = "the input messages a second this client sends (0: one a tick)",
          .kind = con::CvarKind::integer,
          .min = 0,
          .max = 240},
         &k.cmd_rate);
  v.bind({.name = "rate",
          .fallback = r,
          .flags = kPlayer,
          .help = "the snapshot bytes a second this client takes (0: no cap)",
          .kind = con::CvarKind::integer,
          .min = 0,
          .max = 1e8},
         &k.rate);
  v.bind({.name = "cl_timesync",
          .fallback = ts,
          .flags = kPlayer,
          .help = "keep this client's clock just ahead of the server and the view delay adaptive",
          .kind = con::CvarKind::boolean},
         &k.sync);
  v.bind({.name = "cl_smooth",
          .fallback = sm,
          .flags = kPlayer,
          .help = "corrections through a spring and the others on curves (0: decay and lines)",
          .kind = con::CvarKind::boolean},
         &k.spring);
  v.bind({.name = "cl_extrapolate",
          .fallback = ex,
          .flags = kPlayer,
          .help = "the most the others are carried past the newest snapshot, s (0: held)",
          .min = 0,
          .max = 1},
         &k.extrapolate_s);
  v.bind({.name = "cl_interp",
          .fallback = in,
          .flags = kPlayer,
          .help = "how far behind the newest snapshot the others are drawn without time sync, s",
          .min = 0,
          .max = 1},
         &n.interp_s);
  v.bind({.name = "cl_cmdbackup",
          .fallback = cb,
          .flags = kPlayer,
          .help = "inputs repeated in every input message, so a lost one is carried by the next",
          .kind = con::CvarKind::integer,
          .min = 1,
          .max = 32},
         &k.redundancy);
  v.bind({.name = "cl_predictworld",
          .fallback = pw,
          .flags = kPlayer,
          .help = "the other players within this many metres predicted beside this one (0: none)",
          .min = 0,
          .max = 1000},
         &k.predict_radius);
  v.bind({.name = "cl_predictmax",
          .fallback = pm,
          .flags = kPlayer,
          .help = "the longest replay the others are predicted over, s (0: no limit)",
          .min = 0,
          .max = 1},
         &k.predict_max_s);
}

void net_cvars_apply(NetCvars& n) {
  ReplicateConfig& k = *n.config;
  k.cubic = k.spring;  // cl_smooth sets both
  k.interp_ticks = std::max(0, static_cast<int>(std::lround(n.interp_s * k.tick_hz)));
}

}  // namespace ge::net
