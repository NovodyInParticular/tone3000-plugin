#include "FxDeckPanel.h"

#include <array>

#include "core/Fonts.h"
#include "core/KnobScale.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

constexpr int kPadTop = 12, kPadSide = 16, kPadBottom = 8;
constexpr int kColumn = 58;         // one knob column
constexpr int kSectionGap = 22;     // between MOD / DLY / REV
constexpr int kHeaderHeight = theme::kTextBoxHeight;
constexpr int kHeaderToKnobs = 12;
constexpr int kToggleGap = 4;
constexpr int kOrderWidth = 168;

constexpr int kModColumns = 3, kDelayColumns = 4, kReverbColumns = 4;

// Processor defaults on the normalised maps (Processor.cpp / KnobScale.h).
constexpr float kDelayTimeDefault = 0.7814f;   // 380 ms
constexpr float kDivisionDefault = 0.5f;        // 1/8
constexpr float kFeedbackDefault = 0.3684f;     // 35 %
constexpr float kDecayDefault = 0.5617f;        // 1.8 s
constexpr float kRateDefault = 0.5233f;         // 0.8 Hz

constexpr std::array<const char*, 6> kOrderNames = {
    "MOD > DLY > REV", "MOD > REV > DLY", "DLY > MOD > REV",
    "DLY > REV > MOD", "REV > MOD > DLY", "REV > DLY > MOD"};

Knob::Options deckKnob(const char* label, const KnobScale& scale, float def, help::Key help,
                       std::optional<int> steps = {}) {
  Knob::Options o;
  o.label = label;
  o.size = theme::kKnobSizeSecondary;
  o.thumb = Knob::Thumb::secondary;
  o.scale = &scale;
  o.defaultValue = def;
  o.help = help;
  o.steps = steps;
  return o;
}

int sectionWidth(int columns) { return columns * kColumn; }

}  // namespace

FxDeckPanel::OrderButton::OrderButton(Backend& backend) : Clickable("Order"), order_(backend, "fxOrder") {
  setHelpText(help::text(help::Key::fxOrder));
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  setSize(kOrderWidth, theme::kTextBoxHeight);
  order_.onChange = [this] { repaint(); };
  onClick = [this] {
    const int count = static_cast<int>(kOrderNames.size());
    const int index = juce::jlimit(0, count - 1, juce::roundToInt(order_.normalised() * (count - 1)));
    order_.set(static_cast<float>((index + 1) % count) / static_cast<float>(count - 1));
    repaint();
  };
}

void FxDeckPanel::OrderButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
  const auto box = getLocalBounds().toFloat();
  if (highlighted) paint::fill(g, box, theme::kIconBoxRadius, theme::kHighlight);
  paint::border(g, box, theme::kIconBoxRadius, theme::kBorder);
  const int count = static_cast<int>(kOrderNames.size());
  const int index = juce::jlimit(0, count - 1, juce::roundToInt(order_.normalised() * (count - 1)));
  paint::capText(g, kOrderNames[static_cast<size_t>(index)], box, Fonts::mono(11), 11, theme::kMuted);
}

FxDeckPanel::FxDeckPanel(Services& services)
    : modOn_(services.backend, "fxModEnabled", "MOD", help::Key::fxModPower),
      flanger_(services.backend, "fxModType", "FLANGER", help::Key::fxModType),
      modRate_(services.backend, "fxModRate", deckKnob("Rate", scales::fxRateHz(), kRateDefault, help::Key::fxModRate)),
      modDepth_(services.backend, "fxModDepth", deckKnob("Depth", scales::percent(), 0.5f, help::Key::fxModDepth)),
      modMix_(services.backend, "fxModMix", deckKnob("Mix", scales::percent(), 0.5f, help::Key::fxModMix)),
      delayOn_(services.backend, "fxDelayEnabled", "DLY", help::Key::fxDelayPower),
      sync_(services.backend, "fxDelaySync", "SYNC", help::Key::fxDelaySync),
      pingPong_(services.backend, "fxDelayPingPong", "PING", help::Key::fxDelayPingPong),
      delayTime_(services.backend, "fxDelayTime",
                 deckKnob("Time", scales::fxDelayMs(), kDelayTimeDefault, help::Key::fxDelayTime)),
      delayDivision_(services.backend, "fxDelayDivision",
                     deckKnob("Division", scales::fxDivision(), kDivisionDefault, help::Key::fxDelayDivision,
                              scales::kFxDivisions)),
      delayFeedback_(services.backend, "fxDelayFeedback",
                     deckKnob("Feedback", scales::fxFeedback(), kFeedbackDefault, help::Key::fxDelayFeedback)),
      delayMix_(services.backend, "fxDelayMix", deckKnob("Mix", scales::percent(), 0.3f, help::Key::fxDelayMix)),
      delayDamping_(services.backend, "fxDelayDamping",
                    deckKnob("Damping", scales::percent(), 0.3f, help::Key::fxDelayDamping)),
      reverbOn_(services.backend, "fxReverbEnabled", "REV", help::Key::fxReverbPower),
      reverbSize_(services.backend, "fxReverbSize", deckKnob("Size", scales::percent(), 0.5f, help::Key::fxReverbSize)),
      reverbDecay_(services.backend, "fxReverbDecay",
                   deckKnob("Decay", scales::fxDecayS(), kDecayDefault, help::Key::fxReverbDecay)),
      reverbDamping_(services.backend, "fxReverbDamping",
                     deckKnob("Damping", scales::percent(), 0.4f, help::Key::fxReverbDamping)),
      reverbMix_(services.backend, "fxReverbMix", deckKnob("Mix", scales::percent(), 0.25f, help::Key::fxReverbMix)),
      order_(services.backend),
      syncBinding_(services.backend, "fxDelaySync") {
  for (juce::Component* c :
       std::initializer_list<juce::Component*>{&modOn_, &flanger_, &modRate_, &modDepth_, &modMix_, &delayOn_, &sync_,
                                               &pingPong_, &delayTime_, &delayFeedback_, &delayMix_, &delayDamping_,
                                               &reverbOn_, &reverbSize_, &reverbDecay_, &reverbDamping_, &reverbMix_,
                                               &order_})
    addAndMakeVisible(*c);
  addChildComponent(delayDivision_);
  syncBinding_.onChange = [this] { syncSync(); };
  // The SYNC toggle's own click writes synchronously; follow it at once.
  sync_.onClick = [this] {
    sync_.binding().set(!sync_.binding().boolValue());
    sync_.setArmed(sync_.binding().boolValue());
    syncSync();
  };
  syncSync();

  // The anchor is the FX group itself, the panel's toggle: its caption
  // closes the panel on click, and its power toggles stay usable.
  primaryOnly = true;
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  setSize(2 * kBorder + 2 * kPadSide + sectionWidth(kModColumns) + sectionWidth(kDelayColumns) +
              sectionWidth(kReverbColumns) + 2 * kSectionGap,
          2 * kBorder + kPadTop + kHeaderHeight + kHeaderToKnobs + knobs + kPadBottom);
}

void FxDeckPanel::syncSync() {
  const bool synced = syncBinding_.boolValue();
  delayTime_.setVisible(!synced);
  delayDivision_.setVisible(synced);
}

void FxDeckPanel::resetRack(Backend& backend) {
  for (const auto* id : {"fxModEnabled", "fxDelayEnabled", "fxReverbEnabled", "fxDelaySync", "fxDelayPingPong",
                         "fxModType"})
    ParamBinding(backend, id).set(false);
  ParamBinding(backend, "fxModRate").set(kRateDefault);
  ParamBinding(backend, "fxModDepth").set(0.5f);
  ParamBinding(backend, "fxModMix").set(0.5f);
  ParamBinding(backend, "fxDelayTime").set(kDelayTimeDefault);
  ParamBinding(backend, "fxDelayDivision").set(kDivisionDefault);
  ParamBinding(backend, "fxDelayFeedback").set(kFeedbackDefault);
  ParamBinding(backend, "fxDelayMix").set(0.3f);
  ParamBinding(backend, "fxDelayDamping").set(0.3f);
  ParamBinding(backend, "fxReverbSize").set(0.5f);
  ParamBinding(backend, "fxReverbDecay").set(kDecayDefault);
  ParamBinding(backend, "fxReverbDamping").set(0.4f);
  ParamBinding(backend, "fxReverbMix").set(0.25f);
  ParamBinding(backend, "fxOrder").set(0.0f);
}

void FxDeckPanel::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  // Hairlines between the three sections.
  const auto content = contentBounds();
  const float top = static_cast<float>(content.getY() + kPadTop);
  const float bottom = static_cast<float>(content.getBottom() - kPadBottom);
  int x = content.getX() + kPadSide + sectionWidth(kModColumns) + kSectionGap / 2;
  for (const int columns : {kDelayColumns, 0}) {
    g.setColour(theme::kBorder);
    g.drawVerticalLine(x, top, bottom);
    x += kSectionGap + sectionWidth(columns);
  }
}

void FxDeckPanel::resized() {
  auto content = contentBounds();
  content.removeFromTop(kPadTop);
  content.removeFromBottom(kPadBottom);
  content.reduce(kPadSide, 0);
  const int headerY = content.getY();
  const int knobs = Knob::heightFor(theme::kKnobSizeSecondary);
  const int knobY = content.getBottom() - knobs + Knob::kEditorOverflow;

  const auto row = [&](int x, std::initializer_list<juce::Component*> toggles) {
    for (auto* t : toggles) {
      t->setTopLeftPosition(x, headerY);
      x += t->getWidth() + kToggleGap;
    }
  };
  const auto knobsAt = [&](int x, std::initializer_list<juce::Component*> ks) {
    for (auto* k : ks) {
      k->setBounds(x, knobY, kColumn, knobs);
      x += kColumn;
    }
  };

  int x = content.getX();
  row(x, {&modOn_, &flanger_});
  knobsAt(x, {&modRate_, &modDepth_, &modMix_});
  x += sectionWidth(kModColumns) + kSectionGap;

  row(x, {&delayOn_, &sync_, &pingPong_});
  knobsAt(x, {&delayTime_, &delayFeedback_, &delayMix_, &delayDamping_});
  delayDivision_.setBounds(delayTime_.getBounds());
  x += sectionWidth(kDelayColumns) + kSectionGap;

  row(x, {&reverbOn_});
  knobsAt(x, {&reverbSize_, &reverbDecay_, &reverbDamping_, &reverbMix_});

  order_.setTopLeftPosition(content.getRight() - order_.getWidth(), headerY);
}

}  // namespace t3k::ui
