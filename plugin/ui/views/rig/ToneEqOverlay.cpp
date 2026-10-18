#include "ToneEqOverlay.h"

#include <cmath>

#include "core/EqMath.h"
#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kCurvePoints = 96;
constexpr float kFineFactor = 8.0f;
}  // namespace

const std::array<ToneEqOverlay::Band, 3>& ToneEqOverlay::bands() {
  static const std::array<Band, 3> b{{{"Bass", "toneBass", 150.0, 4.0f},
                                      {"Middle", "toneMid", 425.0, 3.0f},
                                      {"Treble", "toneTreble", 1800.0, 2.0f}}};
  return b;
}

float ToneEqOverlay::gainDb(int band, float normalised) {
  return bands()[static_cast<size_t>(band)].dbPerStep * (10.0f * normalised - 5.0f);
}

float ToneEqOverlay::normalisedFor(int band, float db) {
  return juce::jlimit(0.0f, 1.0f, (db / bands()[static_cast<size_t>(band)].dbPerStep + 5.0f) / 10.0f);
}

ToneEqOverlay::ToneEqOverlay(Services& services) : services_(services) {
  for (size_t i = 0; i < 3; ++i) {
    params_[i] = std::make_unique<ParamBinding>(services_.backend, bands()[i].paramId);
    params_[i]->onChange = [this] {
      curveFor_ = {};
      repaint();
    };
  }
  enabled_ = std::make_unique<ParamBinding>(services_.backend, "toneEqEnabled");
  enabled_->onChange = [this] { repaint(); };
  setRepaintsOnMouseActivity(false);
}

ToneEqOverlay::~ToneEqOverlay() = default;

float ToneEqOverlay::xOf(double hz) const {
  return static_cast<float>(getWidth()) * static_cast<float>(std::log(hz / 20.0) / std::log(1000.0));
}

float ToneEqOverlay::yOf(float db) const {
  const float mid = static_cast<float>(getHeight()) / 2.0f;
  return mid - juce::jlimit(-kRangeDb, kRangeDb, db) / kRangeDb * (mid - kHandleRadius);
}

// The stack's three biquads exactly as updateEqCoefficients builds them.
std::vector<EqBand> ToneEqOverlay::stack() const {
  std::vector<EqBand> out(3);
  for (int i = 0; i < 3; ++i) {
    auto& b = out[static_cast<size_t>(i)];
    b.freqHz = bands()[static_cast<size_t>(i)].freqHz;
    b.gainDb = gainDb(i, params_[static_cast<size_t>(i)]->normalised());
    b.type = i == 0 ? EqBandType::lowshelf : i == 1 ? EqBandType::bell : EqBandType::highshelf;
    b.q = i == 1 ? (b.gainDb < 0.0 ? 1.5 : 0.7) : 0.707;
  }
  return out;
}

double ToneEqOverlay::sampleRate() const {
  const double rate = services_.chain.state().sampleRate;
  return rate > 0 ? rate : 48000.0;
}

float ToneEqOverlay::responseAt(double hz) const {
  return static_cast<float>(eq::responseDb(stack(), sampleRate(), {hz})[0]);
}

void ToneEqOverlay::rebuildCurve() {
  curveFor_ = getLocalBounds();
  curve_.clear();
  std::vector<double> freqs(kCurvePoints);
  for (int i = 0; i < kCurvePoints; ++i) freqs[static_cast<size_t>(i)] = 20.0 * std::pow(1000.0, i / double(kCurvePoints - 1));
  const auto db = eq::responseDb(stack(), sampleRate(), freqs);
  for (int i = 0; i < kCurvePoints; ++i) {
    const float x = static_cast<float>(getWidth()) * static_cast<float>(i) / static_cast<float>(kCurvePoints - 1);
    const float y = yOf(static_cast<float>(db[static_cast<size_t>(i)]));
    if (i == 0) curve_.startNewSubPath(x, y);
    else curve_.lineTo(x, y);
  }
}

juce::Point<float> ToneEqOverlay::handlePosition(int band) const {
  const auto& b = bands()[static_cast<size_t>(band)];
  return {xOf(b.freqHz), yOf(gainDb(band, params_[static_cast<size_t>(band)]->normalised()))};
}

void ToneEqOverlay::paint(juce::Graphics& g) {
  if (curveFor_ != getLocalBounds()) rebuildCurve();
  const bool on = enabled_->boolValue();
  const auto colour = on ? theme::kBrandRed : theme::kGray;
  // The 0 dB line the curve bends away from.
  g.setColour(colour.withAlpha(0.25f));
  g.drawHorizontalLine(juce::roundToInt(yOf(0.0f)), 0.0f, static_cast<float>(getWidth()));
  // Tagged at its right end: this line is the stack's 0 dB, not a level.
  g.setFont(Fonts::mono(9));
  g.setColour(colour.withAlpha(0.7f));
  g.drawText("EQ", juce::Rectangle<float>(static_cast<float>(getWidth()) - 22.0f, yOf(0.0f) - 13.0f, 22.0f, 11.0f),
             juce::Justification::centredRight, false);
  if (on && theme::kStyle.glow > 0.0f) {
    g.setColour(colour.withAlpha(0.18f * theme::kStyle.glow));
    g.strokePath(curve_, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  g.setColour(colour.withAlpha(on ? 0.9f : 0.45f));
  g.strokePath(curve_, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  for (int i = 0; i < 3; ++i) {
    const auto c = handlePosition(i);
    const bool lit = i == hover_ || i == drag_;
    const float r = kHandleRadius + (lit ? 1.5f : 0.0f);
    g.setColour(theme::kSurface);
    g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c));
    g.setColour(lit ? theme::kWhite : colour);
    g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c), 1.6f);
  }
}

int ToneEqOverlay::handleAt(juce::Point<float> p) const {
  int best = -1;
  float bestDist = kHandleRadius + 6.0f;
  for (int i = 0; i < 3; ++i)
    if (const float d = p.getDistanceFrom(handlePosition(i)); d <= bestDist) {
      best = i;
      bestDist = d;
    }
  return best;
}

bool ToneEqOverlay::hitTest(int x, int y) {
  return drag_ >= 0 || handleAt({static_cast<float>(x), static_cast<float>(y)}) >= 0;
}

void ToneEqOverlay::showValue(int band) {
  if (band < 0) {
    tip_.hide();
    return;
  }
  const float db = gainDb(band, params_[static_cast<size_t>(band)]->normalised());
  const auto c = handlePosition(band);
  tip_.show(*this, juce::Rectangle<float>(1.0f, 1.0f).withCentre(c).expanded(kHandleRadius),
            juce::String(bands()[static_cast<size_t>(band)].name) + "  " + (db > 0.05f ? "+" : "")
                + juce::String(db, 1) + " dB");
}

void ToneEqOverlay::mouseMove(const juce::MouseEvent& e) {
  const int h = handleAt(e.position);
  if (h == hover_) return;
  hover_ = h;
  setMouseCursor(h >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
  setHelpText(h >= 0 ? juce::String(bands()[static_cast<size_t>(h)].name)
                           + ": drag up or down, like the knob. Shift+drag: fine · double-click: flat."
                     : juce::String());
  showValue(h);
  repaint();
}

void ToneEqOverlay::mouseExit(const juce::MouseEvent&) {
  if (drag_ >= 0) return;
  hover_ = -1;
  tip_.hide();
  repaint();
}

void ToneEqOverlay::mouseDown(const juce::MouseEvent& e) {
  drag_ = handleAt(e.position);
  if (drag_ < 0) return;
  dragStartY_ = e.position.y;
  dragStartDb_ = gainDb(drag_, params_[static_cast<size_t>(drag_)]->normalised());
  params_[static_cast<size_t>(drag_)]->beginGesture();
  showValue(drag_);
}

void ToneEqOverlay::mouseDrag(const juce::MouseEvent& e) {
  if (drag_ < 0) return;
  const float mid = static_cast<float>(getHeight()) / 2.0f - kHandleRadius;
  const float dbPerPx = kRangeDb / juce::jmax(1.0f, mid) / (e.mods.isShiftDown() ? kFineFactor : 1.0f);
  const float db = dragStartDb_ + (dragStartY_ - e.position.y) * dbPerPx;
  params_[static_cast<size_t>(drag_)]->dragTo(normalisedFor(drag_, db));
  if (e.mods.isShiftDown()) {  // re-anchor so toggling Shift never jumps
    dragStartY_ = e.position.y;
    dragStartDb_ = gainDb(drag_, params_[static_cast<size_t>(drag_)]->normalised());
  }
  curveFor_ = {};
  showValue(drag_);
  repaint();
}

void ToneEqOverlay::mouseUp(const juce::MouseEvent& e) {
  if (drag_ < 0) return;
  params_[static_cast<size_t>(drag_)]->endGesture();
  drag_ = -1;
  hover_ = handleAt(e.position);
  if (hover_ < 0) tip_.hide();
  repaint();
}

void ToneEqOverlay::mouseDoubleClick(const juce::MouseEvent& e) {
  const int h = handleAt(e.position);
  if (h < 0) return;
  params_[static_cast<size_t>(h)]->set(0.5f);  // 5 on the knob: flat
  curveFor_ = {};
  showValue(h);
  repaint();
}

}  // namespace t3k::ui
