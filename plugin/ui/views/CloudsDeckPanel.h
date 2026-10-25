// The Clouds group's advanced panel (right-click the group; touch-and-hold
// a knob): the rest of the module's controls (CloudsFx.h on the processor
// side).
//  - FREEZE: stop recording and keep playing what's in the buffer.
//  - Mode: Granular / Stretch / Looping Delay / Spectral; click steps.
//  - Quality: 16-bit or 8-bit, stereo or mono; lower quality buys a longer
//    buffer (1 to 8 s). Changing it clears the buffer.
//  - Position, Size, Pitch, Spread, Feedback, Reverb: the module's knobs
//    (Position and Size mean different things per mode).
// Every control is a host parameter, so it automates, maps to MIDI and
// saves with the session and presets (Quality doesn't automate).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "services/Services.h"
#include "widgets/Clickable.h"
#include "widgets/ParamControls.h"
#include "widgets/Popover.h"

namespace t3k::ui {

class CloudsDeckPanel : public Popover {
public:
  static constexpr int kGap = 6;

  explicit CloudsDeckPanel(Services& services);

  // Restores the whole effect to its defaults (power and freeze off).
  static void resetDeck(Backend& backend);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  // Steps a choice parameter through its names on click.
  class CycleButton : public Clickable {
  public:
    CycleButton(Backend& backend, const juce::String& paramId, const std::array<const char*, 4>& names, int width,
                help::Key help);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

  private:
    int index() const;
    ParamBinding choice_;
    const std::array<const char*, 4>& names_;
  };

  ParamTextToggle freeze_;
  CycleButton mode_, quality_;
  ParamKnob position_, size_, pitch_, spread_, feedback_, reverb_;
};

}  // namespace t3k::ui
