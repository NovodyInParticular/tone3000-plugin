// The Lite suggestion card (LiteAdvisor): floats above the faceplate's
// right end while the DSP load stays high. Suggest: "Switch N NAM blocks to
// A2-Lite?" [Switch to Lite] [Not now]. Switched: "N blocks running Lite"
// [Back to Full] [OK]. Never shown otherwise.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/LiteAdvisor.h"
#include "widgets/PillButton.h"

namespace t3k::ui {

class LiteSuggestionView : public juce::Component, private LiteAdvisor::Listener {
public:
  static constexpr int kWidth = 340;
  static constexpr int kHeight = 96;

  explicit LiteSuggestionView(LiteAdvisor& advisor);
  ~LiteSuggestionView() override;

  // Distance from the parent's bottom edge to the card's bottom.
  void setBottomOffset(int bottom);
  void paint(juce::Graphics& g) override;
  void resized() override;
  void parentSizeChanged() override { layout(); }

private:
  void liteAdvisorChanged() override;
  void layout();
  juce::String message() const;

  LiteAdvisor& advisor_;
  PillButton primary_{"Switch to Lite", PillButton::Style::filled};
  PillButton secondary_{"Not now", PillButton::Style::outline};
  int bottom_ = 0;
};

}  // namespace t3k::ui
