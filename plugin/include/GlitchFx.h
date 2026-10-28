#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * Glitch Engine: tempo-synced stutter, random glitches (stutter, reverse,
 * dropout, octave jumps), freeze, bit-crush and sample-rate reduction, in
 * one insert. By default it sits after Clouds, before the time FX, so
 * delay and reverb catch the glitches; it can also run before the amps.
 *
 * Everything works on a recording of the incoming signal (a ring of the
 * last kRingSeconds). A stutter loops the most recent slice (one rate step
 * long, times the repeat length) from the moment it engages, so it starts
 * instantly instead of waiting for a slice to be recorded. Random glitches
 * are decided on the rate grid: at each step a seeded hash of (seed, step
 * index) says whether something fires, what and for how many steps. With a
 * seed above 0 and the host transport running, the step index comes from
 * the song position, so the same seed replays the same pattern at the same
 * place in the song; without a transport it counts from switch-on. Seed 0
 * is free-running.
 *
 * Freeze copies the last kFreezeSeconds into its own buffer and replays it
 * as two overlapping Hann grains at random positions, a steady drone that
 * fades in and out over the freeze fade time. It is added to the live
 * signal, so you can play over it.
 *
 * Safety: the wet signal is clamped to 1.4x the loudest of the recent
 * input, the slice being replayed and the frozen buffer, so a glitch can
 * never be much louder than what was played (bit-crushing a quiet signal
 * at very low depths would otherwise jump to full scale). The output is
 * scrubbed of NaN / inf and hard-limited at kOutputCeiling.
 *
 * Real-time contract: prepare() allocates every buffer for the worst case
 * (the slowest rate at the slowest tempo at 192 kHz); process() never
 * allocates, locks or does I/O. The random generator is a hash, not the
 * system's. Nothing adds latency.
 *
 * Off is bit-exact passthrough once the 25 ms switch fade has landed, and
 * on with every section neutral (no stutter or freeze, chance 0, 16 bits,
 * rate at the top) is bit-exact too.
 */
namespace glitchfx {

// Stutter / glitch grid, in quarter-note beats.
inline constexpr std::array<const char*, 9> kRateNames = {"1/4",  "1/8",  "1/16",  "1/32", "1/64",
                                                          "1/4T", "1/8T", "1/16T", "1/32T"};
inline constexpr std::array<double, 9> kRateBeats = {1.0,       0.5,       0.25,      0.125,     0.0625,
                                                     2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0};
// How the Stutter and Freeze buttons behave on the UI: Latch toggles,
// Momentary holds while pressed. MIDI has its own hold targets.
inline constexpr std::array<const char*, 2> kHoldModeNames = {"Latch", "Momentary"};

// Random glitch kinds, for the activity feed (the UI's flicker).
enum class Event : int { none = 0, stutter, reverse, dropout, pitchUp, pitchDown };

inline constexpr float kMinBits = 1.0f;
inline constexpr float kMaxBits = 16.0f;  // the top means off
inline constexpr float kMinRateHz = 400.0f;
inline constexpr float kMaxRateHz = 48000.0f;  // the top means off
inline constexpr float kMinFreezeFadeMs = 5.0f;
inline constexpr float kMaxFreezeFadeMs = 2000.0f;
inline constexpr int kMaxSeed = 999;
inline constexpr double kMinBpm = 30.0;
inline constexpr double kMaxBpm = 300.0;
inline constexpr double kRingSeconds = 2.5;   // > a quarter note at kMinBpm
inline constexpr double kFreezeSeconds = 0.5;
inline constexpr float kOutputCeiling = 4.0f;  // +12 dBFS, the last-resort clamp

struct Params {
  bool on = false;
  float mix = 1.0f;       // 0..1, linear dry/wet
  float outputDb = 0.0f;  // -24..+12
  bool stutter = false;
  int rate = 2;          // index into kRateNames (1/16)
  float length = 1.0f;   // 0.1..1: the part of each repeat that sounds
  float bits = kMaxBits;
  float rateHz = kMaxRateHz;
  float chance = 0.0f;     // 0..1 per grid step
  float intensity = 0.5f;  // 0..1: longer, faster, wilder glitches
  int seed = 0;            // 0 free-running, 1..kMaxSeed repeatable
  bool freeze = false;
  float freezeFadeMs = 150.0f;
};

// The host's tempo and position for this block. bpm <= 0 means none (the
// processor passes its standalone tempo instead).
struct Transport {
  double bpm = 120.0;
  bool playing = false;
  double ppq = 0.0;  // valid when playing
};

// Lock-free audio -> UI feed (relaxed atomics, written once per block).
struct Activity {
  std::atomic<std::uint32_t> events{0};  // +1 per fired glitch / stutter engage
  std::atomic<int> lastEvent{0};         // Event
  std::atomic<float> freeze{0.0f};       // freeze fade level 0..1
  std::atomic<bool> stuttering{false};
  std::atomic<float> crush{0.0f};  // 0..1: how much bit / rate reduction is on
};

class Engine {
public:
  Engine();
  ~Engine();

  void prepare(double sampleRate, int maxBlockSize);
  void reset();
  // In place on a mono or stereo buffer.
  void process(juce::AudioBuffer<float>& buffer, const Params& params, const Transport& transport);

  bool running() const { return running_; }
  static double tailSeconds(const Params& params);

  const Activity& activity() const { return activity_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool running_ = false;
  Activity activity_;
};

}  // namespace glitchfx
