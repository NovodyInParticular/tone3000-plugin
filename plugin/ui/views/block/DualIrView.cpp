#include "DualIrView.h"

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {

Knob::Options knob(const char* label, const KnobScale& scale, float defaultValue, help::Key help, bool primary,
                   Knob::Variant variant) {
  Knob::Options o;
  o.label = label;
  o.size = primary ? theme::kKnobSizePrimary : theme::kKnobSizeSecondary;
  o.thumb = primary ? Knob::Thumb::primary : Knob::Thumb::secondary;
  o.variant = variant;
  o.scale = &scale;
  o.defaultValue = defaultValue;
  o.help = help;
  return o;
}

// Level trims are ±12 dB around unity; the knobs work normalised.
float levelToNorm(float db) { return (db + 12.0f) / 24.0f; }
float alignToNorm(float ms) { return (ms + 2.0f) / 4.0f; }

}  // namespace

DualIrView::DualIrView(Services& services, std::string blockId)
    : services_(services),
      blockId_(std::move(blockId)),
      levelA_(knob("Level A", scales::balanceDb(), 0.5f, help::Key::dualIrLevelA, false, Knob::Variant::bipolar)),
      blend_(knob("Blend", scales::dualIrBlend(), 0.5f, help::Key::dualIrBlend, true, Knob::Variant::full)),
      align_(knob("Align", scales::dualIrAlignMs(), 0.5f, help::Key::dualIrAlign, true, Knob::Variant::bipolar)),
      levelB_(knob("Level B", scales::balanceDb(), 0.5f, help::Key::dualIrLevelB, false, Knob::Variant::bipolar)) {
  for (juce::Component* c : std::initializer_list<juce::Component*>{&load_, &file_, &remove_, &invertA_, &invertB_,
                                                                     &levelA_, &blend_, &align_, &levelB_})
    addAndMakeVisible(*c);

  load_.onClick = [this] { services_.loadFlow.selectSecondIr(blockId_); };
  file_.onClick = [this] { services_.localFiles.pickSecondIr(blockId_); };
  remove_.onClick = [this] { services_.chain.clearSecondIr(blockId_); };
  invertA_.onClick = [this] { send("invertA", state_.invertA ? 0.0 : 1.0); };
  invertB_.onClick = [this] { send("invertB", state_.invertB ? 0.0 : 1.0); };
  levelA_.onChange = [this](float n) { send("levelA", n * 24.0 - 12.0); };
  levelB_.onChange = [this](float n) { send("levelB", n * 24.0 - 12.0); };
  blend_.onChange = [this](float n) { send("blend", n); };
  align_.onChange = [this](float n) { send("alignMs", n * 4.0 - 2.0); };
}

void DualIrView::send(const char* param, double value) { services_.chain.setDualIrParam(blockId_, param, value); }

void DualIrView::setBlock(const ChainItem& block) {
  aTitle_ = block.tone.title;
  state_ = block.dualIr.value_or(DualIrState{});
  levelA_.setValue(levelToNorm(state_.levelA));
  levelB_.setValue(levelToNorm(state_.levelB));
  blend_.setValue(state_.blend);
  align_.setValue(alignToNorm(state_.alignMs));
  invertA_.setArmed(state_.invertA);
  invertB_.setArmed(state_.invertB);
  // B's controls only mean something once there is a B.
  for (auto* c : std::initializer_list<juce::Component*>{&levelB_, &blend_, &align_, &invertB_, &levelA_, &invertA_})
    c->setAlpha(state_.hasB ? 1.0f : theme::kDisabledOpacity);
  for (auto* c : std::initializer_list<juce::Component*>{&levelB_, &blend_, &align_, &invertB_, &levelA_, &invertA_})
    c->setEnabled(state_.hasB);
  remove_.setVisible(state_.hasB);
  load_.setVisible(services_.session.authenticated());
  resized();
  repaint();
}

void DualIrView::paint(juce::Graphics& g) {
  auto names = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kPad).withHeight(kNamesHeight);
  const int half = names.getWidth() / 2;
  auto drawName = [&g](juce::Rectangle<int> box, const juce::String& tag, const juce::String& name, bool dim) {
    g.setFont(Fonts::mono(11));
    g.setColour(theme::kSubtle);
    g.drawText(tag, box.removeFromTop(16), juce::Justification::centredLeft, false);
    g.setFont(Fonts::sans(15));
    g.setColour(dim ? theme::kMuted : theme::kWhite);
    g.drawText(name, box.removeFromTop(22), juce::Justification::centredLeft, true);
  };
  drawName(names.removeFromLeft(half).withTrimmedRight(12), "IR A", aTitle_, false);
  const auto bName = state_.hasB ? (state_.bLoaded ? state_.bTitle : state_.bTitle + juce::String::fromUTF8("  (downloading\xe2\x80\xa6)"))
                                 : juce::String("No second IR: load one to blend");
  drawName(names.withTrimmedRight(220), "IR B", bName, !state_.hasB);

  const auto status = getLocalBounds().reduced(kPad, 0).removeFromBottom(kStatusHeight + 8).withHeight(kStatusHeight);
  g.setFont(Fonts::sans(12));
  g.setColour(theme::kSubtle);
  g.drawText(state_.building ? juce::String::fromUTF8("Rebuilding the blend\xe2\x80\xa6")
                             : juce::String("Blend two cabs, then nudge Align until the mix stops sounding hollow."),
             status, juce::Justification::centred, true);
}

void DualIrView::resized() {
  auto row = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kPad).withHeight(theme::kTextBoxHeight);
  // B's buttons sit right-aligned on the names row.
  int x = row.getRight();
  for (auto* b : std::initializer_list<ChromeTextButton*>{&remove_, &file_, &load_}) {
    if (!b->isVisible()) continue;
    const int w = b->getWidth() > 0 ? b->getWidth() : 64;
    b->setBounds(x - w, row.getY(), w, theme::kTextBoxHeight);
    x -= w + 12;
  }

  // Knob row: Level A + Ø A | Blend | Align | Level B + Ø B, evenly spaced.
  const int top = kPad + kNamesHeight + 16;
  const int columns = 4;
  const int colW = (getWidth() - 2 * kPad) / columns;
  auto place = [&](Knob& k, int column, juce::Component* under) {
    const int h = Knob::heightFor(k.knobSize());
    const int cx = kPad + column * colW + colW / 2;
    // Smaller knobs share the big ones' centre line.
    const int y = top + (theme::kKnobSizePrimary - k.knobSize()) / 2;
    k.setBounds(cx - k.knobSize() / 2 - 20, y, k.knobSize() + 40, h);
    if (under != nullptr)
      under->setBounds(cx - 24, y + h + 8, 48, theme::kTextBoxHeight);
  };
  place(levelA_, 0, &invertA_);
  place(blend_, 1, nullptr);
  place(align_, 2, nullptr);
  place(levelB_, 3, &invertB_);
}

}  // namespace t3k::ui
