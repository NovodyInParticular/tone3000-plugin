// The faceplate's time-FX group: MOD / DLY / REV power toggles over a
// "Time FX" caption, plus the rack panel (FxDeckPanel) that holds every
// control. Click the caption, or right-click anywhere on the group (touch:
// hold), to open the rack; Alt/Option-click the caption resets it. Like the
// gate and pitch groups, a view setting (Show Time FX, on by default) hides
// it, but a powered effect always shows.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FxDeckPanel.h"
#include "services/Services.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class FxGroup : public juce::Component, public SecondaryPressTarget {
public:
  static constexpr int kToggleGap = 4;
  static int width();
  static int height() { return Knob::heightFor(theme::kKnobSizeSecondary); }

  explicit FxGroup(Services& services);
  ~FxGroup() override;

  bool anyOn() const;

  void paint(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

  // Fired when a power toggle changes (the plate decides visibility).
  std::function<void()> onPowerChange;

private:
  juce::Rectangle<int> captionBounds() const;
  void toggleDeck();

  Services& services_;
  ParamTextToggle mod_, delay_, reverb_;
  FxDeckPanel deck_;
};

}  // namespace t3k::ui
