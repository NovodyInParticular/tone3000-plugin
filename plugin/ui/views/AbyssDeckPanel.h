// Abyss Pitch's panel (click its card in the rig view): every control of
// AbyssPitch.h on the processor side.
//  - ON; TIGHT / SMOOTH tracking (changes the latency); PRE-AMP / POST-AMP.
//  - The Shift layer's interval (FREE: the Shift knob), the bend range and
//    which insert runs first when Abyss and Glitch share a side.
//  - Layer levels (-2, -1, +1 octave, Shift), Shift, Detune; Bend, Glide,
//    Dry, Wet, Output.
// Every control is a host parameter, so it automates, maps to MIDI and
// saves with the session and presets (tracking, placement and order don't
// automate).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "views/GlitchDeckPanel.h"

namespace t3k::ui {

class AbyssDeckPanel : public Popover {
public:
  static constexpr int kGap = 6;

  explicit AbyssDeckPanel(Services& services);

  // Restores the whole effect to its defaults (power off).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  ParamTextToggle power_;
  ChoiceCycleButton tracking_, placement_, interval_, bendRange_, order_;
  ParamKnob subTwo_, subOne_, up_, shiftLevel_, shift_, detune_;
  ParamKnob bend_, glide_, dry_, wet_, output_;
};

}  // namespace t3k::ui
