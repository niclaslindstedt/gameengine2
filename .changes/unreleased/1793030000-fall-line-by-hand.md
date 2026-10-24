---
type: Added
title: Fall Line played by hand
---

The `fall_line` window now puts the player on the skis: the keys, a gamepad and a wheel all steer at once through Fall Line's default bindings (`games/fall_line/controls`), F5 walks a rebinding of every action and saves it to the player's own bindings file, the pads rumble on landings, wipeouts, trunks and the finish, and the run is written as an input log that `ge replay` plays back to its digest. `--bot` hands the skis to the bot, `--events` plays a recorded device session into the window and `--record-events` writes one. The platform gains `prefs_dir`, the player's own directory for what a game keeps. The `by-hand` lab holds the bot's run on a simulated pad and skis it again by hand to the bot's digest.
