---
type: Added
title: Snow contact laws
---

`ge::snow` gains the contact laws any machine on snow is built from (`snow/contact.hpp`): Bekker's pressure–sinkage law capped by what a column can compact, compaction and bulldozing resistance, Mohr–Coulomb strength with Janosi–Hanamoto slip for a track's or a tyre's traction, a base's glide and a tyre's grip by temperature, and planing. Each named snow has a row of numbers a game may replace with its own. `press` now sinks a contact given only its load by these laws, so a standing ski sinks into powder and a boot post-holes to the ground. New lab `snow-contact`.
