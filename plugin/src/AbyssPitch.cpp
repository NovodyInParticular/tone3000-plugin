#include "AbyssPitch.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace abyss {

namespace {

constexpr int kMaxChannels = 2;
constexpr int kSub = 32;                   // control-rate sub-block
constexpr float kSwitchSeconds = 0.025f;   // power on / off
constexpr float kLevelSeconds = 0.02f;
constexpr float kShiftSeconds = 0.03f;     // shift / interval / detune moves
constexpr float kDipSeconds = 0.01f;       // tracking change
// A voice starts contributing once its shifter has a buffer of audio and
// has blended in (PitchShift's priming plus its 25 ms blend).
constexpr double kWarmExtraMs = 30.0;
constexpr float kOutputCeiling = 4.0f;
// Stereo spread of the shift pair after the amps: the far side's gain.
constexpr float kSpreadFar = 0.55f;

}  // namespace

PitchShift::Window windowFor(int tracking) {
  return tracking == 1 ? PitchShift::Window::ms40 : PitchShift::Window::ms20;
}

int latencySamples(int tracking, double sampleRate) {
  return PitchShift::latencySamples(windowFor(tracking), sampleRate);
}

struct Engine::Impl {
  double sampleRate = 48000.0;
  struct VoiceState {
    PitchShift shifter;
    bool enabled = false;
    int warm = 0;  // samples until it may be heard
    juce::LinearSmoothedValue<float> gain;
    float peak = 0.0f;
  };
  std::array<VoiceState, kVoices> voices;
  juce::AudioBuffer<float> scratch;  // one voice's sub-block

  // Dry delay matching the shifters' latency.
  std::array<std::vector<float>, kMaxChannels> dryRing;
  int dryMask = 0;
  std::int64_t written = 0;
  int dryDelay = 0;
  int tracking = 0, pendingTracking = 0;

  juce::LinearSmoothedValue<float> onGain, dry, wet, outGain, dip, shift, detune, bend;
  float glideMs = -1.0f;
  int warmSamples = 1;
  float inPeak = 0.0f;

  void applyTracking(int t) {
    tracking = t;
    dryDelay = latencySamples(t, sampleRate);
  }

  void resetState() {
    for (auto& r : dryRing) std::fill(r.begin(), r.end(), 0.0f);
    written = 0;
    for (auto& v : voices) {
      v.enabled = false;
      v.shifter.stop();  // the next enable starts with fresh rings
      v.gain.setCurrentAndTargetValue(0.0f);
      v.warm = 0;
      v.peak = 0.0f;
    }
    dip.setCurrentAndTargetValue(1.0f);
  }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
  juce::ignoreUnused(maxBlockSize);
  auto& m = *impl_;
  m.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
  const int maxDelay = latencySamples(1, m.sampleRate) + kSub + 8;
  const int size = juce::nextPowerOfTwo(maxDelay + 1);
  m.dryMask = size - 1;
  for (auto& r : m.dryRing) r.assign(static_cast<size_t>(size), 0.0f);
  m.scratch.setSize(kMaxChannels, kSub);
  for (auto& v : m.voices) {
    v.shifter.prepare(m.sampleRate, kSub);
    v.gain.reset(m.sampleRate, kLevelSeconds);
  }
  m.onGain.reset(m.sampleRate, kSwitchSeconds);
  for (auto* s : {&m.dry, &m.wet, &m.outGain}) s->reset(m.sampleRate, kLevelSeconds);
  m.dip.reset(m.sampleRate, kDipSeconds);
  // Shift and detune move per sub-block; the ramps count sub-blocks.
  m.shift.reset(m.sampleRate / kSub, kShiftSeconds);
  m.detune.reset(m.sampleRate / kSub, kShiftSeconds);
  m.glideMs = -1.0f;
  m.warmSamples = PitchShift::windowSamples(PitchShift::Window::ms40, m.sampleRate) +
                  static_cast<int>(m.sampleRate * kWarmExtraMs * 0.001);
  m.applyTracking(m.tracking);
  reset();
}

void Engine::reset() {
  auto& m = *impl_;
  m.resetState();
  running_ = false;
  m.onGain.setCurrentAndTargetValue(0.0f);
  for (auto& l : activity_.level) l.store(0.0f, std::memory_order_relaxed);
}

void Engine::process(juce::AudioBuffer<float>& buffer, const Params& p) {
  auto& m = *impl_;
  const int numSamples = buffer.getNumSamples();
  const int numChannels = std::min(buffer.getNumChannels(), kMaxChannels);
  if (m.dryMask == 0 || numSamples <= 0 || numChannels <= 0) return;
  if (!p.on && !running_) return;

  const int tracking = std::clamp(p.tracking, 0, 1);
  const float interval = kIntervalSemitones[static_cast<size_t>(
      std::clamp(p.interval, 0, static_cast<int>(kIntervalSemitones.size()) - 1))];
  const float shiftTarget =
      p.interval > 0 ? interval : std::clamp(p.shiftSemitones, -kShiftRange, kShiftRange);
  const float range = p.bendRange == 1 ? 24.0f : 12.0f;
  const float bendTarget = std::clamp(p.bend, -1.0f, 1.0f) * range;
  const float glide = std::clamp(p.glideMs, 0.0f, kMaxGlideMs);

  if (p.on && !running_) {
    m.resetState();
    m.applyTracking(tracking);
    m.pendingTracking = tracking;
    for (auto& v : m.voices) v.shifter.setParams({0.0f, 0.0f, windowFor(tracking)});
    m.onGain.setCurrentAndTargetValue(0.0f);
    m.dry.setCurrentAndTargetValue(p.dry);
    m.wet.setCurrentAndTargetValue(p.wet);
    m.outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(p.outputDb));
    m.shift.setCurrentAndTargetValue(shiftTarget);
    m.detune.setCurrentAndTargetValue(p.detuneCents);
    m.bend.reset(m.sampleRate / kSub, glide * 0.001);
    m.bend.setCurrentAndTargetValue(bendTarget);
    m.glideMs = glide;
    running_ = true;
  }
  // Switching on waits for the dry delay to fill: until then the delayed
  // path is silence, and blending toward it would dip and then jump.
  m.onGain.setTargetValue(p.on && m.written >= m.dryDelay ? 1.0f : 0.0f);
  m.dry.setTargetValue(std::clamp(p.dry, 0.0f, 1.0f));
  m.wet.setTargetValue(std::clamp(p.wet, 0.0f, 1.0f));
  m.outGain.setTargetValue(juce::Decibels::decibelsToGain(std::clamp(p.outputDb, -24.0f, 12.0f)));
  m.shift.setTargetValue(shiftTarget);
  m.detune.setTargetValue(std::clamp(p.detuneCents, 0.0f, kMaxDetuneCents));
  if (!juce::exactlyEqual(glide, m.glideMs)) {
    const float now = m.bend.getCurrentValue();
    m.bend.reset(m.sampleRate / kSub, glide * 0.001);
    m.bend.setCurrentAndTargetValue(now);
    m.glideMs = glide;
  }
  m.bend.setTargetValue(bendTarget);

  // A tracking change: dip, switch at the bottom, come back.
  if (tracking != m.tracking) {
    m.pendingTracking = tracking;
    m.dip.setTargetValue(0.0f);
  }

  const std::array<float, kVoices> levels = {p.subTwoLevel, p.subOneLevel, p.upLevel, p.shiftLevel, p.shiftLevel};
  for (int v = 0; v < kVoices; ++v) {
    auto& voice = m.voices[static_cast<size_t>(v)];
    // Each of the shift pair carries half, so with no detune they sum to
    // the knob's level.
    const float target = std::clamp(levels[static_cast<size_t>(v)], 0.0f, 1.0f) * (v >= shiftA ? 0.5f : 1.0f);
    const bool want = p.on && target > 0.0f;
    if (want && !voice.enabled) {
      voice.enabled = true;
      if (!voice.shifter.isRunning()) voice.warm = m.warmSamples;
      voice.shifter.setEnabled(true);
    }
    voice.gain.setTargetValue(want && voice.warm <= 0 ? target : 0.0f);
    if (!want && voice.enabled && voice.gain.getCurrentValue() <= 0.0f) {
      voice.enabled = false;
      voice.shifter.setEnabled(false);
    }
  }

  float* io[kMaxChannels] = {buffer.getWritePointer(0), buffer.getWritePointer(numChannels > 1 ? 1 : 0)};
  float inPeak = 0.0f;
  std::array<float, kVoices> peaks{};

  for (int start = 0; start < numSamples; start += kSub) {
    const int n = std::min(kSub, numSamples - start);
    const float shift = m.shift.skip(1);
    const float detune = m.detune.skip(1) / 100.0f;  // semitones
    const float bend = m.bend.skip(1);
    const std::array<float, kVoices> semis = {-24.0f + bend + 0.5f * detune, -12.0f + bend - 0.5f * detune,
                                              12.0f + bend + 0.5f * detune, shift + bend + 0.5f * detune,
                                              shift + bend - 0.5f * detune};

    // This sub-block's wet sum.
    float wetL[kSub] = {}, wetR[kSub] = {};
    for (int v = 0; v < kVoices; ++v) {
      auto& voice = m.voices[static_cast<size_t>(v)];
      if (!voice.shifter.isRunning()) {
        voice.gain.setCurrentAndTargetValue(0.0f);
        continue;
      }
      for (int ch = 0; ch < kMaxChannels; ++ch)
        m.scratch.copyFrom(ch, 0, io[ch] + start, n);
      juce::AudioBuffer<float> view(m.scratch.getArrayOfWritePointers(), kMaxChannels, 0, n);
      voice.shifter.setParams({std::clamp(semis[static_cast<size_t>(v)], -24.0f, 24.0f), 0.0f, windowFor(m.tracking)});
      voice.shifter.process(view);
      const float* sl = m.scratch.getReadPointer(0);
      const float* sr = m.scratch.getReadPointer(1);
      // After the amps the shift pair leans apart in the stereo field.
      float gl = 1.0f, gr = 1.0f;
      if (p.stereoSpread && numChannels > 1) {
        if (v == shiftA) gr = kSpreadFar;
        if (v == shiftB) gl = kSpreadFar;
      }
      float pk = 0.0f;
      for (int i = 0; i < n; ++i) {
        const float g = voice.gain.getNextValue();
        wetL[i] += sl[i] * g * gl;
        wetR[i] += sr[i] * g * gr;
        pk = std::max(pk, std::abs(sl[i]) * g);
      }
      peaks[static_cast<size_t>(v)] = std::max(peaks[static_cast<size_t>(v)], pk);
      if (voice.warm > 0) {
        voice.warm -= n;
        if (voice.warm <= 0 && voice.enabled)
          voice.gain.setTargetValue(std::clamp(levels[static_cast<size_t>(v)], 0.0f, 1.0f) * (v >= shiftA ? 0.5f : 1.0f));
      }
    }

    for (int i = 0; i < n; ++i) {
      const int idx = start + i;
      float in[kMaxChannels];
      for (int ch = 0; ch < kMaxChannels; ++ch) {
        const float x = io[ch][idx];
        in[ch] = std::isfinite(x) ? x : 0.0f;
        m.dryRing[static_cast<size_t>(ch)][static_cast<size_t>(m.written & m.dryMask)] = in[ch];
      }
      inPeak = std::max({inPeak, std::abs(in[0]), std::abs(in[1])});
      const auto at = static_cast<size_t>((m.written - m.dryDelay) & m.dryMask);
      ++m.written;
      const float dl = m.dryRing[0][at], dr = m.dryRing[1][at];
      const float dryG = m.dry.getNextValue(), wetG = m.wet.getNextValue(), og = m.outGain.getNextValue();
      const float dip = m.dip.getNextValue();
      if (!m.dip.isSmoothing() && m.dip.getTargetValue() <= 0.0f) {
        // Bottom of a tracking dip: the shifters and the dry delay switch.
        m.applyTracking(m.pendingTracking);
        m.dip.setTargetValue(1.0f);
      }
      float ol = (dl * dryG + wetL[i] * wetG) * og * dip;
      float orr = (dr * dryG + wetR[i] * wetG) * og * dip;
      const float on = m.onGain.getNextValue();
      if (on < 1.0f) {
        ol = in[0] + (ol - in[0]) * on;
        orr = in[1] + (orr - in[1]) * on;
      }
      if (!std::isfinite(ol)) ol = 0.0f;
      if (!std::isfinite(orr)) orr = 0.0f;
      io[0][idx] = std::clamp(ol, -kOutputCeiling, kOutputCeiling);
      if (numChannels > 1) io[1][idx] = std::clamp(orr, -kOutputCeiling, kOutputCeiling);
    }
  }

  for (int v = 0; v < kVoices; ++v) {
    auto& voice = m.voices[static_cast<size_t>(v)];
    voice.peak = std::max(peaks[static_cast<size_t>(v)], voice.peak * 0.8f);
    activity_.level[static_cast<size_t>(v)].store(std::min(1.0f, voice.peak), std::memory_order_relaxed);
  }
  m.inPeak = std::max(inPeak, m.inPeak * 0.8f);
  activity_.input.store(m.inPeak, std::memory_order_relaxed);
  activity_.bend.store(m.bend.getCurrentValue(), std::memory_order_relaxed);
  activity_.detune.store(m.detune.getCurrentValue(), std::memory_order_relaxed);

  if (!p.on && !m.onGain.isSmoothing()) {
    // Faded out: everything stops; the next switch-on starts fresh.
    running_ = false;
    for (auto& v : m.voices) {
      v.enabled = false;
      v.shifter.stop();
    }
    for (auto& l : activity_.level) l.store(0.0f, std::memory_order_relaxed);
  }
}

}  // namespace abyss
