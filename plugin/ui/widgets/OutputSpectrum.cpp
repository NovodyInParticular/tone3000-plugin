#include "OutputSpectrum.h"

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"

namespace t3k::ui {

namespace {
constexpr int kLowRateMs = 125;
constexpr float kTopDb = 0.0f, kBottomDb = -90.0f;  // the plotted range
constexpr float kDbColumnWidth = 22.0f;              // the dB scale's labels
}

// The "DRY IN" switch in the panel's top-right corner.
class OutputSpectrum::Chip : public Clickable {
public:
  Chip() : Clickable("DRY IN") {
    setClickingTogglesState(true);
    setHelpText("Dry input: a faint second trace of the signal going into the rig, to compare with the output.");
  }
  void paintButton(juce::Graphics& g, bool over, bool) override {
    const bool on = getToggleState();
    const auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(on ? theme::kWhite.withAlpha(0.85f) : over ? theme::kMuted : theme::kSubtle);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    g.setFont(Fonts::mono(9, on));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
  }
};

OutputSpectrum::OutputSpectrum(Backend& backend, UiClock& clock)
    : backend_(backend), clock_(clock), chip_(std::make_unique<Chip>()) {
  setInterceptsMouseClicks(false, true);  // only the chip takes clicks
  addAndMakeVisible(*chip_);
  chip_->onClick = [this] {
    if (onInputTraceChange) onInputTraceChange(chip_->getToggleState());
    else setInputTrace(chip_->getToggleState());
  };
  setRate("normal");
}

OutputSpectrum::~OutputSpectrum() {
  inputFeed_.reset();
  feed_.reset();
}

void OutputSpectrum::setRate(const juce::String& rate) {
  const juce::String r = rate == "low" || rate == "off" ? rate : juce::String("normal");
  if (r == rate_ && (feed_ != nullptr) == (r != "off")) return;
  rate_ = r;
  feed_.reset();
  inputFeed_.reset();
  syncFeeds();
}

void OutputSpectrum::setInputTrace(bool on) {
  chip_->setToggleState(on, juce::dontSendNotification);
  if (on == inputTrace_) return;
  inputTrace_ = on;
  syncFeeds();
}

// One feed per shown trace; a feed switches its native tap on for as long
// as it lives.
void OutputSpectrum::syncFeeds() {
  const bool live = rate_ != "off";
  const int interval = rate_ == "low" ? kLowRateMs : 0;
  if (live && feed_ == nullptr) {
    feed_ = std::make_unique<SpectrumFeed>(backend_, clock_, kOutputId, interval);
    feed_->onChange = [this] { repaint(); };
  }
  if (!live) feed_.reset();
  if (live && inputTrace_ && inputFeed_ == nullptr) {
    inputFeed_ = std::make_unique<SpectrumFeed>(backend_, clock_, kInputId, interval);
    inputFeed_->onChange = [this] { repaint(); };
  }
  if (!live || !inputTrace_) inputFeed_.reset();
  chip_->setVisible(live);
  repaint();
}

void OutputSpectrum::resized() {
  const auto inner = getLocalBounds().reduced(10, 8);
  chip_->setBounds(inner.getRight() - 46, inner.getY(), 46, 14);
}

juce::Path OutputSpectrum::tracePath(const std::vector<float>& bins) const {
  const auto plot = plotArea();
  juce::Path line;
  for (size_t i = 0; i < bins.size(); ++i) {
    const float x = plot.getX() + plot.getWidth() * static_cast<float>(i) / static_cast<float>(bins.size() - 1);
    const float t = juce::jlimit(0.0f, 1.0f, (bins[i] - kBottomDb) / (kTopDb - kBottomDb));
    const float y = plot.getBottom() - t * plot.getHeight();
    if (i == 0) line.startNewSubPath(x, y);
    else line.lineTo(x, y);
  }
  return line;
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
  // The dry input: a faint line over the output's fill, under its trace.
  const auto ghost = [&] {
    if (inputFeed_ == nullptr || inputFeed_->bins().size() < 2) return;
    g.setColour(theme::kWhite.withAlpha(0.38f));
    g.strokePath(tracePath(inputFeed_->bins()),
                 juce::PathStrokeType(1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  };
  if (feed_ == nullptr || feed_->bins().size() < 2) {
    ghost();
    return;
  }
  const juce::Path line = tracePath(feed_->bins());
  juce::Path fill(line);
  fill.lineTo(plot.getRight(), plot.getBottom());
  fill.lineTo(plot.getX(), plot.getBottom());
  fill.closeSubPath();
  g.setGradientFill(juce::ColourGradient::vertical(theme::kBrandYellow.withAlpha(0.28f), plot.getY(),
                                                   theme::kBrandYellow.withAlpha(0.0f), plot.getBottom()));
  g.fillPath(fill);
  ghost();
  if (const float glow = theme::kStyle.glow; glow > 0.0f) {
    g.setColour(theme::kBrandYellow.withAlpha(0.18f * glow));
    g.strokePath(line, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  g.setColour(theme::kBrandYellow);
  g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

}  // namespace t3k::ui
