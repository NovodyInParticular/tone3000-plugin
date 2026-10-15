#include "TimeFx.h"

#include <algorithm>
#include <cmath>

namespace timefx {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Input fades (on/off), mix and order dips: short enough to feel instant,
// long enough not to click.
constexpr double kFadeSeconds = 0.02;
constexpr double kReorderSeconds = 0.005;

float clampFinite(float v, float lo, float hi, float fallback) {
  if (!std::isfinite(v))
    return fallback;
  return std::clamp(v, lo, hi);
}

// Linear-interpolated read `delay` samples behind `write` (exclusive).
float readFrac(const std::vector<float>& line, int write, float delay) {
  const int size = static_cast<int>(line.size());
  const int whole = static_cast<int>(delay);
  const float frac = delay - static_cast<float>(whole);
  int a = write - whole;
  if (a < 0)
    a += size;
  int b = a - 1;
  if (b < 0)
    b += size;
  return line[static_cast<size_t>(a)] + frac * (line[static_cast<size_t>(b)] - line[static_cast<size_t>(a)]);
}

// In-place 8-point Hadamard, scaled to stay orthonormal (energy preserving).
void hadamard8(std::array<float, 8>& x) {
  for (int span = 1; span < 8; span <<= 1)
    for (int i = 0; i < 8; i += span << 1)
      for (int j = i; j < i + span; ++j) {
        const float a = x[static_cast<size_t>(j)];
        const float b = x[static_cast<size_t>(j + span)];
        x[static_cast<size_t>(j)] = a + b;
        x[static_cast<size_t>(j + span)] = a - b;
      }
  constexpr float kScale = 0.35355339f;  // 1/sqrt(8)
  for (auto& v : x)
    v *= kScale;
}

}  // namespace

// ============================================================================
// Delay
// ============================================================================

double Delay::delaySeconds(const DelayParams& p, double bpm) {
  double seconds;
  if (p.sync) {
    const double tempo = (std::isfinite(bpm) && bpm > 0.0) ? std::clamp(bpm, 20.0, 999.0) : 120.0;
    const int division = std::clamp(p.division, 0, static_cast<int>(kDivisionBeats.size()) - 1);
    seconds = kDivisionBeats[static_cast<size_t>(division)] * 60.0 / tempo;
  } else {
    seconds = clampFinite(p.timeMs, 1.0f, 2000.0f, 380.0f) / 1000.0;
  }
  return std::clamp(seconds, 0.001, kMaxDelaySeconds);
}

void Delay::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  const auto size = static_cast<size_t>(std::ceil(kMaxDelaySeconds * sampleRate_)) + 4;
  for (auto& l : line_)
    l.assign(size, 0.0f);
  // Time glides (tape-like) rather than jumping, so turning the knob while
  // repeats ring never tears the buffer.
  time_.reset(sampleRate_, 0.15);
  feedback_.reset(sampleRate_, kFadeSeconds);
  mix_.reset(sampleRate_, kFadeSeconds);
  input_.reset(sampleRate_, kFadeSeconds);
  reset();
}

void Delay::reset() {
  for (auto& l : line_)
    std::fill(l.begin(), l.end(), 0.0f);
  write_ = 0;
  lowpass_ = {};
  primed_ = false;
  idleSamples_ = 0;
  tailSamples_ = 0;
  input_.setCurrentAndTargetValue(0.0f);
}

void Delay::process(float* left, float* right, int numSamples, const DelayParams& p, double bpm) {
  if (line_[0].empty() || numSamples <= 0)
    return;

  const float delaySamples = static_cast<float>(delaySeconds(p, bpm) * sampleRate_);
  const float feedback = clampFinite(p.feedback, 0.0f, 0.95f, 0.35f);
  const float mix = clampFinite(p.mix, 0.0f, 1.0f, 0.3f);
  const float damping = clampFinite(p.damping, 0.0f, 1.0f, 0.3f);

  if (!primed_) {
    // First block after a reset: start at the target, no glide from zero.
    time_.setCurrentAndTargetValue(delaySamples);
    feedback_.setCurrentAndTargetValue(feedback);
    mix_.setCurrentAndTargetValue(mix);
    input_.setCurrentAndTargetValue(p.on ? 1.0f : 0.0f);
    primed_ = true;
  }
  time_.setTargetValue(delaySamples);
  feedback_.setTargetValue(feedback);
  mix_.setTargetValue(mix);
  input_.setTargetValue(p.on ? 1.0f : 0.0f);

  // Time for the repeats to fall 60 dB (or one pass when there's no feedback).
  const double repeats = feedback > 0.001f ? std::log(0.001) / std::log(static_cast<double>(feedback)) : 1.0;
  tailSamples_ = static_cast<int>(std::min((repeats + 1.0) * delaySamples, 30.0 * sampleRate_));

  if (!p.on && !input_.isSmoothing()) {
    if (idleSamples_ > tailSamples_)
      return;  // the tail has died away: nothing left to do
    idleSamples_ += numSamples;
    if (idleSamples_ > tailSamples_) {
      // Clear once so a later switch-on starts from silence.
      for (auto& l : line_)
        std::fill(l.begin(), l.end(), 0.0f);
      lowpass_ = {};
      return;
    }
  } else {
    idleSamples_ = 0;
  }

  const int size = static_cast<int>(line_[0].size());
  const float maxDelay = static_cast<float>(size - 2);
  const float lp = 0.9f * damping;  // one-pole coefficient in the feedback path (0 = flat)
  auto& lineL = line_[0];
  auto& lineR = line_[1];

  for (int i = 0; i < numSamples; ++i) {
    const float d = std::clamp(time_.getNextValue(), 1.0f, maxDelay);
    const float fb = feedback_.getNextValue();
    const float wetGain = mix_.getNextValue();
    const float in = input_.getNextValue();

    const float dryL = left[i];
    const float dryR = right ? right[i] : dryL;

    float tapL = readFrac(lineL, write_, d);
    float tapR = readFrac(lineR, write_, d);
    lowpass_[0] += (1.0f - lp) * (tapL - lowpass_[0]);
    lowpass_[1] += (1.0f - lp) * (tapR - lowpass_[1]);
    tapL = lowpass_[0];
    tapR = lowpass_[1];

    if (p.pingPong) {
      // Mono input enters the left line only; each repeat crosses sides.
      lineL[static_cast<size_t>(write_)] = 0.5f * (dryL + dryR) * in + fb * tapR;
      lineR[static_cast<size_t>(write_)] = fb * tapL;
    } else {
      lineL[static_cast<size_t>(write_)] = dryL * in + fb * tapL;
      lineR[static_cast<size_t>(write_)] = dryR * in + fb * tapR;
    }
    if (++write_ >= size)
      write_ = 0;

    if (right) {
      left[i] = dryL + wetGain * tapL;
      right[i] = dryR + wetGain * tapR;
    } else {
      left[i] = dryL + wetGain * 0.5f * (tapL + tapR);
    }
  }
}

// ============================================================================
// Reverb: 8-line feedback delay network with a Hadamard mixing matrix and
// four input allpass diffusers. Line lengths scale with Size; per-line gains
// give the requested RT60; a one-pole in each line darkens the tail.
// ============================================================================

namespace {
constexpr std::array<double, Reverb::kLines> kLineMs = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.3};
constexpr std::array<double, 4> kDiffuserMs = {4.77, 3.59, 12.73, 9.31};
constexpr double kMaxSizeScale = 2.0;

double sizeScale(float size) {
  return 0.4 + 1.6 * static_cast<double>(size);  // 0.4x .. 2x
}
}  // namespace

float Reverb::Allpass::process(float x) {
  constexpr float g = 0.6f;
  const float delayed = buffer[static_cast<size_t>(index)];
  const float v = x + g * delayed;
  buffer[static_cast<size_t>(index)] = v;
  if (++index >= length)
    index = 0;
  return delayed - g * v;
}

void Reverb::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  for (int i = 0; i < kLines; ++i) {
    const auto size = static_cast<size_t>(std::ceil(kLineMs[static_cast<size_t>(i)] * kMaxSizeScale * 0.001 * sampleRate_)) + 4;
    lines_[static_cast<size_t>(i)].assign(size, 0.0f);
  }
  for (size_t i = 0; i < diffusers_.size(); ++i) {
    auto& d = diffusers_[i];
    d.length = std::max(1, static_cast<int>(kDiffuserMs[i] * 0.001 * sampleRate_));
    d.buffer.assign(static_cast<size_t>(d.length), 0.0f);
  }
  mix_.reset(sampleRate_, kFadeSeconds);
  input_.reset(sampleRate_, kFadeSeconds);
  reset();
}

void Reverb::reset() {
  for (auto& l : lines_)
    std::fill(l.begin(), l.end(), 0.0f);
  for (auto& d : diffusers_) {
    std::fill(d.buffer.begin(), d.buffer.end(), 0.0f);
    d.index = 0;
  }
  write_ = {};
  lowpass_ = {};
  primed_ = false;
  idleSamples_ = 0;
  input_.setCurrentAndTargetValue(0.0f);
}

void Reverb::process(float* left, float* right, int numSamples, const ReverbParams& p) {
  if (lines_[0].empty() || numSamples <= 0)
    return;

  const float size = clampFinite(p.size, 0.0f, 1.0f, 0.5f);
  const float decay = clampFinite(p.decayS, 0.2f, static_cast<float>(kMaxReverbSeconds), 1.8f);
  const float damping = clampFinite(p.damping, 0.0f, 1.0f, 0.4f);
  const float mix = clampFinite(p.mix, 0.0f, 1.0f, 0.25f);

  std::array<float, kLines> target{};
  for (int i = 0; i < kLines; ++i) {
    const double len = kLineMs[static_cast<size_t>(i)] * sizeScale(size) * 0.001 * sampleRate_;
    target[static_cast<size_t>(i)] = static_cast<float>(std::min(len, static_cast<double>(lines_[static_cast<size_t>(i)].size() - 2)));
  }
  if (!primed_) {
    length_ = target;
    mix_.setCurrentAndTargetValue(mix);
    input_.setCurrentAndTargetValue(p.on ? 1.0f : 0.0f);
    primed_ = true;
  }
  mix_.setTargetValue(mix);
  input_.setTargetValue(p.on ? 1.0f : 0.0f);

  const int tailSamples = static_cast<int>((decay + kMaxSizeScale * 0.08) * sampleRate_);
  if (!p.on && !input_.isSmoothing()) {
    if (idleSamples_ > tailSamples)
      return;
    idleSamples_ += numSamples;
    if (idleSamples_ > tailSamples) {
      for (auto& l : lines_)
        std::fill(l.begin(), l.end(), 0.0f);
      for (auto& d : diffusers_)
        std::fill(d.buffer.begin(), d.buffer.end(), 0.0f);
      lowpass_ = {};
      return;
    }
  } else {
    idleSamples_ = 0;
  }

  // Per-line feedback gain for the RT60 (computed from the target length;
  // the glide is short enough that the mismatch never matters).
  std::array<float, kLines> gain{};
  for (int i = 0; i < kLines; ++i)
    gain[static_cast<size_t>(i)] = static_cast<float>(
        std::pow(10.0, -3.0 * (target[static_cast<size_t>(i)] / sampleRate_) / static_cast<double>(decay)));
  const float lp = 0.7f * damping;
  // Size changes glide over roughly 50 ms, a slight pitch bend in the tail.
  const float glide = static_cast<float>(1.0 - std::exp(-1.0 / (0.05 * sampleRate_)));

  constexpr std::array<float, kLines> kInSign = {1, -1, 1, -1, -1, 1, -1, 1};
  constexpr float kOutScale = 0.5f;

  for (int i = 0; i < numSamples; ++i) {
    const float wetGain = mix_.getNextValue();
    const float in = input_.getNextValue();
    const float dryL = left[i];
    const float dryR = right ? right[i] : dryL;

    float x = 0.5f * (dryL + dryR) * in;
    for (auto& d : diffusers_)
      x = d.process(x);

    std::array<float, kLines> taps{};
    for (int k = 0; k < kLines; ++k) {
      const auto ku = static_cast<size_t>(k);
      length_[ku] += glide * (target[ku] - length_[ku]);
      float t = readFrac(lines_[ku], write_[ku], std::max(1.0f, length_[ku]));
      lowpass_[ku] += (1.0f - lp) * (t - lowpass_[ku]);
      taps[ku] = lowpass_[ku] * gain[ku];
    }

    const float outL = (taps[0] + taps[2] + taps[4] + taps[6]) * kOutScale;
    const float outR = (taps[1] + taps[3] + taps[5] + taps[7]) * kOutScale;

    hadamard8(taps);
    for (int k = 0; k < kLines; ++k) {
      const auto ku = static_cast<size_t>(k);
      auto& line = lines_[ku];
      line[static_cast<size_t>(write_[ku])] = taps[ku] + kInSign[ku] * x;
      if (++write_[ku] >= static_cast<int>(line.size()))
        write_[ku] = 0;
    }

    if (right) {
      left[i] = dryL + wetGain * outL;
      right[i] = dryR + wetGain * outR;
    } else {
      left[i] = dryL + wetGain * 0.5f * (outL + outR);
    }
  }
}

// ============================================================================
// Modulation: one modulated delay per side, LFO 90 degrees apart. Chorus
// sweeps around 7 ms; flanger sweeps 0.5..4.5 ms with feedback.
// ============================================================================

namespace {
constexpr double kModLineSeconds = 0.03;
}

void Modulation::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  const auto size = static_cast<size_t>(std::ceil(kModLineSeconds * sampleRate_)) + 4;
  for (auto& l : line_)
    l.assign(size, 0.0f);
  mix_.reset(sampleRate_, kFadeSeconds);
  depth_.reset(sampleRate_, 0.05);
  rate_.reset(sampleRate_, 0.05);
  reset();
}

void Modulation::reset() {
  for (auto& l : line_)
    std::fill(l.begin(), l.end(), 0.0f);
  write_ = 0;
  phase_ = 0.0;
  feedback_ = {};
  primed_ = false;
  mix_.setCurrentAndTargetValue(0.0f);
}

void Modulation::process(float* left, float* right, int numSamples, const ModParams& p) {
  if (line_[0].empty() || numSamples <= 0)
    return;

  const float rate = clampFinite(p.rateHz, 0.05f, 10.0f, 0.8f);
  const float depth = clampFinite(p.depth, 0.0f, 1.0f, 0.5f);
  const float mix = clampFinite(p.mix, 0.0f, 1.0f, 0.5f);

  if (!primed_) {
    depth_.setCurrentAndTargetValue(depth);
    rate_.setCurrentAndTargetValue(rate);
    primed_ = true;
  }
  depth_.setTargetValue(depth);
  rate_.setTargetValue(rate);
  mix_.setTargetValue(p.on ? mix : 0.0f);

  if (!p.on && !mix_.isSmoothing()) {
    if (mix_.getCurrentValue() == 0.0f && (feedback_[0] != 0.0f || feedback_[1] != 0.0f || write_ != 0)) {
      // Just went idle: clear so a later switch-on starts from silence.
      for (auto& l : line_)
        std::fill(l.begin(), l.end(), 0.0f);
      feedback_ = {};
      write_ = 0;
    }
    return;
  }

  const int size = static_cast<int>(line_[0].size());
  const double sr = sampleRate_;
  const bool flanger = p.flanger;
  const float centerMs = flanger ? 2.5f : 7.0f;
  const float sweepMs = flanger ? 2.0f : 4.0f;
  const float fbAmount = flanger ? 0.6f : 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    const float m = mix_.getNextValue();
    const float dpt = depth_.getNextValue();
    const double r = rate_.getNextValue();

    phase_ += r / sr;
    if (phase_ >= 1.0)
      phase_ -= 1.0;
    const float lfoL = static_cast<float>(std::sin(kTwoPi * phase_));
    const float lfoR = static_cast<float>(std::cos(kTwoPi * phase_));

    const float dryL = left[i];
    const float dryR = right ? right[i] : dryL;

    const float msL = centerMs + sweepMs * dpt * lfoL;
    const float msR = centerMs + sweepMs * dpt * lfoR;
    const float dL = std::clamp(static_cast<float>(msL * 0.001 * sr), 1.0f, static_cast<float>(size - 2));
    const float dR = std::clamp(static_cast<float>(msR * 0.001 * sr), 1.0f, static_cast<float>(size - 2));

    const float wetL = readFrac(line_[0], write_, dL);
    const float wetR = readFrac(line_[1], write_, dR);
    feedback_[0] = wetL;
    feedback_[1] = wetR;
    line_[0][static_cast<size_t>(write_)] = dryL + fbAmount * wetL;
    line_[1][static_cast<size_t>(write_)] = dryR + fbAmount * wetR;
    if (++write_ >= size)
      write_ = 0;

    // Flanger feedback raises the wet level; scale it back so Mix compares.
    const float wetScale = flanger ? (1.0f - fbAmount) : 1.0f;
    if (right) {
      left[i] = dryL + m * (wetL * wetScale - dryL);
      right[i] = dryR + m * (wetR * wetScale - dryR);
    } else {
      left[i] = dryL + m * (0.5f * (wetL + wetR) * wetScale - dryL);
    }
  }
}

// ============================================================================
// Rack
// ============================================================================

void Rack::prepare(double sampleRate, int /*maxBlockSize*/) {
  delay_.prepare(sampleRate);
  reverb_.prepare(sampleRate);
  mod_.prepare(sampleRate);
  reorderGain_.reset(sampleRate, kReorderSeconds);
  reset();
}

void Rack::reset() {
  delay_.reset();
  reverb_.reset();
  mod_.reset();
  pendingOrder_ = -1;
  reorderGain_.setCurrentAndTargetValue(1.0f);
}

void Rack::process(juce::AudioBuffer<float>& buffer, const Params& p, double bpm) {
  const int numSamples = buffer.getNumSamples();
  if (numSamples <= 0 || buffer.getNumChannels() < 1)
    return;
  float* left = buffer.getWritePointer(0);
  float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  const int wanted = std::clamp(p.order, 0, static_cast<int>(kOrders.size()) - 1);
  if (wanted != order_ && pendingOrder_ != wanted) {
    pendingOrder_ = wanted;
    reorderGain_.setTargetValue(0.0f);
  }
  if (pendingOrder_ >= 0 && !reorderGain_.isSmoothing() && reorderGain_.getCurrentValue() <= 0.0f) {
    order_ = pendingOrder_;
    pendingOrder_ = -1;
    reorderGain_.setTargetValue(1.0f);
  }

  for (const Effect e : kOrders[static_cast<size_t>(order_)]) {
    switch (e) {
      case Effect::mod: mod_.process(left, right, numSamples, p.mod); break;
      case Effect::delay: delay_.process(left, right, numSamples, p.delay, bpm); break;
      case Effect::reverb: reverb_.process(left, right, numSamples, p.reverb); break;
    }
  }

  if (reorderGain_.isSmoothing() || reorderGain_.getCurrentValue() < 1.0f) {
    for (int i = 0; i < numSamples; ++i) {
      const float g = reorderGain_.getNextValue();
      left[i] *= g;
      if (right)
        right[i] *= g;
    }
  }
}

double Rack::tailSeconds(const Params& p, double bpm) {
  double tail = 0.0;
  if (p.delay.on) {
    const double d = Delay::delaySeconds(p.delay, bpm);
    const double fb = std::clamp(static_cast<double>(p.delay.feedback), 0.0, 0.95);
    const double repeats = fb > 0.001 ? std::log(0.001) / std::log(fb) : 1.0;
    tail = std::max(tail, std::min((repeats + 1.0) * d, 30.0));
  }
  if (p.reverb.on)
    tail = std::max(tail, std::clamp(static_cast<double>(p.reverb.decayS), 0.2, kMaxReverbSeconds) + 0.2);
  if (p.mod.on)
    tail = std::max(tail, kModLineSeconds);
  return tail;
}

}  // namespace timefx
