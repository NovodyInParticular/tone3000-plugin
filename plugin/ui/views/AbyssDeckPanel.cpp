#include "AbyssDeckPanel.h"

#include "core/KnobScale.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

constexpr int kPadTop = 12, kPadSide = 16, kPadBottom = 8;
constexpr int kColumn = 58;  // one knob column (the time-FX rack's)
constexpr int kColumns = 6;
constexpr int kHeaderHeight = theme::kTextBoxHeight;
constexpr int kHeaderToKnobs = 12;
constexpr int kRowGap = 6;
constexpr int kToggleGap = 4;

// The processor defaults (Processor.cpp / AbyssPitch.h) on the normalised maps.
constexpr float kSubTwoDefault = 0.0f;
constexpr float kSubOneDefault = 0.7f;
constexpr float kUpDefault = 0.0f;
constexpr float kShiftLevelDefault = 0.0f;
constexpr float kShiftDefault = 0.5f;  // 0 st
constexpr float kDetuneDefault = 0.0f;
constexpr float kBendDefault = 0.5f;   // centre
constexpr float kGlideDefault = 0.2759f;  // 80 ms on the 0.4-skewed 0..2000 ms map
constexpr float kDryDefault = 1.0f;
constexpr float kWetDefault = 1.0f;
constexpr float kOutputDefault = 24.0f / 36.0f;  // 0 dB

Knob::Options deckKnob(const char* label, const KnobScale& scale, float def, help::Key help,
                       Knob::Variant variant = Knob::Variant::full) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  o.variant = variant;
  return o;
}

}  // namespace

AbyssDeckPanel::AbyssDeckPanel(Services& services)
    : power_(services.backend, "abyssEnabled", "ON", help::Key::abyssPower),
      tracking_(services.backend, "abyssTracking", {"TIGHT", "SMOOTH"}, 70, help::Key::abyssTracking),
      placement_(services.backend, "abyssPlacement", {"PRE-AMP", "POST-AMP"}, 82, help::Key::abyssPlacement),
      interval_(services.backend, "abyssInterval",
                {"FREE", "MIN 2ND UP", "MIN 2ND DOWN", "TRITONE UP", "TRITONE DOWN", "MAJ 7TH UP", "MIN 9TH DOWN"}, 112,
                help::Key::abyssInterval),
      bendRange_(services.backend, "abyssBendRange", {"BEND 12 ST", "BEND 24 ST"}, 82, help::Key::abyssBendRange),
      order_(services.backend, "insertOrder", {"ABYSS 1ST", "GLITCH 1ST"}, 90, help::Key::insertOrder),
      subTwo_(services.backend, "abyssSubTwo", deckKnob("-2 Oct", scales::percent(), kSubTwoDefault, help::Key::abyssSubTwo)),
      subOne_(services.backend, "abyssSubOne", deckKnob("-1 Oct", scales::percent(), kSubOneDefault, help::Key::abyssSubOne)),
      up_(services.backend, "abyssUp", deckKnob("+1 Oct", scales::percent(), kUpDefault, help::Key::abyssUp)),
      shiftLevel_(services.backend, "abyssShiftLevel",
                  deckKnob("Shift Lvl", scales::percent(), kShiftLevelDefault, help::Key::abyssShiftLevel)),
      shift_(services.backend, "abyssShift",
             deckKnob("Shift", scales::abyssShift(), kShiftDefault, help::Key::abyssShift, Knob::Variant::bipolar)),
      detune_(services.backend, "abyssDetune",
              deckKnob("Detune", scales::abyssDetune(), kDetuneDefault, help::Key::abyssDetune)),
      bend_(services.backend, "abyssBend",
            deckKnob("Bend", scales::abyssBend(), kBendDefault, help::Key::abyssBend, Knob::Variant::bipolar)),
      glide_(services.backend, "abyssGlide", deckKnob("Glide", scales::abyssGlideMs(), kGlideDefault, help::Key::abyssGlide)),
      dry_(services.backend, "abyssDry", deckKnob("Dry", scales::percent(), kDryDefault, help::Key::abyssDry)),
      wet_(services.backend, "abyssWet", deckKnob("Wet", scales::percent(), kWetDefault, help::Key::abyssWet)),
      output_(services.backend, "abyssOutput",
              deckKnob("Output", scales::insertOutputDb(), kOutputDefault, help::Key::abyssOutput, Knob::Variant::bipolar)) {
  for (juce::Component* c : std::initializer_list<juce::Component*>{
           &power_, &tracking_, &placement_, &interval_, &bendRange_, &order_, &subTwo_, &subOne_, &up_, &shiftLevel_,
           &shift_, &detune_, &bend_, &glide_, &dry_, &wet_, &output_})
    addAndMakeVisible(*c);
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  setSize(2 * kBorder + 2 * kPadSide + kColumns * kColumn,
          2 * kBorder + kPadTop + 2 * kHeaderHeight + kToggleGap + kHeaderToKnobs + 2 * knobs + kRowGap +
              kPadBottom);
}

void AbyssDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "abyssSubTwo").set(kSubTwoDefault);
  ParamBinding(backend, "abyssSubOne").set(kSubOneDefault);
  ParamBinding(backend, "abyssUp").set(kUpDefault);
  ParamBinding(backend, "abyssShiftLevel").set(kShiftLevelDefault);
  ParamBinding(backend, "abyssShift").set(kShiftDefault);
  ParamBinding(backend, "abyssInterval").set(0.0f);
  ParamBinding(backend, "abyssDetune").set(kDetuneDefault);
  ParamBinding(backend, "abyssBend").set(kBendDefault);
  ParamBinding(backend, "abyssGlide").set(kGlideDefault);
  ParamBinding(backend, "abyssDry").set(kDryDefault);
  ParamBinding(backend, "abyssWet").set(kWetDefault);
  ParamBinding(backend, "abyssOutput").set(kOutputDefault);
}

void AbyssDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void AbyssDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);
  const int headerY = content.getY();
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);

  power_.setTopLeftPosition(content.getX(), headerY);
  tracking_.setTopLeftPosition(power_.getRight() + kToggleGap, headerY);
  placement_.setTopLeftPosition(content.getRight() - placement_.getWidth(), headerY);
  const int row2 = headerY + kHeaderHeight + kToggleGap;
  interval_.setTopLeftPosition(content.getX(), row2);
  bendRange_.setTopLeftPosition(interval_.getRight() + kToggleGap, row2);
  order_.setTopLeftPosition(content.getRight() - order_.getWidth(), row2);

  const int knobs1 = row2 + kHeaderHeight + kHeaderToKnobs + Knob::kEditorOverflow;
  const int knobs2 = knobs1 + knobs + kRowGap;
  int x = content.getX();
  for (auto* k : {&subTwo_, &subOne_, &up_, &shiftLevel_, &shift_, &detune_}) {
    k->setBounds(x, knobs1, kColumn, knobs);
    x += kColumn;
  }
  x = content.getX();
  for (auto* k : {&bend_, &glide_, &dry_, &wet_, &output_}) {
    k->setBounds(x, knobs2, kColumn, knobs);
    x += kColumn;
  }
}

}  // namespace t3k::ui
