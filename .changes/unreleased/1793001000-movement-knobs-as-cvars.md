---
type: Added
title: Movement knobs as console variables
---

`ge::move::bind_knobs` binds every character-movement knob to a console under an owner prefix (`sv_gravity`, `sv_airaccelerate`, ...), by default with the server's flags so a client follows the server's row and cannot set it itself; each cvar's default is the bound row's value and its help ends with its unit. `apply_cfg` runs a server cfg on a `MoveSpec`, naming each refused line by file and line, and the `move-strafe` and `move-trace` labs take one as `--cfg`.
