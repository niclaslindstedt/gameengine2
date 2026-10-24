---
type: Added
title: The predicted world: other players predicted beside your own
---

A predicting client can now step the other players near its own entity beside it, by their last inputs, which the server sends on the snapshot. A game's `Schema::interact` resolves each contact the same way on both ends. Each contact is predicted as an event and then confirmed, undone, missed or heard against the server's, which comes over a reliable channel. A replay budget leaves out the farthest players. Past a horizon (`predict_max_s`, 0.1 s), the others fade back to their interpolated curve. The new cvars `cl_predictworld`, `cl_predictmax` and `cl_cmdbackup` set these. The `net-predict` lab's joust measures it: at 72 Hz, corrections a minute fall from 7.9 to 0 on a clean link and from 92 to 14 on wifi.
