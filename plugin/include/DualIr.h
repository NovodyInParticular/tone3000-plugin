#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdint>
#include <vector>

/**
 * Dual cab IR blending: two impulse responses mixed into one kernel, which
 * then loads through the ordinary IR path (one convolver, so the CPU is that
 * of a single IR, and every change swaps engines with the block's IR→IR
 * crossfade, so moving a knob never clicks).
 *
 * Mix, per IR: unit-energy normalized (so the blend compares the cabs, not
 * their file levels), scaled by its level and blend share, sign-flipped
 * when inverted. B is time-shifted against A by the align offset (positive
 * delays B, negative delays A) with a windowed-sinc fractional delay, so a
 * sub-sample offset still lines the two up instead of comb-filtering.
 *
 * Runs on loader threads only (allocates, decodes).
 */
namespace dualir {

struct Params {
  float blend = 0.5f;     // 0 = all A, 1 = all B (linear share)
  float levelADb = 0.0f;  // ±12 dB trims
  float levelBDb = 0.0f;
  bool invertA = false;
  bool invertB = false;
  float alignMs = 0.0f;  // B relative to A, ±kMaxAlignMs

  static constexpr float kMaxAlignMs = 2.0f;
  static constexpr float kMaxLevelDb = 12.0f;
  // Clamped and finite (state and shared rigs are untrusted).
  Params sanitized() const;
};

// The two IR files' bytes (any format JUCE reads; WAV in practice) blended
// into a 32-bit float WAV at `sampleRate`, or empty when either can't be
// decoded. `maxSeconds` caps each input before mixing.
std::vector<uint8_t> combine(const std::vector<uint8_t>& irA, const std::vector<uint8_t>& irB, const Params& params,
                             double sampleRate = 48000.0, double maxSeconds = 2.0);

// Decoded and resampled to `sampleRate` (exposed for the tests).
bool decode(const std::vector<uint8_t>& bytes, double sampleRate, double maxSeconds, juce::AudioBuffer<float>& out);

}  // namespace dualir
