// THE SEA A WIND GROWS (docs/water.md § Waves): how high and how long the waves are for a wind
// that has blown over a fetch of water, the spectrum their energy is spread over, and how it is
// spread round the wind's direction. Pure functions; no state, no randomness.
//
// - THE GROWTH LAWS (`grow`): the fetch-limited significant height and peak period,
//   g Hs / U^2 = 0.0016 (g F / U^2)^(1/2) and g Tp / U = 0.286 (g F / U^2)^(1/3), with U the wind
//   at 10 m, capped where both reach the fully developed sea (g Hs / U^2 = 0.243, g Tp / U = 8.13,
//   at the same dimensionless fetch, about 2.3e4): a pond grows ripples, a lake a chop, an ocean a
//   sea.
// - THE SPECTRUM (`jonswap`): the JONSWAP shape over angular frequency,
//   S(w) = a g^2 w^-5 exp(-5/4 (wp / w)^4) gamma^r, r = exp(-(w - wp)^2 / (2 s^2 wp^2)),
//   s = 0.07 below the peak and 0.09 above, gamma the peak enhancement (3.3 by default); `a` is
//   found by normalising to 4 sqrt(m0) = Hs, so the spectrum always carries the asked height.
// - SHALLOW WATER (`tma`): the depth factor that limits a spectrum where the bed is near (the TMA
//   form, Kitaigorodskii's phi(w sqrt(h / g))): 1 in deep water, falling to nothing as w h -> 0.
// - THE SPREAD (`spread_power`, `spread`): energy round the mean direction as cos^2s(theta / 2),
//   s largest at the peak (the swell-like core, `kSpreadPeak`) and falling away above and below it,
//   so the short chop is more confused than the long waves under it.
// - DISPERSION (`wavenumber`): w^2 = g k tanh(k d), solved by Newton from the deep-water guess.
//
// ge:summary The sea a wind grows: fetch laws, JONSWAP, depth limit, spread, dispersion
// ge:keywords water waves sea spectrum jonswap pierson-moskowitz fetch growth significant-height
// ge:keywords peak-period tma depth spread directional dispersion wavenumber grow jonswap_at
// ge:skill water determinism
// ge:test tests/water_spectrum_test.cpp
#pragma once

namespace ge::water {

inline constexpr double kGravity = 9.80665;  // m/s^2
inline constexpr double kGamma = 3.3;        // JONSWAP's mean peak enhancement
inline constexpr double kSpreadPeak = 10;    // cos^2s's s at the peak

// A sea as two numbers and a direction.
struct Sea {
  double hs = 0;      // significant height, m (4 sqrt(m0))
  double tp = 0;      // peak period, s
  double toward = 0;  // the heading the waves run toward, rad
  double gamma = kGamma;
  double depth = 1e9;  // the water's depth for the depth limit, m
};

// The sea a wind of `wind` m/s at 10 m grows over `fetch` m of water, its waves running toward
// `toward`; nothing below 0.3 m/s (still air) or over no fetch.
Sea grow(double wind, double fetch, double toward);

// The dimensionless fetch g F / U^2 past which the sea is fully developed.
double developed_fetch();

// The JONSWAP shape at `w` rad/s for a peak at `wp` with enhancement `gamma`, with a = 1.
double jonswap_shape(double w, double wp, double gamma);

// The spectrum's density at `w`, m^2 s, normalised so 4 sqrt(m0) = sea.hs (the depth limit and
// all). `norm` is `normaliser(sea)`, worked out once a sea.
double jonswap_at(const Sea& sea, double w, double norm);
double normaliser(const Sea& sea);

// The depth factor at `w` over `depth` m (1 in deep water).
double tma(double w, double depth);

// The spread's power s at `w` for a peak at `wp`.
double spread_power(double w, double wp);
// The spread's shape at an angle `off` (rad) from the mean for power `s`: cos^2s(off / 2), 1 at
// the mean (unnormalised: a reader that needs a density integrates it, as `lay` does).
double spread(double off, double s);

// The wavenumber of a wave of angular frequency `w` in water `depth` m deep, rad/m.
double wavenumber(double w, double depth);

}  // namespace ge::water
