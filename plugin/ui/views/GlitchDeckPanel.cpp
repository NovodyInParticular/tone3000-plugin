#include "GlitchDeckPanel.h"

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
constexpr int kRowGap = 6;
constexpr int kToggleGap = 4;
constexpr int kModeWidth = 82;        // fits "STUT HOLD"
constexpr int kPlacementWidth = 82;   // fits "POST-AMP"

// The processor defaults (Processor.cpp / GlitchFx.h) on the normalised maps.
constexpr float kRateDefault = 0.25f;  // 1/16 of 9 detents
constexpr float kLengthDefault = 1.0f;
constexpr float kChanceDefault = 0.0f;
constexpr float kIntensityDefault = 0.5f;
constexpr float kSeedDefault = 0.0f;
constexpr float kFadeDefault = 0.5677f;  // 150 ms on the 5..2000 ms log map
constexpr float kBitsDefault = 1.0f;
constexpr float kSampleRateDefault = 1.0f;
constexpr float kMixDefault = 1.0f;
constexpr float kOutputDefault = 24.0f / 36.0f;  // 0 dB
constexpr float kTempoDefault = 80.0f / 260.0f;  // 120 BPM

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

ChoiceCycleButton::ChoiceCycleButton(Backend& backend, const juce::String& paramId, std::vector<juce::String> names,
                                     int width, help::Key help)
    : Clickable(paramId), choice_(backend, paramId), names_(std::move(names)) {
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

int ChoiceCycleButton::index() const {
  const int count = static_cast<int>(names_.size());
  return juce::jlimit(0, count - 1, juce::roundToInt(choice_.normalised() * (count - 1)));
}

void ChoiceCycleButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
  const auto box = getLocalBounds().toFloat();
  if (highlighted) paint::fill(g, box, theme::kIconBoxRadius, theme::kHighlight);
  paint::border(g, box, theme::kIconBoxRadius, theme::kBorder);
  paint::capText(g, names_[static_cast<size_t>(index())], box, Fonts::mono(11), 11, theme::kMuted);
}

GlitchDeckPanel::GlitchDeckPanel(Services& services)
    : power_(services.backend, "glitchEnabled", "ON", help::Key::glitchPower),
      stutter_(services.backend, "glitchStutter", "glitchStutterMode", "STUTTER", help::Key::glitchStutter),
      freeze_(services.backend, "glitchFreeze", "glitchFreezeMode", "FREEZE", help::Key::glitchFreeze),
      stutterMode_(services.backend, "glitchStutterMode", {"STUT LATCH", "STUT HOLD"}, kModeWidth,
                   help::Key::glitchStutterMode),
      freezeMode_(services.backend, "glitchFreezeMode", {"FRZ LATCH", "FRZ HOLD"}, kModeWidth,
                  help::Key::glitchFreezeMode),
      placement_(services.backend, "glitchPlacement", {"PRE-AMP", "POST-AMP"}, kPlacementWidth,
                 help::Key::glitchPlacement),
      rate_(services.backend, "glitchStutterRate", deckKnob("Rate", scales::glitchRate(), kRateDefault, help::Key::glitchRate)),
      length_(services.backend, "glitchStutterLength",
              deckKnob("Length", scales::glitchLength(), kLengthDefault, help::Key::glitchLength)),
      chance_(services.backend, "glitchChance", deckKnob("Chance", scales::percent(), kChanceDefault, help::Key::glitchChance)),
      intensity_(services.backend, "glitchIntensity",
                 deckKnob("Intensity", scales::percent(), kIntensityDefault, help::Key::glitchIntensity)),
      seed_(services.backend, "glitchSeed", deckKnob("Seed", scales::glitchSeed(), kSeedDefault, help::Key::glitchSeed)),
      fade_(services.backend, "glitchFreezeFade",
            deckKnob("Fade", scales::glitchFadeMs(), kFadeDefault, help::Key::glitchFreezeFade)),
      bits_(services.backend, "glitchBits", deckKnob("Bits", scales::glitchBits(), kBitsDefault, help::Key::glitchBits)),
      sampleRate_(services.backend, "glitchRate",
                  deckKnob("Rate Hz", scales::glitchSampleRate(), kSampleRateDefault, help::Key::glitchSampleRate)),
      mix_(services.backend, "glitchMix", deckKnob("Mix", scales::percent(), kMixDefault, help::Key::glitchMix)),
      output_(services.backend, "glitchOutput",
              deckKnob("Output", scales::insertOutputDb(), kOutputDefault, help::Key::glitchOutput, Knob::Variant::bipolar)),
      tempo_(services.backend, "standaloneBpm",
             deckKnob("Tempo", scales::tempoBpm(), kTempoDefault, help::Key::standaloneBpm)) {
  for (juce::Component* c : std::initializer_list<juce::Component*>{
           &power_, &stutter_, &freeze_, &stutterMode_, &freezeMode_, &placement_, &rate_, &length_, &chance_,
           &intensity_, &seed_, &fade_, &bits_, &sampleRate_, &mix_, &output_, &tempo_})
    addAndMakeVisible(*c);
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  setSize(2 * kBorder + 2 * kPadSide + kColumns * kColumn,
          2 * kBorder + kPadTop + 2 * kHeaderHeight + kToggleGap + kHeaderToKnobs + 2 * knobs + kRowGap +
              kPadBottom);
}

void GlitchDeckPanel::resetDeck(Backend& backend) {
  ParamBinding(backend, "glitchStutter").set(false);
  ParamBinding(backend, "glitchFreeze").set(false);
  ParamBinding(backend, "glitchStutterRate").set(kRateDefault);
  ParamBinding(backend, "glitchStutterLength").set(kLengthDefault);
  ParamBinding(backend, "glitchChance").set(kChanceDefault);
  ParamBinding(backend, "glitchIntensity").set(kIntensityDefault);
  ParamBinding(backend, "glitchSeed").set(kSeedDefault);
  ParamBinding(backend, "glitchFreezeFade").set(kFadeDefault);
  ParamBinding(backend, "glitchBits").set(kBitsDefault);
  ParamBinding(backend, "glitchRate").set(kSampleRateDefault);
  ParamBinding(backend, "glitchMix").set(kMixDefault);
  ParamBinding(backend, "glitchOutput").set(kOutputDefault);
}

void GlitchDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
}

void GlitchDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);
  const int headerY = content.getY();
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);

  int x = content.getX();
  for (auto* b : std::initializer_list<juce::Component*>{&power_, &stutter_, &freeze_}) {
    b->setTopLeftPosition(x, headerY);
    x += b->getWidth() + kToggleGap;
  }
  placement_.setTopLeftPosition(content.getRight() - placement_.getWidth(), headerY);
  // Second header row: how STUTTER and FREEZE's buttons behave.
  const int modeY = headerY + kHeaderHeight + kToggleGap;
  stutterMode_.setTopLeftPosition(content.getX(), modeY);
  freezeMode_.setTopLeftPosition(stutterMode_.getRight() + kToggleGap, modeY);

  const int row1 = modeY + kHeaderHeight + kHeaderToKnobs + Knob::kEditorOverflow;
  const int row2 = row1 + knobs + kRowGap;
  x = content.getX();
  for (auto* k : {&rate_, &length_, &chance_, &intensity_, &seed_, &fade_}) {
    k->setBounds(x, row1, kColumn, knobs);
    x += kColumn;
  }
  x = content.getX();
  for (auto* k : {&bits_, &sampleRate_, &mix_, &output_, &tempo_}) {
    k->setBounds(x, row2, kColumn, knobs);
    x += kColumn;
  }
}

}  // namespace t3k::ui
