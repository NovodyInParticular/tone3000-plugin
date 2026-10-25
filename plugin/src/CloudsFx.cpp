#include "CloudsFx.h"

#include <algorithm>
#include <cmath>
#include <vector>

// The vendored sources pick their portable C++ paths (no ARM assembly or
// STM32 headers) under TEST, the macro the module's own desktop tests use.
// Scoped to these includes: GoogleTest's TEST() must survive in the tests.
#pragma push_macro("TEST")
#ifndef TEST
#define TEST
#endif
#include "clouds/dsp/granular_processor.h"
#pragma pop_macro("TEST")

namespace cloudsfx {

namespace {

constexpr double kEngineRate = 32000.0;
constexpr int kEngineBlock = static_cast<int>(clouds::kMaxBlockSize);  // 32 frames
// The module's two RAM blocks (clouds/clouds.cc): main SRAM and the CCM.
constexpr size_t kLargeBufferBytes = 118784;
constexpr size_t kSmallBufferBytes = 65536 - 128;
// Wet FIFO between the engine's 32-frame bursts and the host's samples.
// Holds the priming plus one burst at the highest host rate with room to
// spare (32 frames at 32 kHz = 192 samples at 192 kHz).
constexpr int kFifoSize = 1024;
constexpr float kFadeSeconds = 0.02f;
// One-pole smoothing of the knobs once per engine block (1 kHz): ~10 ms,
// like the module's own control-rate filtering, so automation doesn't step.
constexpr float kKnobSmoothing = 0.1f;

struct Frame {
  float l = 0.0f, r = 0.0f;
};

// RBJ low-pass biquad, transposed direct form II, stereo.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float zl1 = 0, zl2 = 0, zr1 = 0, zr2 = 0;

  void setLowPass(double rate, double hz, double q) {
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / rate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double c = std::cos(w);
    const double a0 = 1.0 + alpha;
    b0 = static_cast<float>((1.0 - c) / 2.0 / a0);
    b1 = static_cast<float>((1.0 - c) / a0);
    b2 = b0;
    a1 = static_cast<float>(-2.0 * c / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
  }
  void reset() { zl1 = zl2 = zr1 = zr2 = 0.0f; }
  Frame process(Frame x) {
    const float yl = b0 * x.l + zl1;
    zl1 = b1 * x.l - a1 * yl + zl2;
    zl2 = b2 * x.l - a2 * yl;
    const float yr = b0 * x.r + zr1;
    zr1 = b1 * x.r - a1 * yr + zr2;
    zr2 = b2 * x.r - a2 * yr;
    return {yl, yr};
  }
};

// Fourth-order Butterworth low-pass (two biquads): band-limits the wet path
// on both sides of the rate change.
struct AntiAlias {
  Biquad stages[2];
  void set(double rate, double hz) {
    stages[0].setLowPass(rate, hz, 0.54119610);
    stages[1].setLowPass(rate, hz, 1.30656296);
  }
  void reset() {
    for (auto& s : stages) s.reset();
  }
  Frame process(Frame x) { return stages[1].process(stages[0].process(x)); }
};

// Streaming 4-point Hermite resampler. Push input frames; each push emits
// the output frames that fall between the two middle history points.
struct Resampler {
  double step = 1.0;  // input samples per output sample
  double phase = 0.0;
  Frame h[4];

  void reset() {
    phase = 0.0;
    for (auto& f : h) f = {};
  }
  static float hermite(float xm1, float x0, float x1, float x2, float t) {
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
  }
  template <typename Emit>
  void push(Frame x, Emit&& emit) {
    h[0] = h[1];
    h[1] = h[2];
    h[2] = h[3];
    h[3] = x;
    while (phase < 1.0) {
      const auto t = static_cast<float>(phase);
      emit(Frame{hermite(h[0].l, h[1].l, h[2].l, h[3].l, t), hermite(h[0].r, h[1].r, h[2].r, h[3].r, t)});
      phase += step;
    }
    phase -= 1.0;
  }
};

short toShort(float x) {
  return static_cast<short>(juce::jlimit(-32767.0f, 32767.0f, x * 32767.0f));
}

}  // namespace

struct Engine::Impl {
  // The processor's constructor leaves its members uninitialised (the
  // module keeps it in zeroed static RAM), so it lives in zeroed storage.
  alignas(clouds::GranularProcessor) unsigned char storage[sizeof(clouds::GranularProcessor)] = {};
  clouds::GranularProcessor* processor = nullptr;
  std::vector<uint32_t> largeBuffer = std::vector<uint32_t>(kLargeBufferBytes / sizeof(uint32_t));
  std::vector<uint32_t> smallBuffer = std::vector<uint32_t>(kSmallBufferBytes / sizeof(uint32_t));

  double hostRate = 48000.0;
  AntiAlias downFilter, upFilter;
  Resampler down, up;
  clouds::ShortFrame engineIn[clouds::kMaxBlockSize] = {};
  clouds::ShortFrame engineOut[clouds::kMaxBlockSize] = {};
  int engineFill = 0;

  Frame fifo[kFifoSize];
  int fifoRead = 0, fifoCount = 0;
  int priming = 0;

  juce::SmoothedValue<float> onGain, blend;
  Params smoothed;
  bool knobsPrimed = false;

  Impl() { processor = new (storage) clouds::GranularProcessor(); }
  ~Impl() { processor->~GranularProcessor(); }

  void restart() {
    std::fill(largeBuffer.begin(), largeBuffer.end(), 0u);
    std::fill(smallBuffer.begin(), smallBuffer.end(), 0u);
    processor->Init(largeBuffer.data(), kLargeBufferBytes, smallBuffer.data(), kSmallBufferBytes);
    processor->set_silence(false);
    processor->set_bypass(false);
    processor->set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
    processor->set_quality(0);
    downFilter.reset();
    upFilter.reset();
    down.reset();
    up.reset();
    engineFill = 0;
    // Prime the FIFO with one burst of silence so the host never waits on
    // the engine's next 32-frame block.
    fifoRead = 0;
    fifoCount = std::min(priming, kFifoSize);
    for (int i = 0; i < fifoCount; ++i) fifo[i] = {};
    knobsPrimed = false;
  }

  void applyParams(const Params& p) {
    auto& target = smoothed;
    if (!knobsPrimed) {
      target = p;
      knobsPrimed = true;
    } else {
      const auto glide = [](float& v, float to) { v += kKnobSmoothing * (to - v); };
      glide(target.position, p.position);
      glide(target.size, p.size);
      glide(target.pitch, p.pitch);
      glide(target.density, p.density);
      glide(target.texture, p.texture);
      glide(target.spread, p.spread);
      glide(target.feedback, p.feedback);
      glide(target.reverb, p.reverb);
    }
    processor->set_playback_mode(static_cast<clouds::PlaybackMode>(juce::jlimit(0, 3, p.mode)));
    processor->set_quality(juce::jlimit(0, 3, p.quality));
    processor->set_freeze(p.freeze);
    auto* cp = processor->mutable_parameters();
    cp->position = juce::jlimit(0.0f, 1.0f, target.position);
    cp->size = juce::jlimit(0.0f, 1.0f, target.size);
    cp->pitch = juce::jlimit(-kPitchRange, kPitchRange, target.pitch);
    cp->density = juce::jlimit(0.0f, 1.0f, target.density);
    cp->texture = juce::jlimit(0.0f, 1.0f, target.texture);
    cp->stereo_spread = juce::jlimit(0.0f, 1.0f, target.spread);
    cp->feedback = juce::jlimit(0.0f, 1.0f, target.feedback);
    cp->reverb = juce::jlimit(0.0f, 1.0f, target.reverb);
    // Blend is mixed here at the host rate, so the engine runs fully wet.
    cp->dry_wet = 1.0f;
    cp->trigger = false;
    cp->gate = false;
  }

  void pushWet(Frame f) {
    if (fifoCount >= kFifoSize) return;  // can't happen at supported rates; drop rather than overrun
    fifo[(fifoRead + fifoCount) % kFifoSize] = f;
    ++fifoCount;
  }

  Frame popWet() {
    if (fifoCount == 0) return {};
    const Frame f = fifo[fifoRead];
    fifoRead = (fifoRead + 1) % kFifoSize;
    --fifoCount;
    return f;
  }

  void runEngine(const Params& p) {
    applyParams(p);
    processor->Prepare();
    processor->Process(engineIn, engineOut, clouds::kMaxBlockSize);
    for (int i = 0; i < kEngineBlock; ++i) {
      // The module's output stage halves the level before its soft clipper
      // (SoftConvert); undo that so Blend at full wet matches the dry level.
      const Frame wet{engineOut[i].l / 16384.0f, engineOut[i].r / 16384.0f};
      up.push(wet, [this](Frame f) { pushWet(upFilter.process(f)); });
    }
    engineFill = 0;
  }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
  juce::ignoreUnused(maxBlockSize);
  auto& m = *impl_;
  m.hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
  m.down.step = m.hostRate / kEngineRate;
  m.up.step = kEngineRate / m.hostRate;
  // Band-limit below both Nyquists: the engine's 16 kHz, and the host's.
  const double cutoff = std::min(14000.0, 0.45 * std::min(m.hostRate, kEngineRate));
  m.downFilter.set(m.hostRate, cutoff);
  m.upFilter.set(m.hostRate, cutoff);
  m.priming = static_cast<int>(std::ceil(kEngineBlock * m.hostRate / kEngineRate)) + 4;
  m.onGain.reset(m.hostRate, kFadeSeconds);
  m.blend.reset(m.hostRate, kFadeSeconds);
  m.onGain.setCurrentAndTargetValue(0.0f);
  running_ = false;
  m.restart();
}

void Engine::reset() {
  impl_->restart();
  impl_->onGain.setCurrentAndTargetValue(running_ ? 1.0f : 0.0f);
}

void Engine::process(juce::AudioBuffer<float>& buffer, const Params& params) {
  auto& m = *impl_;
  if (!params.on && !running_) return;
  if (params.on && !running_) {
    m.restart();
    m.onGain.setCurrentAndTargetValue(0.0f);
    m.blend.setCurrentAndTargetValue(params.blend);
    running_ = true;
  }
  m.onGain.setTargetValue(params.on ? 1.0f : 0.0f);
  m.blend.setTargetValue(juce::jlimit(0.0f, 1.0f, params.blend));

  const int numSamples = buffer.getNumSamples();
  const int numChannels = buffer.getNumChannels();
  if (numChannels == 0) return;
  float* l = buffer.getWritePointer(0);
  float* r = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

  for (int i = 0; i < numSamples; ++i) {
    const Frame dry{l[i], r != nullptr ? r[i] : l[i]};

    m.down.push(m.downFilter.process(dry), [&](Frame f) {
      m.engineIn[m.engineFill] = {toShort(f.l), toShort(f.r)};
      if (++m.engineFill == kEngineBlock) m.runEngine(params);
    });
    const Frame wet = m.popWet();

    // Equal-power blend (the module's own crossfade law); switching the
    // effect on or off glides the blend from / to fully dry.
    const float mix = m.blend.getNextValue() * m.onGain.getNextValue();
    const float angle = mix * juce::MathConstants<float>::halfPi;
    const float dryGain = std::cos(angle), wetGain = std::sin(angle);
    if (r != nullptr) {
      l[i] = dry.l * dryGain + wet.l * wetGain;
      r[i] = dry.r * dryGain + wet.r * wetGain;
    } else {
      l[i] = dry.l * dryGain + 0.5f * (wet.l + wet.r) * wetGain;
    }
  }

  if (!params.on && !m.onGain.isSmoothing()) running_ = false;
}

double Engine::tailSeconds(const Params& params) {
  // The recording buffer replays for up to 8 s (8-bit mono) and the
  // reverb rings past it; freeze holds forever, which hosts can't use.
  return params.on ? 10.0 : 0.0;
}

}  // namespace cloudsfx
