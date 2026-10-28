// The rig view's insert-effect cards (Glitch Engine, Abyss Pitch): what
// they draw, separate from RigView's input handling so the look stays in
// one place. Everything is in the card's nominal units (RigView scales the
// whole module), and draws from a snapshot of the effect's state, so
// painting never touches the backend.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui::rigface {

inline constexpr float kInsertWidth = 124.0f;

struct GlitchLook {
  bool on = false;
  bool stutter = false, freeze = false;            // the parameters
  bool stutterHold = false, freezeHold = false;    // Momentary modes
  bool pre = false;                                // placed before the amps
  float crush = 0.0f;   // 0 clean .. 1 heavily bit / rate reduced (activity)
  float bits = 16.0f;   // the Bits setting
  float freezeLevel = 0.0f;  // the freeze fade, 0..1 (activity)
  bool repeating = false;    // a stutter or random glitch is playing
  int event = 0;             // last glitch kind (glitchfx::Event)
  float eventAge = 1.0f;     // seconds since it fired (1+: long ago)
  bool split = false;        // Ether: the RGB split / pixel melt is showing (gated, < 3 a second)
  float time = 0.0f;         // animation clock, s (0 on static skins)
};

struct AbyssLook {
  bool on = false;
  bool pre = true;
  bool smooth = false;  // Smooth tracking
  // Layer settings (levels 0..1): -2, -1, +1 octave, shift.
  float subTwo = 0, subOne = 0, up = 0, shift = 0;
  float shiftSemitones = 0;  // the shift layer's interval
  float detuneCents = 0;
  float bend = 0;        // glided bend, semitones (activity)
  float activity[5] = {};  // each voice's output peak (activity)
  float input = 0;         // input peak (activity)
  float time = 0;          // animation clock, s (0 on static skins)
};

// The card's pieces, in nominal units inside `box` (the card minus its
// 1-unit margin): STUTTER / FREEZE pills (0 / 1) and the footswitch.
juce::Rectangle<float> glitchPill(juce::Rectangle<float> box, int which);
juce::Rectangle<float> insertFootswitch(juce::Rectangle<float> box);
// The screen that animates (the only part repainted per frame).
juce::Rectangle<float> glitchScreen(juce::Rectangle<float> box);

void drawGlitch(juce::Graphics& g, juce::Rectangle<float> box, const GlitchLook& look);

// Abyss shares the glitch card's screen area.
inline juce::Rectangle<float> abyssScreen(juce::Rectangle<float> box) { return glitchScreen(box).withHeight(74); }
void drawAbyss(juce::Graphics& g, juce::Rectangle<float> box, const AbyssLook& look);

}  // namespace t3k::ui::rigface
