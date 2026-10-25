// The faceplate's Clouds group (Mutable Instruments' granular texture
// processor, CloudsFx.h): Density, Texture and Blend with a power switch,
// in the place the tone stack's Bass / Middle / Treble had (the tone stack
// itself lives on in the rig view's EQ curve). Same gestures as the gate
// and pitch groups: the knobs dim and go inert while the effect is off, the
// power stays bright; right-click anywhere on the group (Ctrl-click on
// macOS, touch-and-hold on a knob) opens the deck (CloudsDeckPanel) with the
// rest of the module's controls; Alt/Option-click on Blend resets the whole
// effect.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CloudsDeckPanel.h"
#include "services/Services.h"
#include "widgets/DimGroup.h"
#include "widgets/ParamControls.h"
#include "widgets/SecondaryPress.h"

namespace t3k::ui {

class CloudsGroup : public juce::Component, public SecondaryPressTarget {
public:
  // Between the three knobs (the old tone stack's spacing).
  static constexpr int kKnobGap = 24;
  // Three primary knobs + the plate's knob-to-companion gap + the power box.
  static constexpr int kWidth = 3 * theme::kKnobSizePrimary + 2 * kKnobGap + 10 + theme::kIconBoxSize;
  static int height() { return Knob::heightFor(theme::kKnobSizePrimary); }

  explicit CloudsGroup(Services& services);
  ~CloudsGroup() override;

  void resized() override;
  void visibilityChanged() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void secondaryPress(const juce::MouseEvent&) override { toggleDeck(); }

private:
  void toggleDeck();

  Services& services_;
  DimGroup dim_;
  ParamKnob density_, texture_, blend_;
  ParamPowerButton power_;
  CloudsDeckPanel deck_;
};

}  // namespace t3k::ui
