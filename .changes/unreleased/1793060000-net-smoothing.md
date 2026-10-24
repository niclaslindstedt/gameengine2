---
type: Added
title: Smooth corrections, curved interpolation and any tick rate
---

A predicting client now shows a correction through a critically damped spring that keeps the drawn velocity continuous, instead of a decay that kinked it. The other players are drawn on cubic curves through the snapshots. When snapshots run late, they are carried on at their last velocity for up to 0.2 s and blended back when the snapshot arrives, instead of standing still. Every replication duration is now in seconds, so the same settings behave the same at any tick rate. The new `sv_fps` cvar (10 to 240 Hz) and the rate and smoothing cvars (`sv_maxupdaterate`, `sv_maxrate`, `cl_updaterate`, `cl_cmdrate`, `rate`, `cl_timesync`, `cl_smooth`, `cl_interp`, `cl_extrapolate`) are bound by `add_net_cvars`. The `net-predict` lab runs every preset at 72 Hz and 120 Hz.
