#include "OutputSpectrum.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kLowRateMs = 125;
}

OutputSpectrum::OutputSpectrum(Backend& backend, UiClock& clock) : backend_(backend), clock_(clock) {
  setInterceptsMouseClicks(false, false);
  setRate("normal");
}

OutputSpectrum::~OutputSpectrum() { feed_.reset(); }

void OutputSpectrum::setRate(const juce::String& rate) {
  const juce::String r = rate == "low" || rate == "off" ? rate : juce::String("normal");
  if (r == rate_ && (feed_ != nullptr) == (r != "off")) return;
  rate_ = r;
  feed_.reset();
  if (rate_ != "off") {
    feed_ = std::make_unique<SpectrumFeed>(backend_, clock_, kOutputId, rate_ == "low" ? kLowRateMs : 0);
    feed_->onChange = [this] { repaint(); };
  }
  repaint();
}

void OutputSpectrum::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, 8.0f, theme::kSurface.withAlpha(0.85f));
  paint::border(g, box, 8.0f, theme::kBorder);
  auto plot = box.reduced(10.0f, 8.0f);
  g.setFont(theme::kStyle.monoLabels ? Fonts::mono(10) : Fonts::sans(11));
  g.setColour(theme::kSubtle);
  g.drawText(rate_ == "off" ? "OUTPUT SPECTRUM  (off)" : "OUTPUT SPECTRUM", plot.removeFromTop(14.0f),
             juce::Justification::topLeft, false);
  // Octave guides at 100 Hz, 1 kHz, 10 kHz on the 20 Hz..20 kHz log axis.
  g.setColour(theme::kBorder.withMultipliedAlpha(0.6f));
  for (const double hz : {100.0, 1000.0, 10000.0}) {
    const float x = plot.getX() + plot.getWidth() * static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
    g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
  }
  if (feed_ == nullptr) return;
  const auto& bins = feed_->bins();
  if (bins.size() < 2) return;

  constexpr float kTopDb = 0.0f, kBottomDb = -90.0f;
  juce::Path line;
  for (size_t i = 0; i < bins.size(); ++i) {
    const float x = plot.getX() + plot.getWidth() * static_cast<float>(i) / static_cast<float>(bins.size() - 1);
    const float t = juce::jlimit(0.0f, 1.0f, (bins[i] - kBottomDb) / (kTopDb - kBottomDb));
    const float y = plot.getBottom() - t * plot.getHeight();
    if (i == 0) line.startNewSubPath(x, y);
    else line.lineTo(x, y);
  }
  juce::Path fill(line);
  fill.lineTo(plot.getRight(), plot.getBottom());
  fill.lineTo(plot.getX(), plot.getBottom());
  fill.closeSubPath();
  g.setGradientFill(juce::ColourGradient::vertical(theme::kBrandYellow.withAlpha(0.28f), plot.getY(),
                                                   theme::kBrandYellow.withAlpha(0.0f), plot.getBottom()));
  g.fillPath(fill);
  if (const float glow = theme::kStyle.glow; glow > 0.0f) {
    g.setColour(theme::kBrandYellow.withAlpha(0.18f * glow));
    g.strokePath(line, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  g.setColour(theme::kBrandYellow);
  g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace t3k::ui
