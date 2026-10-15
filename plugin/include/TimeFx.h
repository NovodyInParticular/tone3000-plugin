#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

/**
 * Time effects after the cab: delay, reverb and modulation (chorus /
 * flanger) as one rack at the host rate, between the tone stack and the
 * output level. Each effect can be switched off and the three run in any of
 * the six orders.
 *
 * Real-time contract: prepare() allocates every buffer for the worst case
 * (longest delay, largest room, deepest modulation); process() never
 * allocates, locks or does I/O. Nothing here adds latency.
 *
 * Switching an effect off fades its *input* out, so delay repeats and the
 * reverb tail ring out naturally ("spillover") and switching on never
 * clicks; once its tail has decayed an off effect costs nothing. A change
 * of order dips the rack's output for a few milliseconds around the switch.
 */
namespace timefx {

// Tempo-synced delay divisions, in quarter-note beats.
inline constexpr std::array<const char*, 11> kDivisionNames = {
    "1/1", "1/2", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T"};
inline constexpr std::array<double, 11> kDivisionBeats = {4.0, 2.0,  1.0,         1.5,  2.0 / 3.0, 0.5,
                                                          0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0};

// The six orders of [mod, delay, reverb]; 0 is the conventional one.
inline constexpr std::array<const char*, 6> kOrderNames = {
    "Mod > Delay > Reverb", "Mod > Reverb > Delay", "Delay > Mod > Reverb",
    "Delay > Reverb > Mod", "Reverb > Mod > Delay", "Reverb > Delay > Mod"};
enum class Effect { mod, delay, reverb };
inline constexpr std::array<std::array<Effect, 3>, 6> kOrders = {{
    {Effect::mod, Effect::delay, Effect::reverb},
    {Effect::mod, Effect::reverb, Effect::delay},
    {Effect::delay, Effect::mod, Effect::reverb},
    {Effect::delay, Effect::reverb, Effect::mod},
    {Effect::reverb, Effect::mod, Effect::delay},
    {Effect::reverb, Effect::delay, Effect::mod},
}};

struct DelayParams {
  bool on = false;
  float timeMs = 380.0f;  // used when not synced
  bool sync = false;
  int division = 5;       // 1/8 when synced
  float feedback = 0.35f;  // 0..0.95
  float mix = 0.3f;        // 0..1
  bool pingPong = false;
  float damping = 0.3f;  // 0..1: high-frequency loss per repeat
};

struct ReverbParams {
  bool on = false;
  float size = 0.5f;     // 0..1: room dimensions (delay lengths)
  float decayS = 1.8f;   // RT60, 0.2..10 s
  float damping = 0.4f;  // 0..1: high-frequency decay
  float mix = 0.25f;
};

struct ModParams {
  bool on = false;
  bool flanger = false;  // chorus otherwise
  float rateHz = 0.8f;   // 0.05..10
  float depth = 0.5f;    // 0..1
  float mix = 0.5f;
};

struct Params {
  DelayParams delay;
  ReverbParams reverb;
  ModParams mod;
  int order = 0;
};

inline constexpr double kMaxDelaySeconds = 2.0;
inline constexpr double kMaxReverbSeconds = 10.0;

class Delay {
public:
  void prepare(double sampleRate);
  void reset();
  // `bpm` <= 0 means no tempo from the host (synced divisions use 120).
  void process(float* left, float* right, int numSamples, const DelayParams& p, double bpm);
  static double delaySeconds(const DelayParams& p, double bpm);

private:
  double sampleRate_ = 48000.0;
  std::array<std::vector<float>, 2> line_;
  int write_ = 0;
  juce::LinearSmoothedValue<float> time_, feedback_, mix_, input_;
  std::array<float, 2> lowpass_{};
  bool primed_ = false;
  int idleSamples_ = 0;  // samples since the input fade landed at zero
  int tailSamples_ = 0;  // how long the repeats can still be heard
};

class Reverb {
public:
  static constexpr int kLines = 8;
  void prepare(double sampleRate);
  void reset();
  void process(float* left, float* right, int numSamples, const ReverbParams& p);

private:
  struct Allpass {
    std::vector<float> buffer;
    int index = 0;
    int length = 1;
    float process(float x);
  };
  double sampleRate_ = 48000.0;
  std::array<std::vector<float>, kLines> lines_;
  std::array<int, kLines> write_{};
  std::array<float, kLines> lowpass_{};
  std::array<float, kLines> length_{};  // current (smoothed) length in samples
  std::array<Allpass, 4> diffusers_;
  juce::LinearSmoothedValue<float> mix_, input_;
  bool primed_ = false;
  int idleSamples_ = 0;
};

class Modulation {
public:
  void prepare(double sampleRate);
  void reset();
  void process(float* left, float* right, int numSamples, const ModParams& p);

private:
  double sampleRate_ = 48000.0;
  std::array<std::vector<float>, 2> line_;
  int write_ = 0;
  double phase_ = 0.0;
  juce::LinearSmoothedValue<float> mix_, depth_, rate_;
  std::array<float, 2> feedback_{};
  bool primed_ = false;
};

class Rack {
public:
  void prepare(double sampleRate, int maxBlockSize);
  void reset();
  // In place on 1 or 2 channels (mono: the left pointer only).
  void process(juce::AudioBuffer<float>& buffer, const Params& p, double bpm);
  // How long the rack can keep sounding after its input stops (host tail).
  static double tailSeconds(const Params& p, double bpm);

private:
  Delay delay_;
  Reverb reverb_;
  Modulation mod_;
  int order_ = 0;
  int pendingOrder_ = -1;
  juce::LinearSmoothedValue<float> reorderGain_{1.0f};
};

}  // namespace timefx
