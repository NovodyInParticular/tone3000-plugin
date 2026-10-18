// The Bass / Middle / Treble tone stack drawn over the rig view's output
// spectrum: the stack's combined response as a curve, with one handle per
// band to drag up or down. The handles write the same parameters as the
// faceplate knobs (as one host gesture per drag) and follow them, host
// automation and presets back, so both stay in step.
//
// The curve is computed from the stack's real voicing (Processor.cpp
// updateEqCoefficients: low shelf 150 Hz, bell 425 Hz, high shelf 1.8 kHz,
// each knob 0..10 mapping linearly to ±20 / ±15 / ±10 dB) with the EQ
// editor's biquad math, so it is the audio truth. The stack runs before the
// output tap, so the curve shapes the very trace it sits on.
//
// Laid over the spectrum's plot area. Its own vertical scale: ±kRangeDb
// around the middle line. Takes the mouse only near a handle, so the rest
// of the analyser stays click-through.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "services/ParamBinding.h"
#include "services/Services.h"
#include "widgets/HoverTip.h"

namespace t3k::ui {

class ToneEqOverlay : public juce::Component {
public:
  static constexpr float kRangeDb = 24.0f;
  static constexpr float kHandleRadius = 5.0f;

  explicit ToneEqOverlay(Services& services);
  ~ToneEqOverlay() override;

  struct Band {
    const char* name;
    const char* paramId;
    double freqHz;
    float dbPerStep;  // dB per knob step (0..10, 5 = flat)
  };
  static const std::array<Band, 3>& bands();
  // A knob position (normalised 0..1) as the band's gain in dB, and back.
  static float gainDb(int band, float normalised);
  static float normalisedFor(int band, float gainDb);

  // The stack's combined response (dB) at `hz`, for the current knobs.
  float responseAt(double hz) const;

  void paint(juce::Graphics& g) override;
  bool hitTest(int x, int y) override;
  void mouseMove(const juce::MouseEvent& e) override;
  void mouseExit(const juce::MouseEvent& e) override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
  void mouseDoubleClick(const juce::MouseEvent& e) override;

  juce::Point<float> handlePosition(int band) const;

private:
  float xOf(double hz) const;
  float yOf(float db) const;
  int handleAt(juce::Point<float> p) const;
  void showValue(int band);
  void rebuildCurve();
  std::vector<EqBand> stack() const;
  double sampleRate() const;

  Services& services_;
  std::array<std::unique_ptr<ParamBinding>, 3> params_;
  std::unique_ptr<ParamBinding> enabled_;
  juce::Path curve_;
  juce::Rectangle<int> curveFor_;  // the bounds curve_ was built for
  int hover_ = -1, drag_ = -1;
  float dragStartY_ = 0.0f, dragStartDb_ = 0.0f;
  HoverTip tip_;
};

}  // namespace t3k::ui
