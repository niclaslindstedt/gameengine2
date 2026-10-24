---
type: Added
title: The camera's settings are cvars
---

Every camera setting a player turns is a cvar: `cl_fov`, `cl_viewheight`, `cl_bob`, `cl_bobcycle`, `cl_bobup`, `cl_rollangle`, `cl_rollspeed`, `v_kicktime`, `v_kickpitch`, `v_kickroll`, `v_idlescale`, `cl_landdip`, `cl_chasedist`, `cl_chaseheight`, `cl_fovspeed`, `cl_camblend`, `cl_motion` (comfort) and `cl_shake`, plus `cl_camera` when a game asks for it. A game registers them with its own defaults and world unit (`add_camera_cvars`), and they are written over its camera ladder once a frame (`apply_camera_cvars`). Fall Line registers them, so a cfg or the console turns its camera, and `ge cvar ls cl_` lists them. `camera-rigs --motion=0` checks that no rig rolls the horizon at comfort 0.
