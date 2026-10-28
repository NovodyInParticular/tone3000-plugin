// The Glitch Engine's panel (click its card in the rig view): every control
// of GlitchFx.h on the processor side.
//  - ON, STUTTER, FREEZE; under them, whether STUTTER and FREEZE latch or
//    hold (STUT LATCH / HOLD, FRZ LATCH / HOLD).
//  - PRE-AMP / POST-AMP: where the insert runs (also by dragging the card).
//  - Rate, Length, Chance, Intensity, Seed, Fade; Bits, Rate Hz, Mix,
//    Output, and the tempo used without a host.
// Every control is a host parameter, so it automates, maps to MIDI and
// saves with the session and presets (modes, placement and tempo don't
// automate).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "services/Services.h"
#include "widgets/Clickable.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

// Steps a choice parameter through its names on click (shared by the
// insert decks).
class ChoiceCycleButton : public Clickable {
public:
  ChoiceCycleButton(Backend& backend, const juce::String& paramId, std::vector<juce::String> names, int width,
                    help::Key help);
  void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
  int index() const;
  ParamBinding choice_;
  std::vector<juce::String> names_;
};

class GlitchDeckPanel : public Popover {
public:
  static constexpr int kGap = 6;

  explicit GlitchDeckPanel(Services& services);

  // Restores the whole effect to its defaults (power, stutter, freeze off).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  ParamTextToggle power_;
  ParamHoldToggle stutter_, freeze_;
  ChoiceCycleButton stutterMode_, freezeMode_, placement_;
  ParamKnob rate_, length_, chance_, intensity_, seed_, fade_;
  ParamKnob bits_, sampleRate_, mix_, output_, tempo_;
};

}  // namespace t3k::ui
