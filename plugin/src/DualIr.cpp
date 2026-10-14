#include "DualIr.h"

#include <algorithm>
#include <cmath>

namespace dualir {

namespace {

constexpr int kSincHalfTaps = 16;  // fractional-delay kernel: 32 taps

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

// Scale every channel so the IR (summed over its channels) has unit energy.
void normalizeEnergy(juce::AudioBuffer<float>& ir) {
  double energy = 0.0;
  for (int ch = 0; ch < ir.getNumChannels(); ++ch) {
    const float* d = ir.getReadPointer(ch);
    for (int i = 0; i < ir.getNumSamples(); ++i)
      energy += static_cast<double>(d[i]) * d[i];
  }
  energy /= juce::jmax(1, ir.getNumChannels());
  if (energy > 1e-20)
    ir.applyGain(static_cast<float>(1.0 / std::sqrt(energy)));
}

// out[n + delay] += gain * in[n], with a Blackman-windowed sinc for the
// fractional part. `out` must be long enough (the caller sizes it).
void addDelayed(const float* in, int length, double delay, float gain, float* out, int outLength) {
  const int whole = static_cast<int>(std::floor(delay));
  const double frac = delay - whole;
  if (frac < 1e-6) {
    for (int n = 0; n < length; ++n)
      if (n + whole >= 0 && n + whole < outLength)
        out[n + whole] += gain * in[n];
    return;
  }
  // Kernel h[k], k = -H+1 .. H, centred on the fractional offset.
  float h[2 * kSincHalfTaps];
  double sum = 0.0;
  for (int k = -kSincHalfTaps + 1; k <= kSincHalfTaps; ++k) {
    const double x = k - frac;
    const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) /
                                                         (juce::MathConstants<double>::pi * x);
    const double w = (x + kSincHalfTaps) / (2.0 * kSincHalfTaps);  // 0..1 across the window
    const double blackman = 0.42 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * w) +
                            0.08 * std::cos(4.0 * juce::MathConstants<double>::pi * w);
    h[k + kSincHalfTaps - 1] = static_cast<float>(sinc * blackman);
    sum += sinc * blackman;
  }
  for (auto& v : h)
    v = static_cast<float>(v / sum);  // unity DC gain
  for (int n = 0; n < length; ++n) {
    const float x = gain * in[n];
    for (int k = -kSincHalfTaps + 1; k <= kSincHalfTaps; ++k) {
      const int at = n + whole + k;
      if (at >= 0 && at < outLength)
        out[at] += x * h[k + kSincHalfTaps - 1];
    }
  }
}

}  // namespace

Params Params::sanitized() const {
  Params p;
  p.blend = juce::jlimit(0.0f, 1.0f, finiteOr(blend, 0.5f));
  p.levelADb = juce::jlimit(-kMaxLevelDb, kMaxLevelDb, finiteOr(levelADb, 0.0f));
  p.levelBDb = juce::jlimit(-kMaxLevelDb, kMaxLevelDb, finiteOr(levelBDb, 0.0f));
  p.invertA = invertA;
  p.invertB = invertB;
  p.alignMs = juce::jlimit(-kMaxAlignMs, kMaxAlignMs, finiteOr(alignMs, 0.0f));
  return p;
}

bool decode(const std::vector<uint8_t>& bytes, double sampleRate, double maxSeconds, juce::AudioBuffer<float>& out) {
  if (bytes.empty())
    return false;
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(
      std::make_unique<juce::MemoryInputStream>(bytes.data(), bytes.size(), false)));
  if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    return false;

  const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
  const auto fileLength = static_cast<int>(
      std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(maxSeconds * reader->sampleRate)));
  juce::AudioBuffer<float> raw(channels, fileLength);
  if (!reader->read(&raw, 0, fileLength, 0, true, channels > 1))
    return false;

  if (std::abs(reader->sampleRate - sampleRate) < 0.5) {
    out = std::move(raw);
    return true;
  }
  const double ratio = reader->sampleRate / sampleRate;  // input samples per output sample
  const int outLength = static_cast<int>(std::ceil(fileLength / ratio));
  out.setSize(channels, outLength);
  out.clear();
  for (int ch = 0; ch < channels; ++ch) {
    juce::Interpolators::WindowedSinc interpolator;
    // Zero-padded input so the tail of the kernel is read out fully.
    std::vector<float> padded(static_cast<size_t>(fileLength) + 256, 0.0f);
    std::copy(raw.getReadPointer(ch), raw.getReadPointer(ch) + fileLength, padded.begin());
    interpolator.process(ratio, padded.data(), out.getWritePointer(ch), outLength);
  }
  return true;
}

std::vector<uint8_t> combine(const std::vector<uint8_t>& irA, const std::vector<uint8_t>& irB, const Params& raw,
                             double sampleRate, double maxSeconds) {
  const Params p = raw.sanitized();
  juce::AudioBuffer<float> a, b;
  if (!decode(irA, sampleRate, maxSeconds, a) || !decode(irB, sampleRate, maxSeconds, b))
    return {};
  normalizeEnergy(a);
  normalizeEnergy(b);

  const double delay = p.alignMs * 0.001 * sampleRate;  // samples; > 0 delays B
  const double delayA = delay < 0.0 ? -delay : 0.0;
  const double delayB = delay > 0.0 ? delay : 0.0;
  const int pad = static_cast<int>(std::ceil(std::abs(delay))) + kSincHalfTaps + 1;
  const int length = std::max(a.getNumSamples(), b.getNumSamples()) + pad;
  const int channels = std::max(a.getNumChannels(), b.getNumChannels());

  const float gainA = (1.0f - p.blend) * juce::Decibels::decibelsToGain(p.levelADb) * (p.invertA ? -1.0f : 1.0f);
  const float gainB = p.blend * juce::Decibels::decibelsToGain(p.levelBDb) * (p.invertB ? -1.0f : 1.0f);

  juce::AudioBuffer<float> mixed(channels, length);
  mixed.clear();
  for (int ch = 0; ch < channels; ++ch) {
    // A mono IR feeds both channels of a stereo pair.
    const float* srcA = a.getReadPointer(std::min(ch, a.getNumChannels() - 1));
    const float* srcB = b.getReadPointer(std::min(ch, b.getNumChannels() - 1));
    addDelayed(srcA, a.getNumSamples(), delayA, gainA, mixed.getWritePointer(ch), length);
    addDelayed(srcB, b.getNumSamples(), delayB, gainB, mixed.getWritePointer(ch), length);
  }

  juce::MemoryBlock block;
  {
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(block, false);
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(channels)
                             .withBitsPerSample(32)
                             .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);  // takes the stream on success
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(mixed, 0, length))
      return {};
  }
  const auto* bytes = static_cast<const uint8_t*>(block.getData());
  return std::vector<uint8_t>(bytes, bytes + block.getSize());
}

}  // namespace dualir
