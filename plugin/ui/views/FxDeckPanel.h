// The time-FX rack panel (TimeFx.h on the processor side): modulation,
// delay and reverb, each with its own power toggle and knobs, plus the
// order the three run in. Opens above the faceplate's FX group.
//  - MOD: chorus (FLANGER off) or flanger; Rate, Depth, Mix.
//  - DLY: Time, or Division while SYNC follows the host tempo; PING
//    bounces the repeats L/R; Feedback, Mix, Damping.
//  - REV: Size, Decay (RT60), Damping, Mix.
//  - Order: click steps through the six orders.
// Every control is a host parameter, so it automates, maps to MIDI and
// saves with the session and presets.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "widgets/Clickable.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class FxDeckPanel : public Popover {
public:
  static constexpr int kGap = 6;

  explicit FxDeckPanel(Services& services);

  // Restores the whole rack to its defaults (all three off).
  static void resetRack(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class OrderButton : public Clickable {
  public:
    explicit OrderButton(Backend& backend);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

  private:
    ParamBinding order_;
  };

  void syncSync();

  ParamTextToggle modOn_, flanger_;
  ParamKnob modRate_, modDepth_, modMix_;
  ParamTextToggle delayOn_, sync_, pingPong_;
  ParamKnob delayTime_, delayDivision_, delayFeedback_, delayMix_, delayDamping_;
  ParamTextToggle reverbOn_;
  ParamKnob reverbSize_, reverbDecay_, reverbDamping_, reverbMix_;
  OrderButton order_;
  ParamBinding syncBinding_;
};

}  // namespace t3k::ui
