#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

/**
 * Guided input level calibration: "play your hardest for a few seconds".
 *
 * arm() starts a listening window; the audio thread feeds the input (after
 * the Input knob, before the calibration gain) through push(), which only
 * tracks the peak and counts samples: two relaxed atomics, no locks, no
 * allocation. Once the window has elapsed poll() reports the peak, and
 * suggestedGainDb() turns it into the gain that brings that peak to the
 * level captures are trained at.
 *
 * Why a peak target: NAM captures are trained on reamp signals normalized
 * close to full scale, so a capture's drive matches the original rig when
 * the player's hardest hits land near the top of the converter. kTargetDb
 * leaves 6 dB of headroom for picking harder than during the measurement.
 *
 * The listening window only counts samples with signal (above
 * kSilenceDb), so a pause before playing doesn't eat the window; a timeout
 * (kTimeoutSeconds of wall time) ends a measurement that never hears
 * anything.
 */
class InputCalibration {
public:
  enum class State { idle, listening, measured, timedOut };

  static constexpr float kTargetDb = -6.0f;
  static constexpr float kSilenceDb = -60.0f;
  static constexpr float kMaxGainDb = 24.0f;
  static constexpr double kListenSeconds = 4.0;
  static constexpr double kTimeoutSeconds = 20.0;

  // Message thread.
  void arm(double sampleRate) {
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    needed_ = static_cast<long long>(kListenSeconds * rate);
    timeout_ = static_cast<long long>(kTimeoutSeconds * rate);
    peak_.store(0.0f, std::memory_order_relaxed);
    heard_.store(0, std::memory_order_relaxed);
    elapsed_.store(0, std::memory_order_relaxed);
    state_.store(static_cast<int>(State::listening), std::memory_order_release);
  }
  void cancel() { state_.store(static_cast<int>(State::idle), std::memory_order_release); }
  // Back to idle after the caller consumed a measured / timed-out result.
  void acknowledge() { cancel(); }

  State state() const { return static_cast<State>(state_.load(std::memory_order_acquire)); }
  double progress() const {
    return needed_ > 0 ? std::clamp(static_cast<double>(heard_.load(std::memory_order_relaxed)) /
                                        static_cast<double>(needed_), 0.0, 1.0)
                       : 0.0;
  }
  // Peak heard so far, dBFS (-inf as kSilenceDb).
  float peakDb() const {
    const float p = peak_.load(std::memory_order_relaxed);
    return p > 0.0f ? std::max(kSilenceDb, 20.0f * std::log10(p)) : kSilenceDb;
  }

  // Gain that lands `peakDb` on the target, clamped to the parameter range.
  static float suggestedGainDb(float peakDb) {
    if (!std::isfinite(peakDb)) return 0.0f;
    return std::clamp(kTargetDb - peakDb, -kMaxGainDb, kMaxGainDb);
  }

  // Audio thread. `right` may be null (mono).
  void push(const float* left, const float* right, int numSamples) {
    if (state_.load(std::memory_order_acquire) != static_cast<int>(State::listening) || numSamples <= 0)
      return;
    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
      blockPeak = std::max(blockPeak, std::abs(left[i]));
      if (right != nullptr) blockPeak = std::max(blockPeak, std::abs(right[i]));
    }
    if (!std::isfinite(blockPeak)) blockPeak = 0.0f;
    static const float silence = std::pow(10.0f, kSilenceDb / 20.0f);
    if (blockPeak > silence) {
      peak_.store(std::max(peak_.load(std::memory_order_relaxed), blockPeak), std::memory_order_relaxed);
      heard_.store(heard_.load(std::memory_order_relaxed) + numSamples, std::memory_order_relaxed);
    }
    const long long elapsed = elapsed_.load(std::memory_order_relaxed) + numSamples;
    elapsed_.store(elapsed, std::memory_order_relaxed);
    if (heard_.load(std::memory_order_relaxed) >= needed_)
      state_.store(static_cast<int>(State::measured), std::memory_order_release);
    else if (elapsed >= timeout_)
      state_.store(static_cast<int>(State::timedOut), std::memory_order_release);
  }

private:
  std::atomic<int> state_{static_cast<int>(State::idle)};
  std::atomic<float> peak_{0.0f};
  std::atomic<long long> heard_{0}, elapsed_{0};
  long long needed_ = 0, timeout_ = 0;
};
