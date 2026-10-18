#include "HoverTip.h"

#include "core/Fonts.h"
#include "core/Theme.h"
#include "widgets/Popover.h"

namespace t3k::ui {

namespace {
constexpr float kTextPx = 12.0f;
constexpr float kPadX = 8.0f, kHeight = 22.0f, kGap = 6.0f, kMaxWidth = 420.0f;

juce::Font tipFont() { return theme::kStyle.monoLabels ? Fonts::mono(kTextPx - 1) : Fonts::sans(kTextPx); }
}  // namespace

class HoverTip::Bubble : public juce::Component {
public:
  Bubble() {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);  // the anchor's help text already says it
    setComponentID(HoverTip::kComponentId);
  }

  void setText(const juce::String& text) {
    if (text_ == text) return;
    text_ = text;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(theme::kSurfaceRaised);
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(theme::kBorder);
    g.drawRoundedRectangle(box, 4.0f, 1.0f);
    g.setColour(theme::kWhite);
    g.setFont(tipFont());
    g.drawText(text_, getLocalBounds().toFloat().reduced(kPadX, 0.0f), juce::Justification::centred, true);
  }

private:
  juce::String text_;
};

HoverTip::HoverTip() = default;
HoverTip::~HoverTip() = default;

void HoverTip::show(juce::Component& anchor, juce::Rectangle<float> area, const juce::String& text) {
  auto* host = anchor.findParentComponentOfClass<OverlayHost>();
  if (host == nullptr || anchor.getWidth() <= 0 || text.isEmpty()) {
    hide();
    return;
  }
  auto& overlay = host->overlayLayer();
  if (bubble_ == nullptr) bubble_ = std::make_unique<Bubble>();
  if (bubble_->getParentComponent() != &overlay) overlay.addAndMakeVisible(*bubble_);
  bubble_->toFront(false);
  // Overlay px; the scale the anchor is shown at goes back in as a transform
  // so the text renders at that scale (as the knob readout does).
  const auto slot = overlay.getLocalArea(&anchor, area);
  const float k = juce::jmax(0.1f, overlay.getLocalArea(&anchor, juce::Rectangle<float>(100.0f, 1.0f)).getWidth() / 100.0f);
  const float w = juce::jmin(kMaxWidth, Fonts::width(tipFont(), text) + 2.0f * kPadX + 2.0f);
  const auto bounds = overlay.getLocalBounds().toFloat() / k;
  auto tip = juce::Rectangle<float>(w, kHeight).withCentre({slot.getCentreX() / k, 0.0f}).withY(slot.getBottom() / k + kGap);
  if (tip.getBottom() > bounds.getBottom()) tip.setY(slot.getY() / k - kGap - kHeight);
  tip = tip.constrainedWithin(bounds);
  bubble_->setTransform(juce::approximatelyEqual(k, 1.0f) ? juce::AffineTransform() : juce::AffineTransform::scale(k));
  bubble_->setBounds(tip.toNearestInt());
  bubble_->setText(text);
}

void HoverTip::hide() { bubble_.reset(); }

}  // namespace t3k::ui
