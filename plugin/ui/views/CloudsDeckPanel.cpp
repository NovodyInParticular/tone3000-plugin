#include "CloudsDeckPanel.h"

#include "core/Fonts.h"
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
constexpr int kToggleGap = 4;
constexpr int kModeWidth = 124;     // fits "LOOPING DELAY"
constexpr int kQualityWidth = 124;  // fits "16-BIT STEREO"

// The processor defaults (Processor.cpp / CloudsFx.h) on the normalised maps.
constexpr float kPositionDefault = 0.3f;
constexpr float kSizeDefault = 0.5f;
constexpr float kPitchDefault = 0.5f;  // 0 st
constexpr float kSpreadDefault = 0.5f;
constexpr float kFeedbackDefault = 0.0f;
constexpr float kReverbDefault = 0.3f;

// The faceplate knobs' defaults, reset with the deck (CloudsGroup.cpp).
constexpr float kDensityDefault = 0.65f;
constexpr float kTextureDefault = 0.5f;
constexpr float kBlendDefault = 0.5f;

constexpr std::array<const char*, 4> kModeNames = {"GRANULAR", "STRETCH", "LOOPING DELAY", "SPECTRAL"};
constexpr std::array<const char*, 4> kQualityNames = {"16-BIT STEREO", "16-BIT MONO", "8-BIT STEREO",
                                                      "8-BIT MONO"};

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

CloudsDeckPanel::CycleButton::CycleButton(Backend& backend, const juce::String& paramId,
                                          const std::array<const char*, 4>& names, int width, help::Key help)
    : Clickable(paramId), choice_(backend, paramId), names_(names) {
  setHelpText(help::text(help));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setSize(width, theme::kTextBoxHeight);
  choice_.onChange = [this] { repaint(); };
  onClick = [this] {
    const int count = static_cast<int>(names_.size());
    choice_.set(static_cast<float>((index() + 1) % count) / static_cast<float>(count - 1));
    repaint();
  };
}

int CloudsDeckPanel::CycleButton::index() const {
  const int count = static_cast<int>(names_.size());
  return juce::jlimit(0, count - 1, juce::roundToInt(choice_.normalised() * (count - 1)));
}

void CloudsDeckPanel::CycleButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
  const auto box = getLocalBounds().toFloat();
  if (highlighted) paint::fill(g, box, theme::kIconBoxRadius, theme::kHighlight);
  paint::border(g, box, theme::kIconBoxRadius, theme::kBorder);
  paint::capText(g, names_[static_cast<size_t>(index())], box, Fonts::mono(11), 11, theme::kMuted);
}

CloudsDeckPanel::CloudsDeckPanel(Services& services)
    : freeze_(services.backend, "cloudsFreeze", "FREEZE", help::Key::cloudsFreeze),
      mode_(services.backend, "cloudsMode", kModeNames, kModeWidth, help::Key::cloudsMode),
      quality_(services.backend, "cloudsQuality", kQualityNames, kQualityWidth, help::Key::cloudsQuality),
      position_(services.backend, "cloudsPosition",
                deckKnob("Position", scales::percent(), kPositionDefault, help::Key::cloudsPosition)),
      size_(services.backend, "cloudsSize", deckKnob("Size", scales::percent(), kSizeDefault, help::Key::cloudsSize)),
      pitch_(services.backend, "cloudsPitch",
             deckKnob("Pitch", scales::semitones(), kPitchDefault, help::Key::cloudsPitch, Knob::Variant::bipolar)),
      spread_(services.backend, "cloudsSpread",
              deckKnob("Spread", scales::percent(), kSpreadDefault, help::Key::cloudsSpread)),
      feedback_(services.backend, "cloudsFeedback",
                deckKnob("Feedback", scales::percent(), kFeedbackDefault, help::Key::cloudsFeedback)),
      reverb_(services.backend, "cloudsReverb",
              deckKnob("Reverb", scales::percent(), kReverbDefault, help::Key::cloudsReverb)) {
  for (juce::Component* c : std::initializer_list<juce::Component*>{
           &freeze_, &mode_, &quality_, &position_, &size_, &pitch_, &spread_, &feedback_, &reverb_})
    addAndMakeVisible(*c);
  primaryOnly = true;           // right-click toggles the panel; don't dismiss on it
  dismissOnAnchorPress = true;  // the anchor is a Clouds knob, a control
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  setSize(2 * kBorder + 2 * kPadSide + kColumns * kColumn,
          2 * kBorder + kPadTop + kHeaderHeight + kHeaderToKnobs + knobs + kPadBottom);
}

void CloudsDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "cloudsFreeze").set(false);
  ParamBinding(backend, "cloudsMode").set(0.0f);
  ParamBinding(backend, "cloudsQuality").set(0.0f);
  ParamBinding(backend, "cloudsPosition").set(kPositionDefault);
  ParamBinding(backend, "cloudsSize").set(kSizeDefault);
  ParamBinding(backend, "cloudsPitch").set(kPitchDefault);
  ParamBinding(backend, "cloudsSpread").set(kSpreadDefault);
  ParamBinding(backend, "cloudsFeedback").set(kFeedbackDefault);
  ParamBinding(backend, "cloudsReverb").set(kReverbDefault);
  ParamBinding(backend, "cloudsDensity").set(kDensityDefault);
  ParamBinding(backend, "cloudsTexture").set(kTextureDefault);
  ParamBinding(backend, "cloudsBlend").set(kBlendDefault);
}

void CloudsDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void CloudsDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);
  const int headerY = content.getY();
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  const int knobY = content.getBottom() - knobs + Knob::kEditorOverflow;

  freeze_.setTopLeftPosition(content.getX(), headerY);
  quality_.setTopLeftPosition(content.getRight() - quality_.getWidth(), headerY);
  mode_.setTopLeftPosition(quality_.getX() - kToggleGap - mode_.getWidth(), headerY);

  int x = content.getX();
  for (auto* k : {&position_, &size_, &pitch_, &spread_, &feedback_, &reverb_}) {
    k->setBounds(x, knobY, kColumn, knobs);
    x += kColumn;
  }
}

}  // namespace t3k::ui
