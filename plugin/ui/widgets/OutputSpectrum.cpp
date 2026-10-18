#include "OutputSpectrum.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kLowRateMs = 125;
constexpr float kTopDb = 0.0f, kBottomDb = -90.0f;  // the plotted range
constexpr float kDbColumnWidth = 22.0f;              // the dB scale's labels
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

juce::Rectangle<float> OutputSpectrum::plotArea() const {
  auto plot = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
  plot.removeFromTop(14.0f + 6.0f);  // the title, and room for the 0 dB label under it
  plot.removeFromBottom(12.0f);      // the Hz row
  plot.removeFromLeft(kDbColumnWidth + 4.0f);
  return plot;
}

float OutputSpectrum::xOf(double hz) const {
  const auto plot = plotArea();
  return plot.getX() + plot.getWidth() * static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
}

float OutputSpectrum::yOf(float db) const {
  const auto plot = plotArea();
  return plot.getBottom() - (db - kBottomDb) / (kTopDb - kBottomDb) * plot.getHeight();
}

// The frame, title and both scales. Drawn directly every frame: blitting a
// cached image of it measured slower than the few strokes and labels.
void OutputSpectrum::paintBackdrop(juce::Graphics& g) const {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, 8.0f, theme::kSurface.withAlpha(0.85f));
  paint::border(g, box, 8.0f, theme::kBorder);
  const auto inner = box.reduced(10.0f, 8.0f);
  const auto plot = plotArea();
  g.setFont(theme::kStyle.monoLabels ? Fonts::mono(10) : Fonts::sans(11));
  g.setColour(theme::kSubtle);
  g.drawText(rate_ == "off" ? "OUTPUT SPECTRUM  (off)" : "OUTPUT SPECTRUM", inner.withHeight(14.0f),
             juce::Justification::topLeft, false);
  const auto hzRow = inner.withTrimmedTop(inner.getHeight() - 12.0f);
  const auto dbColumn = juce::Rectangle<float>(inner.getX(), plot.getY(), kDbColumnWidth, plot.getHeight());
  // Octave guides at 100 Hz, 1 kHz, 10 kHz on the 20 Hz..20 kHz log axis,
  // and level guides every 30 dB.
  const auto guide = theme::kBorder.withMultipliedAlpha(0.6f);
  for (const auto& [hz, name] : {std::pair{100.0, "100"}, std::pair{1000.0, "1k"}, std::pair{10000.0, "10k"}}) {
    const float x = xOf(hz);
    g.setColour(guide);
    g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    g.setColour(theme::kSubtle);
    g.drawText(name, juce::Rectangle<float>(40.0f, hzRow.getHeight()).withCentre({x, hzRow.getCentreY()}),
               juce::Justification::centred, false);
  }
  g.setColour(theme::kSubtle);
  g.drawText("Hz", hzRow.withTrimmedLeft(hzRow.getWidth() - 24.0f), juce::Justification::centredRight, false);
  for (const float db : {0.0f, -30.0f, -60.0f, -90.0f}) {
    const float y = yOf(db);
    if (db > kBottomDb && db < kTopDb) {
      g.setColour(guide.withMultipliedAlpha(0.6f));
      g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
    }
    g.setColour(theme::kSubtle);
    const auto row = juce::Rectangle<float>(dbColumn.getX(), y - 6.0f, dbColumn.getWidth(), 12.0f)
                         .constrainedWithin(dbColumn.expanded(0.0f, 6.0f));
    g.drawText(juce::String(static_cast<int>(db)), row, juce::Justification::centredRight, false);
  }
}

void OutputSpectrum::paint(juce::Graphics& g) {
  paintBackdrop(g);
  const auto plot = plotArea();
  if (feed_ == nullptr) return;
  const auto& bins = feed_->bins();
  if (bins.size() < 2) return;

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
