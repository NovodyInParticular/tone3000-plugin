#include "GlitchFx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace glitchfx {

namespace {

constexpr int kMaxChannels = 2;
constexpr double kMaxSampleRate = 192000.0;
constexpr float kSwitchSeconds = 0.025f;   // power on / off
constexpr float kSmoothSeconds = 0.02f;    // mix, output, bits, rate
constexpr double kXfadeMs = 3.0;           // between glitch sources
constexpr double kEdgeMs = 2.0;            // fade at a slice's ends
constexpr double kGrainSeconds = 0.12;     // freeze grains
constexpr float kHeadroom = 1.4f;          // wet ceiling over the loudest source
constexpr double kEnvReleaseSeconds = 3.0; // input peak follower
constexpr int kCrushUpdate = 16;           // samples between quantiser updates

// SplitMix64: the seeded pattern hash and the free-running generator.
std::uint64_t mix64(std::uint64_t z) {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
double unit(std::uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

// Where a glitch source reads from.
enum class Mode { live, loop, reverse, silence, pitched };

struct Reader {
  Mode mode = Mode::live;
  int buf = -1;            // which slice buffer holds the audio
  int len = 1;             // slice length
  int period = 1;          // samples per repeat
  int play = 1;            // samples of each repeat that sound
  double rate = 1.0;       // pitched: read speed
  float depth = 1.0f;      // silence: how deep the dropout is
  std::int64_t t = 0;      // samples since the reader started
  float peak = 0.0f;       // the slice's loudest sample (the safety ceiling)
};

}  // namespace

struct Engine::Impl {
  double sampleRate = 48000.0;
  int ringSize = 0, ringMask = 0;
  std::array<std::vector<float>, kMaxChannels> ring;
  std::int64_t written = 0;
  // A slice is copied out of the ring when it engages, so a stutter can be
  // held for as long as you like without the ring overwriting it. Two
  // buffers: the playing source and the one it is crossfading from.
  std::array<std::array<std::vector<float>, kMaxChannels>, 2> slices;
  int maxSlice = 1;

  Reader cur, prev;
  int xfadeLen = 1, xfadeLeft = 0, edgeLen = 1;

  // Grid / random glitches.
  double freePos = 0.0;  // step position without a song grid
  std::int64_t eventStepsLeft = 0;
  std::uint64_t freeState = 0x1234567ull;
  int lastRate = -1;
  bool manualStutter = false;

  // Freeze.
  std::array<std::vector<float>, kMaxChannels> frozen;
  int frozenLen = 0, grainLen = 1;
  std::vector<float> grainWindow;
  std::array<int, 2> grainStart{}, grainPhase{};
  float frozenPeak = 0.0f;
  bool freezeHeld = false;
  juce::LinearSmoothedValue<float> freezeGain;
  float freezeFadeMs = -1.0f;

  // Crush.
  float holdL = 0.0f, holdR = 0.0f;
  double dsPhase = 1.0;
  float quantStep = 0.0f;  // 0 = off

  float env = 0.0f, envRelease = 0.0f;

  juce::LinearSmoothedValue<float> onGain, mix, outGain, bits, rateHz;

  float read(int ch, std::int64_t index) const {
    return ring[static_cast<size_t>(ch)][static_cast<size_t>(index & ringMask)];
  }
  float sliceAt(const Reader& r, int ch, int index) const {
    return slices[static_cast<size_t>(r.buf)][static_cast<size_t>(ch)][static_cast<size_t>(index)];
  }

  // A reader over a copy of the last `len` samples, repeating every
  // `period`. Takes the slice buffer the playing source isn't using.
  Reader sliceReader(Mode mode, int period, int len) {
    Reader r;
    r.mode = mode;
    r.period = std::max(1, period);
    r.len = std::clamp(len, 1, maxSlice);
    r.play = std::min(r.len, r.period);
    r.buf = cur.buf == 0 ? 1 : 0;
    const std::int64_t from = written - r.len;
    float peak = 0.0f;
    for (int ch = 0; ch < kMaxChannels; ++ch) {
      auto& dst = slices[static_cast<size_t>(r.buf)][static_cast<size_t>(ch)];
      for (int i = 0; i < r.len; ++i) {
        const float v = from + i >= 0 ? read(ch, from + i) : 0.0f;
        dst[static_cast<size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
      }
    }
    r.peak = peak;
    return r;
  }

  void switchTo(const Reader& next) {
    prev = cur;
    cur = next;
    xfadeLeft = xfadeLen;
  }

  // Fade-in/out gain at position `pos` of a slice `len` long.
  float edge(double pos, int len) const {
    const double g = std::min(pos, static_cast<double>(len) - pos) / static_cast<double>(edgeLen);
    return static_cast<float>(std::clamp(g, 0.0, 1.0));
  }

  // The source's output for both channels at the current sample; `liveL/R`
  // is this sample's input.
  void sample(Reader& r, float liveL, float liveR, float& outL, float& outR) const {
    switch (r.mode) {
      case Mode::live:
        outL = liveL;
        outR = liveR;
        break;
      case Mode::silence:
        outL = liveL * (1.0f - r.depth);
        outR = liveR * (1.0f - r.depth);
        break;
      case Mode::loop:
      case Mode::reverse: {
        const auto inPeriod = static_cast<int>(r.t % r.period);
        if (inPeriod >= r.play) {
          outL = outR = 0.0f;
          break;
        }
        const int pos = r.mode == Mode::loop ? inPeriod : r.play - 1 - inPeriod;
        const float g = edge(inPeriod, r.play);
        outL = sliceAt(r, 0, pos) * g;
        outR = sliceAt(r, 1, pos) * g;
        break;
      }
      case Mode::pitched: {
        const double pos = std::fmod(static_cast<double>(r.t) * r.rate, static_cast<double>(r.len));
        const float g = edge(pos, r.len);
        const int i0 = static_cast<int>(pos);
        const int i1 = i0 + 1 < r.len ? i0 + 1 : 0;
        const auto f = static_cast<float>(pos - i0);
        outL = (sliceAt(r, 0, i0) + (sliceAt(r, 0, i1) - sliceAt(r, 0, i0)) * f) * g;
        outR = (sliceAt(r, 1, i0) + (sliceAt(r, 1, i1) - sliceAt(r, 1, i0)) * f) * g;
        break;
      }
    }
    ++r.t;
  }

  void captureFreeze() {
    frozenLen = std::min(static_cast<int>(frozen[0].size()), static_cast<int>(std::min<std::int64_t>(written, ringSize - 1)));
    if (frozenLen < 2) frozenLen = 0;
    const std::int64_t from = written - frozenLen;
    frozenPeak = 0.0f;
    for (int ch = 0; ch < kMaxChannels; ++ch)
      for (int i = 0; i < frozenLen; ++i) {
        const float v = read(ch, from + i);
        frozen[static_cast<size_t>(ch)][static_cast<size_t>(i)] = v;
        frozenPeak = std::max(frozenPeak, std::abs(v));
      }
    grainLen = std::max(1, std::min(static_cast<int>(grainWindow.size()), frozenLen / 2));
    grainPhase = {0, grainLen / 2};
    for (auto& s : grainStart) s = nextGrainStart();
  }

  int nextGrainStart() {
    freeState = mix64(freeState);
    const int span = std::max(1, frozenLen - grainLen);
    return static_cast<int>(unit(freeState) * span);
  }

  void frozenSample(float& l, float& r) {
    l = r = 0.0f;
    if (frozenLen <= 0) return;
    const int winLen = static_cast<int>(grainWindow.size());
    for (size_t g = 0; g < 2; ++g) {
      // Hann over grainLen, read from the precomputed table.
      const auto wi = static_cast<size_t>(static_cast<std::int64_t>(grainPhase[g]) * winLen / grainLen);
      const float w = grainWindow[std::min(wi, grainWindow.size() - 1)];
      const auto at = static_cast<size_t>(grainStart[g] + grainPhase[g]);
      l += frozen[0][at] * w;
      r += frozen[1][at] * w;
      if (++grainPhase[g] >= grainLen) {
        grainPhase[g] = 0;
        grainStart[g] = nextGrainStart();
      }
    }
  }

  void resetState() {
    for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
    written = 0;
    cur = prev = Reader{};
    xfadeLeft = 0;
    freePos = 0.0;
    eventStepsLeft = 0;
    manualStutter = false;
    lastRate = -1;
    frozenLen = 0;
    frozenPeak = 0.0f;
    freezeHeld = false;
    freezeGain.setCurrentAndTargetValue(0.0f);
    holdL = holdR = 0.0f;
    dsPhase = 1.0;
    env = 0.0f;
  }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
  auto& m = *impl_;
  m.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
  // Sized for the worst case at this rate: the slowest grid step at the
  // slowest tempo, plus a block.
  const int needed = static_cast<int>(std::ceil(m.sampleRate * kRingSeconds)) + std::max(1, maxBlockSize) + 16;
  m.ringSize = juce::nextPowerOfTwo(needed);
  m.ringMask = m.ringSize - 1;
  for (auto& r : m.ring) r.assign(static_cast<size_t>(m.ringSize), 0.0f);
  // The longest slice: a quarter note at the slowest tempo.
  m.maxSlice = static_cast<int>(std::ceil(kRateBeats[0] * 60.0 / kMinBpm * m.sampleRate));
  for (auto& slice : m.slices)
    for (auto& ch : slice) ch.assign(static_cast<size_t>(m.maxSlice), 0.0f);
  const auto freezeLen = static_cast<size_t>(std::ceil(m.sampleRate * kFreezeSeconds));
  for (auto& f : m.frozen) f.assign(freezeLen, 0.0f);
  const auto grain = static_cast<size_t>(std::max(16.0, m.sampleRate * kGrainSeconds));
  m.grainWindow.resize(grain);
  for (size_t i = 0; i < grain; ++i)
    m.grainWindow[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) /
                                               static_cast<float>(grain));
  m.xfadeLen = std::max(1, static_cast<int>(m.sampleRate * kXfadeMs * 0.001));
  m.edgeLen = std::max(1, static_cast<int>(m.sampleRate * kEdgeMs * 0.001));
  m.envRelease = static_cast<float>(std::exp(-1.0 / (m.sampleRate * kEnvReleaseSeconds)));
  m.onGain.reset(m.sampleRate, kSwitchSeconds);
  m.mix.reset(m.sampleRate, kSmoothSeconds);
  m.outGain.reset(m.sampleRate, kSmoothSeconds);
  m.bits.reset(m.sampleRate, kSmoothSeconds);
  m.rateHz.reset(m.sampleRate, kSmoothSeconds);
  m.freezeGain.reset(m.sampleRate, 0.15);
  m.freezeFadeMs = -1.0f;
  reset();
}

void Engine::reset() {
  auto& m = *impl_;
  m.resetState();
  running_ = false;
  m.onGain.setCurrentAndTargetValue(0.0f);
  activity_.freeze.store(0.0f, std::memory_order_relaxed);
  activity_.stuttering.store(false, std::memory_order_relaxed);
}

double Engine::tailSeconds(const Params& params) {
  if (!params.on) return 0.0;
  return static_cast<double>(std::clamp(params.freezeFadeMs, kMinFreezeFadeMs, kMaxFreezeFadeMs)) * 0.001 + 0.05;
}

void Engine::process(juce::AudioBuffer<float>& buffer, const Params& p, const Transport& transport) {
  auto& m = *impl_;
  const int numSamples = buffer.getNumSamples();
  const int numChannels = std::min(buffer.getNumChannels(), kMaxChannels);
  if (m.ringSize == 0 || numSamples <= 0 || numChannels <= 0) return;
  if (!p.on && !running_) return;
  if (p.on && !running_) {
    m.resetState();
    m.freeState = mix64(m.freeState + 0x51ull);  // a new free-running pattern each time
    m.onGain.setCurrentAndTargetValue(0.0f);
    m.mix.setCurrentAndTargetValue(p.mix);
    m.outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(p.outputDb));
    m.bits.setCurrentAndTargetValue(p.bits);
    m.rateHz.setCurrentAndTargetValue(p.rateHz);
    running_ = true;
  }
  m.onGain.setTargetValue(p.on ? 1.0f : 0.0f);
  m.mix.setTargetValue(std::clamp(p.mix, 0.0f, 1.0f));
  m.outGain.setTargetValue(juce::Decibels::decibelsToGain(std::clamp(p.outputDb, -24.0f, 12.0f)));
  m.bits.setTargetValue(std::clamp(p.bits, kMinBits, kMaxBits));
  m.rateHz.setTargetValue(std::clamp(p.rateHz, kMinRateHz, kMaxRateHz));

  // Freeze edges.
  const float fadeMs = std::clamp(p.freezeFadeMs, kMinFreezeFadeMs, kMaxFreezeFadeMs);
  if (!juce::exactlyEqual(fadeMs, m.freezeFadeMs)) {
    const float now = m.freezeGain.getCurrentValue();
    const float target = m.freezeGain.getTargetValue();
    m.freezeGain.reset(m.sampleRate, fadeMs * 0.001);
    m.freezeGain.setCurrentAndTargetValue(now);
    m.freezeGain.setTargetValue(target);
    m.freezeFadeMs = fadeMs;
  }
  if (p.freeze != m.freezeHeld) {
    m.freezeHeld = p.freeze;
    // Re-engaging while the old drone is still fading out keeps it (no jump).
    if (p.freeze && m.freezeGain.getCurrentValue() <= 0.0f) m.captureFreeze();
    m.freezeGain.setTargetValue(p.freeze ? 1.0f : 0.0f);
  }

  // Grid.
  const double bpm = std::clamp(transport.bpm > 0.0 ? transport.bpm : 120.0, kMinBpm, kMaxBpm);
  const int rate = std::clamp(p.rate, 0, static_cast<int>(kRateBeats.size()) - 1);
  const double beats = kRateBeats[static_cast<size_t>(rate)];
  const double stepSamples = beats * 60.0 / bpm * m.sampleRate;
  const int step = std::max(1, static_cast<int>(std::lround(stepSamples)));
  // With a seed and a running transport the grid is the song's, so a
  // pattern lands on the same bars every playback.
  const bool songGrid = transport.playing && p.seed > 0;
  const double stepsPerSample = 1.0 / stepSamples;
  double pos = songGrid ? transport.ppq / beats : m.freePos;
  const float length = std::clamp(p.length, 0.1f, 1.0f);
  const double chance = std::clamp(static_cast<double>(p.chance), 0.0, 1.0);
  const double intensity = std::clamp(static_cast<double>(p.intensity), 0.0, 1.0);

  // Manual stutter edges (and a rate change while held) restart the loop
  // on the freshest slice.
  if (p.stutter != m.manualStutter || (p.stutter && rate != m.lastRate)) {
    if (p.stutter) {
      const int play = std::max(1, static_cast<int>(std::lround(step * length)));
      m.switchTo(m.sliceReader(Mode::loop, step, play));
      activity_.events.fetch_add(1, std::memory_order_relaxed);
      activity_.lastEvent.store(static_cast<int>(Event::stutter), std::memory_order_relaxed);
    } else {
      m.switchTo(Reader{});
    }
    m.eventStepsLeft = 0;
    m.manualStutter = p.stutter;
  }
  m.lastRate = rate;

  float* l = buffer.getWritePointer(0);
  float* r = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

  for (int i = 0; i < numSamples; ++i) {
    // A non-finite input sample would poison the ring, the replays and the
    // safety ceiling; it is recorded and passed on as silence.
    const float inL = std::isfinite(l[i]) ? l[i] : 0.0f;
    const float inR = r ? (std::isfinite(r[i]) ? r[i] : 0.0f) : inL;

    // Record.
    const auto w = static_cast<size_t>(m.written & m.ringMask);
    m.ring[0][w] = inL;
    m.ring[1][w] = inR;
    const float a = std::max(std::abs(inL), std::abs(inR));
    ++m.written;
    m.env = std::max(a, m.env * m.envRelease);

    // Grid step boundaries: random glitches start and end here.
    const double nextPos = pos + stepsPerSample;
    if (std::floor(nextPos) != std::floor(pos) && !m.manualStutter) {
      const auto stepIndex = static_cast<std::int64_t>(std::floor(nextPos));
      if (m.eventStepsLeft > 0 && --m.eventStepsLeft == 0) m.switchTo(Reader{});
      if (m.eventStepsLeft == 0 && chance > 0.0) {
        std::uint64_t h;
        if (p.seed > 0) {
          h = mix64(static_cast<std::uint64_t>(p.seed) * 0x100000001B3ull ^ static_cast<std::uint64_t>(stepIndex));
        } else {
          m.freeState = mix64(m.freeState);
          h = m.freeState;
        }
        if (unit(h) < chance) {
          const std::uint64_t h2 = mix64(h ^ 0xA5A5A5A5ull);
          const std::uint64_t h3 = mix64(h2);
          // Kind: stutters and dropouts always; reverses and octave jumps
          // grow with intensity.
          const double wStutter = 1.0, wDrop = 0.6, wRev = 0.3 + intensity, wPitch = 0.15 + intensity;
          double pick = unit(h2) * (wStutter + wDrop + wRev + wPitch);
          Event kind = Event::stutter;
          if ((pick -= wStutter) >= 0.0) {
            kind = Event::dropout;
            if ((pick -= wDrop) >= 0.0) {
              kind = Event::reverse;
              if ((pick -= wRev) >= 0.0) kind = (h3 & 1) ? Event::pitchUp : Event::pitchDown;
            }
          }
          m.eventStepsLeft = 1 + static_cast<std::int64_t>(unit(h3) * (1.0 + intensity * 3.0));
          Reader next;
          switch (kind) {
            case Event::stutter: {
              // Higher intensity: faster repeats inside the step.
              const int div = 1 << static_cast<int>(unit(mix64(h3)) * (1.0 + intensity * 2.99));
              next = m.sliceReader(Mode::loop, std::max(1, step / div), std::max(1, step / div));
              break;
            }
            case Event::reverse: next = m.sliceReader(Mode::reverse, step, step); break;
            case Event::dropout:
              next.mode = Mode::silence;
              next.depth = static_cast<float>(0.6 + 0.4 * intensity);
              break;
            case Event::pitchUp:
            case Event::pitchDown:
              next = m.sliceReader(Mode::pitched, step, step);
              next.rate = kind == Event::pitchUp ? 2.0 : 0.5;
              break;
            case Event::none: break;
          }
          m.switchTo(next);
          activity_.events.fetch_add(1, std::memory_order_relaxed);
          activity_.lastEvent.store(static_cast<int>(kind), std::memory_order_relaxed);
        }
      }
    }
    pos = nextPos;

    // Source, crossfaded from the previous one after a switch.
    float wl, wr;
    m.sample(m.cur, inL, inR, wl, wr);
    if (m.xfadeLeft > 0) {
      float pl, pr;
      m.sample(m.prev, inL, inR, pl, pr);
      const float x = static_cast<float>(m.xfadeLeft) / static_cast<float>(m.xfadeLen);
      wl = wl + (pl - wl) * x;
      wr = wr + (pr - wr) * x;
      --m.xfadeLeft;
    }

    // Freeze drone on top.
    const float fg = m.freezeGain.getNextValue();
    if (fg > 0.0f) {
      float fl, fr;
      m.frozenSample(fl, fr);
      wl += fl * fg;
      wr += fr * fg;
    }

    // Sample-rate reduction (sample and hold), then bit-crush.
    const float hz = m.rateHz.getNextValue();
    if (hz < kMaxRateHz && hz < m.sampleRate) {
      m.dsPhase += static_cast<double>(hz) / m.sampleRate;
      if (m.dsPhase >= 1.0) {
        m.dsPhase -= std::floor(m.dsPhase);
        m.holdL = wl;
        m.holdR = wr;
      }
      wl = m.holdL;
      wr = m.holdR;
    } else {
      m.dsPhase = 1.0;
    }
    const float b = m.bits.getNextValue();
    if ((i % kCrushUpdate) == 0) m.quantStep = b < kMaxBits ? std::exp2(1.0f - b) : 0.0f;
    if (m.quantStep > 0.0f) {
      wl = std::round(wl / m.quantStep) * m.quantStep;
      wr = std::round(wr / m.quantStep) * m.quantStep;
    }

    // Safety ceiling: never much louder than what was played.
    float ceiling = std::max({m.env, m.cur.mode == Mode::live || m.cur.mode == Mode::silence ? 0.0f : m.cur.peak,
                              m.xfadeLeft > 0 ? m.prev.peak : 0.0f, fg > 0.0f ? m.frozenPeak : 0.0f});
    ceiling = ceiling * kHeadroom + 1.0e-6f;
    wl = std::clamp(wl, -ceiling, ceiling);
    wr = std::clamp(wr, -ceiling, ceiling);

    const float mx = m.mix.getNextValue();
    const float og = m.outGain.getNextValue();
    const float on = m.onGain.getNextValue();
    // Exact at both ends: a blend at 1 is exactly the new signal, and a
    // signal equal to the dry blends to exactly the dry.
    const auto blend = [](float dry, float x, float g) { return g >= 1.0f ? x : dry + (x - dry) * g; };
    float ol = blend(inL, wl, mx) * og;
    float orr = blend(inR, wr, mx) * og;
    ol = blend(inL, ol, on);
    orr = blend(inR, orr, on);
    if (!std::isfinite(ol)) ol = 0.0f;
    if (!std::isfinite(orr)) orr = 0.0f;
    l[i] = std::clamp(ol, -kOutputCeiling, kOutputCeiling);
    if (r) r[i] = std::clamp(orr, -kOutputCeiling, kOutputCeiling);
  }

  m.freePos = pos;

  activity_.freeze.store(m.freezeGain.getCurrentValue(), std::memory_order_relaxed);
  activity_.stuttering.store(m.manualStutter || m.cur.mode != Mode::live, std::memory_order_relaxed);
  const float bitsAmt = (kMaxBits - m.bits.getCurrentValue()) / (kMaxBits - kMinBits);
  const float rateAmt = 1.0f - std::log(m.rateHz.getCurrentValue() / kMinRateHz) / std::log(kMaxRateHz / kMinRateHz);
  activity_.crush.store(std::clamp(std::max(bitsAmt, rateAmt), 0.0f, 1.0f), std::memory_order_relaxed);

  if (!p.on && !m.onGain.isSmoothing()) running_ = false;
}

}  // namespace glitchfx
