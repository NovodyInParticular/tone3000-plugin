// The Ether skin's art: a 60s/70s liquid light show drawn with gradients
// and paths (no blurs, no per-pixel work), shared by every view that styles
// itself for it. A skin turns it on with "style": { "psychedelic": true }.
//
// Motion: everything that moves reads clock(), which only advances while
// motion() is true: the skin is psychedelic, animations are allowed (the
// skin, the "Reduce animations" setting and the system's reduced-motion
// preference), Trip Intensity is above 0, and the frame-time watchdog has
// not fallen back to the static look. With motion off every picture is
// drawn at clock 0 with its resting colours: a complete static version.
//
// Safety: nothing flashes. Colour changes are slow drifts (one hue cycle a
// minute); event effects (a glitch firing, a tone landing) go through
// FlashGate, at most 3 starts a second, and never swing the whole card's
// brightness.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace t3k::ui::ether {

// Whether the applied skin is psychedelic.
bool active();
// Trip Intensity 0..1 (UiPrefs::kTripIntensity; 0 = fully static).
float trip();
void setTrip(float value);
// The OS asks for reduced motion (macOS / Windows / GNOME settings), read
// once per editor.
bool systemReducedMotion();
// Whether pictures move right now (see the file comment).
bool motion();
// Animation time in seconds, scaled by Trip Intensity; 0 while static.
double clock();
// Called by the views that own the frame tick (30 Hz).
void advance(double seconds);

// The music's energy, 0..1 (the output level, smoothed): glow, saturation
// and drift speed rise with it, scaled by trip().
void setEnergy(float level01);
float energy();

// Frame-time watchdog: report how long a frame's painting took; if it stays
// over budget for ~2 s the skin falls back to the static look until the
// skin is applied again (or the editor reopens).
void reportFrameMs(double ms);
bool degraded();
void resetWatchdog();

// The palette: cosmic indigo base, accents drifting through magenta,
// electric violet, hot pink, tangerine, acid green and turquoise. `t` picks
// a spot on the wheel (0..1, wraps); the whole wheel turns once a minute.
juce::Colour palette(float t, float alpha = 1.0f);
juce::Colour base();  // the deep indigo behind everything

// At most kMaxPerSecond starts per second (photosensitivity: < 3 Hz).
class FlashGate {
public:
  static constexpr int kMinGapMs = 340;
  bool allow();

private:
  juce::uint32 last_ = 0;
};

// Pictures. All take nominal-unit rectangles in the caller's transform.
// Text backing: a softly darkened, frosted panel.
void frosted(juce::Graphics& g, juce::Rectangle<float> r, float corner, float strength = 1.0f);
// A rounded, liquid card body with a swirling rainbow border.
void liquidCard(juce::Graphics& g, juce::Rectangle<float> r, float corner, bool lit, float seed);
// A footswitch that glows like a lava-lamp bubble while on.
void lavaSwitch(juce::Graphics& g, juce::Rectangle<float> r, bool on, float seed);
// A slowly turning mandala / sunburst behind an amp's name.
void mandala(juce::Graphics& g, juce::Rectangle<float> r, float level);
// Strokes a path as a rainbow ribbon (hue along its length).
void rainbowStroke(juce::Graphics& g, const juce::Path& path, float width, float phase, float alpha = 1.0f);
// A cab grille as a turning kaleidoscope; `lit` brightens it.
void kaleidoscope(juce::Graphics& g, juce::Rectangle<float> r, float lit, float seed);
// Two kaleidoscopes melting into each other at `blend` (0 = all A).
void dualKaleidoscope(juce::Graphics& g, juce::Rectangle<float> r, float blend);
// Time FX rows.
void ripples(juce::Graphics& g, juce::Rectangle<float> r, bool on, float feedback);
void nebula(juce::Graphics& g, juce::Rectangle<float> r, bool on, float decay);
void spiralWave(juce::Graphics& g, juce::Rectangle<float> r, bool on, float rate, float depth);
// Abyss Pitch: a vortex. Octaves down pull colour inward, up blooms
// outward, detune splits it into two shimmering spirals.
struct Vortex {
  float down = 0, up = 0, shift = 0;  // layer levels 0..1 (settings + live)
  float detune = 0;                    // 0..1
  float bend = 0;                      // semitones
  bool on = false;
};
void vortex(juce::Graphics& g, juce::Rectangle<float> r, const Vortex& v);
// Glitch Engine: crystal facets over a frozen screen (amount 0..1).
void crystal(juce::Graphics& g, juce::Rectangle<float> r, float amount);
// A knob as concentric rainbow rings; the colour sweep follows the value.
void rainbowKnob(juce::Graphics& g, juce::Rectangle<float> box, float angleDeg, float arcFromDeg);
// A soft pill with an inner glow that blooms when active.
void pill(juce::Graphics& g, juce::Rectangle<float> r, bool active, bool hovered);
// A cable as a flowing rainbow ribbon, with a light at `phase` (or none).
void cable(juce::Graphics& g, juce::Line<float> line, bool live, float phase, float level);
// A short ripple across a card when a tone lands (age 0..1 of the effect).
void landingRipple(juce::Graphics& g, juce::Rectangle<float> r, float age);
// The analyser's fill: a rainbow liquid wave with a mirrored reflection.
void spectrumFill(juce::Graphics& g, const juce::Path& fill, const juce::Path& line, juce::Rectangle<float> plot);

// The animated backdrop: drifting oil blobs over a starfield, rendered at
// low resolution into `cache` and drawn scaled over `area`.
void backdrop(juce::Graphics& g, juce::Rectangle<int> area, juce::Image& cache);

}  // namespace t3k::ui::ether
