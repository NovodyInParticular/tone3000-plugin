#include "FxGroup.h"

#include "core/Fonts.h"
#include "core/Help.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kLabelSize = 14;  // the knob labels'
}  // namespace

int FxGroup::width() {
  // Three toggles at their natural (text) widths.
  const auto w = [](const char* text) {
    return juce::roundToInt(Fonts::width(Fonts::mono(12), text) + 2 * (4 + 1));
  };
  return w("MOD") + w("DLY") + w("REV") + 2 * kToggleGap;
}

FxGroup::FxGroup(Services& services)
    : services_(services),
      mod_(services.backend, "fxModEnabled", "MOD", help::Key::fxModPower),
      delay_(services.backend, "fxDelayEnabled", "DLY", help::Key::fxDelayPower),
      reverb_(services.backend, "fxReverbEnabled", "REV", help::Key::fxReverbPower),
      deck_(services) {
  for (auto* t : {&mod_, &delay_, &reverb_}) {
    addAndMakeVisible(*t);
    t->binding().onChange = [this, t] {
      t->setArmed(t->binding().boolValue());
      if (onPowerChange) onPowerChange();
    };
    t->onClick = [this, t] {
      t->binding().set(!t->binding().boolValue());
      t->setArmed(t->binding().boolValue());
      if (onPowerChange) onPowerChange();
    };
  }
  setHelpText(help::text(help::Key::fxGroup));
  setSize(width(), height());
}

FxGroup::~FxGroup() { deck_.close(); }

bool FxGroup::anyOn() const { return mod_.value() || delay_.value() || reverb_.value(); }

juce::Rectangle<int> FxGroup::captionBounds() const {
  // The knob label slot: same baseline as every label on the plate.
  return {0, getHeight() - Knob::kEditorOverflow - Knob::kLabelSlot, getWidth(), Knob::kLabelSlot};
}

void FxGroup::paint(juce::Graphics& g) {
  g.setFont(theme::kStyle.monoLabels ? Fonts::mono(kLabelSize - 2) : Fonts::sans(kLabelSize));
  g.setColour(deck_.isOpen() ? theme::kWhite : theme::kGray);
  g.drawText("Time FX", captionBounds(), juce::Justification::centred, false);
}

void FxGroup::visibilityChanged() {
  if (!isVisible()) deck_.close();
}

void FxGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void FxGroup::mouseUp(const juce::MouseEvent& e) {
  if (isSecondaryPress(e) || !captionBounds().contains(e.getPosition())) return;
  if (e.mods.isAltDown()) {
    FxDeckPanel::resetRack(services_.backend);
    return;
  }
  toggleDeck();
}

void FxGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
  } else {
    deck_.onDismiss = [this] { repaint(); };
    // The rack is wider than the space right of the group: shift it left so
    // it ends inside the window (16 px margin), never past its left edge.
    int inset = 0;
    if (auto* top = getTopLevelComponent()) {
      const int left = top->getLocalPoint(this, juce::Point<int>{}).x;
      const int overflow = left + deck_.getWidth() - (top->getWidth() - 16);
      if (overflow > 0) inset = -std::min(overflow, left - 16);
    }
    deck_.open(*this, Popover::Align::left, FxDeckPanel::kGap, inset, Popover::Placement::above);
  }
  repaint();
}

void FxGroup::resized() {
  const int baseline = getHeight() - Knob::kEditorOverflow - Knob::kLabelSlot - theme::kKnobLabelGap;
  // Toggles centred on the secondary-knob face centre, like the plate's chrome.
  const int y = baseline - theme::kKnobSizeSecondary / 2 - theme::kTextBoxHeight / 2;
  int x = 0;
  for (auto* t : {&mod_, &delay_, &reverb_}) {
    t->setTopLeftPosition(x, y);
    x += t->getWidth() + kToggleGap;
  }
}

}  // namespace t3k::ui
