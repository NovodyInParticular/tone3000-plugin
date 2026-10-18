// Live analyser of the plugin's output (feature 13), drawn in the skin's
// colours: an accent trace with a soft fill, plus a halo on glowing skins.
// The FFT happens in native on the message thread (BlockSpectrum); this
// view only plots the 64 log-spaced bins it is handed, so its cost is one
// path per refresh. `rate` "low" refreshes ~8 times a second, "off" shows
// nothing and turns the native tap off.
//
// The input trace: a faint second line for the dry input (native's
// "input" tap, the signal as the rig receives it), so the difference
// between the two is what the rig does to the tone. Switched by the
// "DRY IN" chip in the panel's corner; its tap runs only while shown.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "services/SpectrumFeed.h"

namespace t3k::ui {

class OutputSpectrum : public juce::Component {
public:
  OutputSpectrum(Backend& backend, UiClock& clock);
  ~OutputSpectrum() override;

  void setRate(const juce::String& rate);
  void setInputTrace(bool on);
  bool inputTrace() const { return inputTrace_; }
  // The chip was clicked: the owner stores the choice and calls setInputTrace.
  std::function<void(bool)> onInputTraceChange;

  void paint(juce::Graphics& g) override;
  void resized() override;

  static constexpr const char* kOutputId = "output";
  static constexpr const char* kInputId = "input";

  // Where the trace is plotted, and the axes (20 Hz..20 kHz log across,
  // -90..0 dB up), in this component's coordinates.
  juce::Rectangle<float> plotArea() const;
  float xOf(double hz) const;
  float yOf(float db) const;

  class Chip;

private:
  void paintBackdrop(juce::Graphics& g) const;
  void syncFeeds();
  juce::Path tracePath(const std::vector<float>& bins) const;

  Backend& backend_;
  UiClock& clock_;
  std::unique_ptr<SpectrumFeed> feed_, inputFeed_;
  juce::String rate_;
  bool inputTrace_ = false;
  std::unique_ptr<Chip> chip_;
};

}  // namespace t3k::ui
