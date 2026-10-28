#include "ParamControls.h"

namespace t3k::ui {

ParamKnob::ParamKnob(Backend& backend, const juce::String& paramId, Options options)
    : Knob(std::move(options)), binding_(backend, paramId) {
  Knob::setValue(binding_.normalised());
  binding_.onChange = [this] { Knob::setValue(binding_.normalised()); };
  onDragStateChange = [this](bool dragging) {
    if (dragging)
      binding_.beginGesture();
    else
      binding_.endGesture();
  };
  onChange = [this](float v) {
    if (binding_.dragging())
      binding_.dragTo(v);
    else
      binding_.set(v);
    if (onValueChange) onValueChange(v);
  };
}

ParamPowerButton::ParamPowerButton(Backend& backend, const juce::String& paramId, help::Key help)
    : ChromeIconButton(Icon::Power, Tone::power, help), binding_(backend, paramId) {
  setOn(binding_.boolValue());
  binding_.onChange = [this] { sync(); };
  onClick = [this] {
    binding_.set(!binding_.boolValue());
    sync();  // the host write is synchronous; don't wait for the echo
  };
}

void ParamPowerButton::sync() {
  const bool on = binding_.boolValue();
  setOn(on);
  if (onValueChange) onValueChange(on);
}

ParamTextToggle::ParamTextToggle(Backend& backend, const juce::String& paramId, juce::String label,
                                 help::Key help)
    : ChromeTextButton(std::move(label), help), binding_(backend, paramId) {
  setArmed(binding_.boolValue());
  binding_.onChange = [this] { setArmed(binding_.boolValue()); };
  onClick = [this] {
    binding_.set(!binding_.boolValue());
    setArmed(binding_.boolValue());  // the host write is synchronous; don't wait for the echo
  };
}

ParamHoldToggle::ParamHoldToggle(Backend& backend, const juce::String& paramId, const juce::String& modeId,
                                 juce::String label, help::Key help)
    : ChromeTextButton(std::move(label), help), binding_(backend, paramId), mode_(backend, modeId) {
  setArmed(binding_.boolValue());
  binding_.onChange = [this] { setArmed(binding_.boolValue()); };
  onClick = [this] {
    if (momentary()) return;  // the press and release did it
    binding_.set(!binding_.boolValue());
    setArmed(binding_.boolValue());
  };
}

void ParamHoldToggle::buttonStateChanged() {
  ChromeTextButton::buttonStateChanged();
  if (!momentary()) return;
  const bool down = getState() == juce::Button::buttonDown;
  if (down != binding_.boolValue()) {
    binding_.set(down);
    setArmed(down);
  }
}

}  // namespace t3k::ui
