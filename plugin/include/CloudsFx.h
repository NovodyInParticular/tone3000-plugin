#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>

/**
 * Clouds: Mutable Instruments' granular texture processor (the module's own
 * DSP, vendored in plugin/MutableClouds) after the tone stack, before the
 * time-FX rack. It records the incoming guitar into a short buffer and
 * replays it as overlapping grains, a time-stretcher, a pitch-shifting
 * looping delay or a spectral (phase vocoder) smear, with its own feedback
 * and reverb.
 *
 * The engine runs exactly as on the module: 32 kHz, 32-frame blocks, 16-bit
 * input/output, the module's buffer sizes. The wet path is resampled to and
 * from 32 kHz (band-limited cubic interpolation); the dry path never leaves
 * the host rate, so Blend at 0 is bit-transparent and nothing adds latency
 * to the dry signal (the wet signal trails it by ~2 ms).
 *
 * Real-time contract: prepare() allocates everything; process() never
 * allocates, locks or does I/O. Switching off fades the wet path out over
 * 20 ms and then costs nothing. Switching on starts from an empty buffer.
 */
namespace cloudsfx {

inline constexpr std::array<const char*, 4> kModeNames = {"Granular", "Stretch", "Looping Delay", "Spectral"};
// Clouds' quality setting: bit 0 = mono, bit 1 = 8-bit (lo-fi). Mono and
// 8-bit trade fidelity for a longer recording buffer (1 s stereo 16-bit up
// to 8 s mono 8-bit).
inline constexpr std::array<const char*, 4> kQualityNames = {"16-bit Stereo", "16-bit Mono", "8-bit Stereo",
                                                             "8-bit Mono"};
inline constexpr float kPitchRange = 24.0f;  // semitones, either way

struct Params {
  bool on = false;
  int mode = 0;     // index into kModeNames
  int quality = 0;  // index into kQualityNames
  bool freeze = false;
  float position = 0.3f;  // 0..1 (the module's knobs; meanings follow the mode)
  float size = 0.5f;
  float pitch = 0.0f;  // semitones, ±kPitchRange
  float density = 0.65f;
  float texture = 0.5f;
  float blend = 0.5f;  // dry/wet, equal power
  float spread = 0.5f;
  float feedback = 0.0f;
  float reverb = 0.3f;
};

class Engine {
public:
  Engine();
  ~Engine();

  void prepare(double sampleRate, int maxBlockSize);
  // Clears the recording buffer and the resampler state.
  void reset();
  // In place on a mono or stereo buffer.
  void process(juce::AudioBuffer<float>& buffer, const Params& params);

  bool running() const { return running_; }
  // How long the output can keep sounding after the input stops.
  static double tailSeconds(const Params& params);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool running_ = false;
};

}  // namespace cloudsfx
