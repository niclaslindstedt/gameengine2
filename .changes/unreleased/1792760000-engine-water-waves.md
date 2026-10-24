---
type: Added
title: Bodies of water and the waves a wind grows on them
---

`ge::water` (`engine/water`, published as `ge::water`) lays water in one call a body (`add_lake`, `add_ocean`, `add_river`, `add_volume`) and answers everything through one query, `water::sample`: the surface, its normal, the water's velocity (the waves' orbital motion and a river's current), the depth, the bed, the density and the breaking shares. A lake's sea is grown by the wind over each point's effective fetch, so its lee shore is rough and its windward one calm; an ocean's to the developed sea; the waves are a JONSWAP spectrum laid as a few components with second-order crests. The `waves` and `fetch` labs chart the growth laws, the spectrum and its components, a measured series and the surfaces.
