// THE NETWORK'S CVARS: the settings a cfg or the console changes, each bound to a field of the
// `ReplicateConfig` a server or a client is made with (docs/networking.md § Rates and live tools).
// `sv_` the server's (set on the server, sent to every client in the server info), `cl_` and
// `rate` the player's own (archived, sent to the server in the user info, which caps them).
//
//   sv_fps            the simulation's ticks a second (latched: a new map or a restart)
//   sv_maxupdaterate  sv_maxrate          the most snapshots a second and bytes a second sent
//   cl_updaterate     cl_cmdrate  rate    the snapshots and input messages a second asked, bytes
//   cl_timesync       cl_smooth           time sync (M17n) and smoothing (M17o) on or off
//   cl_interp         cl_extrapolate      the fixed delay without sync (s), the extrapolation cap
//   cl_cmdbackup                          inputs repeated in each input message
//   cl_predictworld   cl_predictmax       the predicted world's radius (M17p, m), its horizon (s)
//
// ge:summary The network's cvars: sv_fps, the rates, time sync and smoothing, bound to the config
// ge:keywords net cvars sv_fps tick-rate tick-hz sv_maxupdaterate sv_maxrate cl_updaterate
// ge:keywords cl_cmdrate rate cl_interp cl_timesync cl_smooth cl_extrapolate console
// ge:keywords cl_cmdbackup cl_predictworld cl_predictmax redundancy predicted-world
// ge:skill net
// ge:test tests/net_cvars_test.cpp
#pragma once

#include "console/cvar.hpp"
#include "net/replicate.hpp"

namespace ge::net {

// Binds every network cvar to `config` (which must outlive `cvars`), writing each fallback into
// it now: a game passes the defaults it wants for its genre (a shooter's 72 Hz, a racer's 120).
// `cl_interp` writes `interp_s`, turned into `config.interp_ticks` by `net_cvars_apply`.
struct NetCvars {
  ReplicateConfig* config = nullptr;
  double interp_s = 0.1;  // cl_interp
};
void add_net_cvars(console::Cvars& cvars, NetCvars& out);
// The values that are not fields of the config as they stand (cl_interp to ticks at sv_fps):
// call after a cfg is exec'd and before the config is used.
void net_cvars_apply(NetCvars& n);

}  // namespace ge::net
