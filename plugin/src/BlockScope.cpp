#include "BlockScope.h"

#include <algorithm>
#include <cmath>

BlockScope::BlockScope() { ring.resize(kRingSize, 0.0f); }

void BlockScope::prepare(double newSampleRate) {
  sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
  std::fill(ring.begin(), ring.end(), 0.0f);
}

void BlockScope::pushSamples(const float* ch0, const float* ch1OrNull, int numSamples) {
  int pos = writePos.load(std::memory_order_relaxed);
  for (int i = 0; i < numSamples; ++i) {
    ring[static_cast<size_t>(pos)] = ch1OrNull != nullptr ? 0.5f * (ch0[i] + ch1OrNull[i]) : ch0[i];
    pos = (pos + 1) & (kRingSize - 1);
  }
  writePos.store(pos, std::memory_order_release);
}

void BlockScope::readWaveform(std::vector<float>& out) const {
  out.assign(kPoints, 0.0f);
  const int end = writePos.load(std::memory_order_acquire);
  const int window = juce::jlimit(kPoints, kRingSize / 4, static_cast<int>(sampleRate * kWindowMs / 1000.0));
  const auto at = [&](int back) {  // the sample `back` samples before the newest
    return ring[static_cast<size_t>((end - 1 - back) & (kRingSize - 1))];
  };
  // Trigger: the newest rising zero crossing that still leaves a full window
  // after it, searched over up to one more window back. None (noise, DC,
  // silence): the latest window as it is.
  int start = window;  // samples back from the newest where the window begins
  for (int back = window; back < 2 * window; ++back)
    if (at(back) < 0.0f && at(back - 1) >= 0.0f) {
      start = back - 1;
      break;
    }
  for (int p = 0; p < kPoints; ++p) {
    // The sample of largest magnitude in this point's slice keeps peaks
    // visible after decimation.
    const int from = start - p * window / kPoints;
    const int to = start - (p + 1) * window / kPoints;
    float pick = 0.0f;
    for (int back = from; back > to && back >= 0; --back)
      if (std::abs(at(back)) > std::abs(pick)) pick = at(back);
    out[static_cast<size_t>(p)] = juce::jlimit(-1.0f, 1.0f, pick);
  }
}

juce::var BlockScope::getWaveform() const {
  std::vector<float> points;
  readWaveform(points);
  juce::Array<juce::var> out;
  out.ensureStorageAllocated(kPoints);
  for (const float v : points) out.add(static_cast<double>(v));
  return juce::var(out);
}
