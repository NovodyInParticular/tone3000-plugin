#include "CpuMeter.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

CpuMeter::CpuMeter(MeterStore& meters) : meters_(meters) {
  setInterceptsMouseClicks(true, false);
  setHelpText("CPU: how much of the audio time budget the plugin uses.");
  setSize(kWidth, kHeight);
  meters_.addListener(this);
}

CpuMeter::~CpuMeter() { meters_.removeListener(this); }

void CpuMeter::cpuChanged() {
  const int tenth = juce::roundToInt(meters_.cpuPercent() * 10.0f);
  if (tenth == shown_) return;
  shown_ = tenth;
  repaint();
}

void CpuMeter::paint(juce::Graphics& g) {
  const float pct = juce::jlimit(0.0f, 100.0f, meters_.cpuPercent());
  const auto font = theme::kStyle.monoLabels ? Fonts::mono(10) : Fonts::sans(11);
  auto row = getLocalBounds().toFloat();
  g.setFont(font);
  g.setColour(theme::kSubtle);
  g.drawText("CPU", row.removeFromLeft(28.0f), juce::Justification::centredLeft, false);
  const auto value = row.removeFromRight(42.0f);
  const auto bar = row.reduced(2.0f, 0.0f).withSizeKeepingCentre(row.getWidth() - 4.0f, 4.0f);
  g.setColour(theme::kSegmentedTrack);
  g.fillRoundedRectangle(bar, 2.0f);
  const auto colour = pct >= kHotPercent ? theme::kBrandRed : theme::kBrandYellow;
  g.setColour(colour);
  g.fillRoundedRectangle(bar.withWidth(juce::jmax(bar.getHeight(), bar.getWidth() * pct / 100.0f)), 2.0f);
  g.setColour(pct >= kHotPercent ? theme::kBrandRed : theme::kMuted);
  g.drawText(juce::String(pct, 1) + "%", value, juce::Justification::centredRight, false);
}

}  // namespace t3k::ui
