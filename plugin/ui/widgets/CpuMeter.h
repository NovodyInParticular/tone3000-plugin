// A compact DSP-load meter: "CPU", a short bar and the percentage, in the
// skin's colours (the accent, the warning red past kHotPercent). Repaints
// only when the meter store publishes a new reading (a few times a second).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/MeterStore.h"

namespace t3k::ui {

class CpuMeter : public juce::Component, private MeterStore::Listener {
public:
  static constexpr float kHotPercent = 80.0f;
  static constexpr int kWidth = 124, kHeight = 16;

  explicit CpuMeter(MeterStore& meters);
  ~CpuMeter() override;

  void paint(juce::Graphics& g) override;

private:
  void meterChanged(const juce::String&) override {}
  void cpuChanged() override;

  MeterStore& meters_;
  int shown_ = -1;  // the tenth of a percent last painted
};

}  // namespace t3k::ui
