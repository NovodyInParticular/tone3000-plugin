#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

/**
 * Per-block oscilloscope tap for the rig view's amp waveform.
 *
 * Same threading pattern as BlockSpectrum: the audio thread copies the
 * block's output (mono sum) into a preallocated lock-free ring, but only
 * while `enabled` is set (the rig view is showing the block); all reading,
 * triggering and decimation happen on the message thread in getWaveform().
 * A torn read of a sample or two while the ring advances only shows as a
 * one-frame blip, which is fine for a picture.
 *
 * getWaveform() returns kPoints floats in -1..1: the most recent kWindowMs
 * of the signal, started at a rising zero crossing when there is one, so a
 * held note stands still instead of scrolling. Silence returns zeros.
 */
class BlockScope {
public:
  static constexpr int kPoints = 96;
  static constexpr double kWindowMs = 20.0;

  BlockScope();

  void prepare(double sampleRate);

  void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }
  bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }

  /** Real-time thread; no allocation. Mono-sums up to two channels. */
  void pushSamples(const float* ch0, const float* ch1OrNull, int numSamples);

  /** Message thread. A var array of kPoints values in -1..1. */
  juce::var getWaveform() const;

  /** Message thread: the same, into `out` (resized to kPoints). */
  void readWaveform(std::vector<float>& out) const;

private:
  static constexpr int kRingSize = 1 << 13;  // 8192 samples: 170 ms at 48 kHz

  std::vector<float> ring;
  std::atomic<int> writePos{0};
  std::atomic<bool> enabled{false};
  double sampleRate = 48000.0;
};
