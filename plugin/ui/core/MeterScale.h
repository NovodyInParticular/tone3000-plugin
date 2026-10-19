// Shared meter scale and colour ramp (port of meterColor.ts + the level →
// colour math in BlockLed.tsx) for DbMeter, DotMeter, BlockLed and the
// gallery energy glow. Stops are the skin's meter tokens (theme::kMeterLow /
// Mid / High), classic blue → yellow → red.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>

#include "core/Theme.h"

namespace t3k::ui::meter {

// Channel-wise blend, rounded to 8-bit like the CSS `rgb()` string.
inline juce::Colour lerp(juce::Colour a, juce::Colour b, float t) {
  const auto mix = [t](juce::uint8 x, juce::uint8 y) {
    return static_cast<juce::uint8>(juce::roundToInt(x + (y - x) * t));
  };
  return juce::Colour(mix(a.getRed(), b.getRed()), mix(a.getGreen(), b.getGreen()),
                      mix(a.getBlue(), b.getBlue()));
}

inline constexpr float kMinDb = -60.0f;
// Top of the scale = 0 dBFS: the last dot lights exactly at clipping.
inline constexpr float kMaxDb = 0.0f;

// Level → 0..1 along the scale, clamped.
inline float unit(float db) { return std::clamp((db - kMinDb) / (kMaxDb - kMinDb), 0.0f, 1.0f); }

// Dot colour along the rail: 0 = bottom (low), 0.5 = middle (mid),
// 1 = top (high).
inline juce::Colour gradientColour(float position) {
  if (position <= 0.5f) return lerp(theme::kMeterLow, theme::kMeterMid, position * 2.0f);
  return lerp(theme::kMeterMid, theme::kMeterHigh, (position - 0.5f) * 2.0f);
}

// Gallery tile energy (BlockLed.tsx useBlockEnergy)
struct Energy {
  juce::Colour colour;
  float unit;      // full-scale level 0..1
  float presence;  // noise-gated intensity 0..1
};

// NAM captures idle around -50…-40 dB, so the glow fades in across that band
// while its intensity keeps tracking the full scale up to 0 dBFS.
inline Energy energy(float db) {
  constexpr float kGateOffDb = -50.0f, kGateOnDb = -40.0f;
  const juce::Colour dark(34, 34, 34), blue = theme::kMeterLow.withAlpha(1.0f),
                     yellow = theme::kMeterMid.withAlpha(1.0f), red = theme::kMeterHigh.withAlpha(1.0f);

  // Low/mid plateaus with short crossfades, high at the top.
  const auto levelColour = [&](float u) {
    constexpr float fade = 0.06f, toYellow = 0.5f, toRed = 0.85f;
    if (u < toYellow - fade) return blue;
    if (u < toYellow + fade) return lerp(blue, yellow, (u - (toYellow - fade)) / (fade * 2));
    if (u < toRed - fade) return yellow;
    if (u < toRed + fade) return lerp(yellow, red, (u - (toRed - fade)) / (fade * 2));
    return red;
  };

  const float u = unit(db);
  const float gate = std::clamp((db - kGateOffDb) / (kGateOnDb - kGateOffDb), 0.0f, 1.0f);
  return {lerp(dark, levelColour(u), gate), u, gate * u};
}

}  // namespace t3k::ui::meter
