#include "LiteSuggestionView.h"

#include "core/Design.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPad = 14;
constexpr int kRightInset = 72;  // clears the output meter column
constexpr int kButtonGap = 8;
}  // namespace

LiteSuggestionView::LiteSuggestionView(LiteAdvisor& advisor) : advisor_(advisor) {
  for (auto* b : {&primary_, &secondary_}) {
    b->setMetrics(PillButton::kOutline);
    addAndMakeVisible(*b);
  }
  primary_.onClick = [this] {
    if (advisor_.mode() == LiteAdvisor::Mode::suggest) advisor_.switchToLite();
    else advisor_.backToFull();
  };
  secondary_.onClick = [this] { advisor_.dismiss(); };
  advisor_.addListener(this);
  setSize(kWidth, kHeight);
  liteAdvisorChanged();
}

LiteSuggestionView::~LiteSuggestionView() { advisor_.removeListener(this); }

void LiteSuggestionView::setBottomOffset(int bottom) {
  bottom_ = bottom;
  layout();
}

void LiteSuggestionView::layout() {
  if (auto* parent = getParentComponent())
    setTopLeftPosition(parent->getWidth() - kWidth - kRightInset, parent->getHeight() - bottom_ - kHeight);
}

juce::String LiteSuggestionView::message() const {
  if (advisor_.mode() == LiteAdvisor::Mode::suggest) {
    const int n = advisor_.eligibleCount();
    return "CPU load is high. Switch " + juce::String(n) + (n == 1 ? " NAM block" : " NAM blocks") +
           " to A2-Lite? Same capture, lighter model.";
  }
  const int n = static_cast<int>(advisor_.switchedBlocks().size());
  return juce::String(n) + (n == 1 ? " block is" : " blocks are") +
         " running A2-Lite (marked LITE on the chain).";
}

void LiteSuggestionView::liteAdvisorChanged() {
  const auto mode = advisor_.mode();
  setVisible(mode != LiteAdvisor::Mode::hidden);
  primary_.setLabel(mode == LiteAdvisor::Mode::switched ? "Back to Full" : "Switch to Lite");
  secondary_.setLabel(mode == LiteAdvisor::Mode::switched ? "OK" : "Not now");
  resized();
  repaint();
}

void LiteSuggestionView::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
  paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  auto text = getLocalBounds().reduced(kPad);
  text.removeFromBottom(primary_.getHeight() + kButtonGap);
  g.setColour(theme::kWhite);
  g.setFont(Fonts::sans(13));
  g.drawFittedText(message(), text, juce::Justification::topLeft, 3, 1.0f);
}

void LiteSuggestionView::resized() {
  int x = getWidth() - kPad;
  const int y = getHeight() - kPad - primary_.getHeight();
  for (auto* b : {&secondary_, &primary_}) {
    x -= b->getWidth();
    b->setTopLeftPosition(x, y);
    x -= kButtonGap;
  }
}

}  // namespace t3k::ui
