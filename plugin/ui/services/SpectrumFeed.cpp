#include "SpectrumFeed.h"

#include <cmath>

namespace t3k::ui {

SpectrumFeed::SpectrumFeed(Backend& backend, UiClock& clock, std::string blockId, int minIntervalMs)
    : backend_(backend), clock_(clock), blockId_(std::move(blockId)), minIntervalMs_(minIntervalMs) {
  backend_.setBlockSpectrumEnabled(blockId_, true);
  tick();
  clock_.addListener(this);
}

SpectrumFeed::~SpectrumFeed() {
  clock_.removeListener(this);
  backend_.setBlockSpectrumEnabled(blockId_, false);
}

void SpectrumFeed::tick() {
  if (minIntervalMs_ > 0) {
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPollMs_ < static_cast<juce::uint32>(minIntervalMs_)) return;
    lastPollMs_ = now;
  }
  const auto res = backend_.getBlockSpectrum(blockId_);
  const auto* arr = res.getArray();
  if (arr == nullptr) return;
  scratch_.clear();
  for (const auto& v : *arr) {
    // Quantise so analyser jitter below what the graph can show (about
    // 0.3 dB per point, 0.15 dB per pixel at 2x) does not trigger a repaint.
    const auto db = static_cast<float>(static_cast<double>(v));
    scratch_.push_back(std::round(db / kQuantumDb) * kQuantumDb);
  }
  if (scratch_ == bins_) return;
  std::swap(bins_, scratch_);
  if (onChange) onChange();
}

}  // namespace t3k::ui
