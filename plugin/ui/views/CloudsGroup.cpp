#include "CloudsGroup.h"

#include "core/KnobScale.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

// The plate's knob-to-companion gap (Faceplate's kGroupGap).
constexpr int kGap = 10;
// Chrome boxes in a bottom-aligned row sit on the secondary-knob centreline.
constexpr int kChromeLift = theme::faceplateChromeLift(theme::kKnobSizeSecondary);

Knob::Options plateKnob(const char* label, float def, help::Key help) {
  Knob::Options o;
  o.label = label;
  o.scale = &scales::percent();
  o.defaultValue = def;
  o.help = help;
  return o;
}

}  // namespace

CloudsGroup::CloudsGroup(Services& services)
    : services_(services),
      density_(services.backend, "cloudsDensity", plateKnob("Density", 0.65f, help::Key::cloudsDensity)),
      texture_(services.backend, "cloudsTexture", plateKnob("Texture", 0.5f, help::Key::cloudsTexture)),
      blend_(services.backend, "cloudsBlend", plateKnob("Blend", 0.5f, help::Key::cloudsBlend)),
      power_(services.backend, "cloudsEnabled", help::Key::cloudsPower),
      deck_(services) {
  for (auto* k : {&density_, &texture_, &blend_}) {
    dim_.addAndMakeVisible(*k);
    // Touch-and-hold on a knob is the right-click of the platform.
    k->onLongPress = [this] { toggleDeck(); };
  }
  dim_.setOff(!power_.value(), false);
  power_.onValueChange = [this](bool on) { dim_.setOff(!on); };
  addAndMakeVisible(dim_);
  addAndMakeVisible(power_);

  // One gesture resets the whole effect, deck included.
  blend_.onReset = [this] { CloudsDeckPanel::resetDeck(services_.backend); };

  setSize(kWidth, height());
}

CloudsGroup::~CloudsGroup() { deck_.close(); }

void CloudsGroup::visibilityChanged() {
  if (!isVisible()) deck_.close();
}

void CloudsGroup::mouseDown(const juce::MouseEvent& e) {
  if (isSecondaryPress(e)) toggleDeck();
}

void CloudsGroup::toggleDeck() {
  if (deck_.isOpen()) {
    deck_.close();
    return;
  }
  // Anchored to the first knob: the panel's left edge tracks the group's.
  deck_.open(density_, Popover::Align::left, CloudsDeckPanel::kGap, 0, Popover::Placement::above);
}

void CloudsGroup::resized() {
  const int primary = theme::kKnobSizePrimary;
  const int baseline = getHeight() - Knob::kEditorOverflow;  // the label slot's bottom edge
  dim_.setBounds(0, 0, 3 * primary + 2 * kKnobGap, getHeight());
  density_.setTopLeftPosition(0, 0);
  texture_.setTopLeftPosition(primary + kKnobGap, 0);
  blend_.setTopLeftPosition(2 * (primary + kKnobGap), 0);
  power_.setTopLeftPosition(3 * primary + 2 * kKnobGap + kGap, baseline - theme::kIconBoxSize + kChromeLift);
}

}  // namespace t3k::ui
