#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "PitchShift.h"

/**
 * Abyss Pitch: layered pitch effect. Up to five shifted voices of the
 * incoming signal (two octaves down, one down, one up, and a detuned pair
 * at a free or dissonant shift), each with its own level, over a dry path
 * delayed to line up with them. A pitch bend with glide moves every voice
 * together, for an expression pedal.
 *
 * Engine: every voice is the project's own correlation-spliced delay-line
 * shifter (PitchShift.h), the low-latency engine the faceplate Pitch uses,
 * one instance per voice so each splices on its own. Tracking picks its
 * window: Tight (20 ms, 11 ms latency, best for chords and playing feel) or
 * Smooth (40 ms, 21 ms, fewer splices on sustained notes). The dry path is
 * delayed by the same amount, so dry and layers stay in phase.
 *
 * Detune spreads the voices: the shift pair splits to ± half the detune,
 * the octave layers lean alternately up and down by half of it, so a few
 * cents shimmer and 50+ cents sounds like a wide chorus. After the amps the
 * pair is also spread across the stereo field.
 *
 * Latency reported to the host: PitchShift::latencySamples for the
 * tracking's window while the effect is on; 0 while off (a powered-off
 * Abyss is a bit-exact passthrough once its fade has landed). A tracking
 * change dips the output for ~10 ms around the switch.
 *
 * Real-time contract: prepare() sizes every buffer; process() never
 * allocates, locks or does I/O. Parameters are smoothed per 32-sample
 * sub-block (shift, bend glide, detune) or per sample (levels).
 */
namespace abyss {

inline constexpr std::array<const char*, 7> kIntervalNames = {
    "Free", "Minor 2nd up", "Minor 2nd down", "Tritone up", "Tritone down", "Major 7th up", "Minor 9th down"};
inline constexpr std::array<float, 7> kIntervalSemitones = {0.0f, 1.0f, -1.0f, 6.0f, -6.0f, 11.0f, -13.0f};
inline constexpr std::array<const char*, 2> kBendRangeNames = {"12 st", "24 st"};
inline constexpr std::array<const char*, 2> kTrackingNames = {"Tight", "Smooth"};
inline constexpr float kShiftRange = 12.0f;     // free shift, semitones either way
inline constexpr float kMaxDetuneCents = 100.0f;
inline constexpr float kMaxGlideMs = 2000.0f;
inline constexpr int kVoices = 5;

enum Voice { subTwo = 0, subOne, up, shiftA, shiftB };

struct Params {
  bool on = false;
  float subTwoLevel = 0.0f;  // -2 octaves, 0..1
  float subOneLevel = 0.7f;  // -1 octave
  float upLevel = 0.0f;      // +1 octave
  float shiftLevel = 0.0f;   // the shift pair
  float shiftSemitones = 0.0f;  // free shift, ±kShiftRange
  int interval = 0;             // kIntervalNames; anything but Free overrides the free shift
  float detuneCents = 0.0f;     // 0..kMaxDetuneCents
  float bend = 0.0f;            // -1..1 of the bend range
  int bendRange = 0;            // kBendRangeNames
  float glideMs = 80.0f;
  int tracking = 0;             // kTrackingNames
  float dry = 1.0f, wet = 1.0f;  // 0..1
  float outputDb = 0.0f;         // -24..+12
  bool stereoSpread = false;     // after the amps: spread the shift pair L/R
};

PitchShift::Window windowFor(int tracking);
// Latency the effect adds while on, at this rate.
int latencySamples(int tracking, double sampleRate);

// Lock-free audio -> UI feed (relaxed atomics, written once per block).
struct Activity {
  std::array<std::atomic<float>, kVoices> level{};  // each voice's output peak, 0..1
  std::atomic<float> bend{0.0f};                     // the glided bend, semitones
  std::atomic<float> detune{0.0f};                   // cents
  std::atomic<float> input{0.0f};                    // input peak
};

class Engine {
public:
  Engine();
  ~Engine();

  void prepare(double sampleRate, int maxBlockSize);
  void reset();
  // In place on a mono or stereo buffer.
  void process(juce::AudioBuffer<float>& buffer, const Params& params);

  bool running() const { return running_; }
  const Activity& activity() const { return activity_; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool running_ = false;
  Activity activity_;
};

}  // namespace abyss
