#include "RigTuner.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Pitch.h"
#include "core/Theme.h"

namespace t3k::ui {

RigTuner::RigTuner(Services& services) : feed_(services.backend, services.clock) {
  feed_.onChange = [this] { repaint(); };
  close_.setHelpText("Close the tuner.");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(close_);
  setHelpText("Rig tuner: play a single open string.");
}

void RigTuner::resized() { close_.setTopRightPosition(getWidth() - 6, 6); }

void RigTuner::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  paint::fill(g, box, 8.0f, theme::kSurface.withAlpha(0.92f));
  paint::border(g, box, 8.0f, theme::kBorder);
  const auto& s = feed_.state();
  const bool inTune = s.hasSignal && std::abs(s.cents) <= pitch::kInTuneCents;
  const float glow = theme::kStyle.glow;
  const auto mono = [](float px, bool bold = false) {
    return theme::kStyle.monoLabels ? Fonts::mono(px - 1, bold) : Fonts::sans(px, bold);
  };
  auto inner = box.reduced(14.0f, 8.0f);
  g.setFont(mono(11));
  g.setColour(theme::kSubtle);
  g.drawText("TUNER", inner.removeFromTop(14.0f), juce::Justification::topLeft, false);

  // The cents scale along the bottom: ticks every 10, the centre tall.
  auto scale = inner.removeFromBottom(26.0f);
  const float mid = scale.getCentreX();
  const float half = scale.getWidth() / 2.0f - 8.0f;
  const float baseY = scale.getY() + 14.0f;
  for (int c = -50; c <= 50; c += 10) {
    const float x = mid + half * static_cast<float>(c) / pitch::kMaxCents;
    const float h = c == 0 ? 12.0f : 6.0f;
    g.setColour(c == 0 ? theme::kWhite.withAlpha(0.7f) : theme::kGray.withAlpha(0.6f));
    g.drawVerticalLine(juce::roundToInt(x), baseY - h, baseY);
  }
  g.setFont(mono(10));
  g.setColour(theme::kSubtle);
  g.drawText("-50", juce::Rectangle<float>(mid - half - 20, baseY + 1, 40, 11), juce::Justification::centred, false);
  g.drawText("+50", juce::Rectangle<float>(mid + half - 20, baseY + 1, 40, 11), juce::Justification::centred, false);
  // The in-tune window.
  const float window = half * pitch::kInTuneCents / pitch::kMaxCents;
  g.setColour((inTune ? theme::kBrandYellow : theme::kGray).withAlpha(inTune ? 0.35f : 0.15f));
  g.fillRect(juce::Rectangle<float>(mid - window, baseY - 12, 2 * window, 12));

  // The needle: a short marker on the scale with a head pointing down at it,
  // kept below the readout.
  if (s.hasSignal) {
    const float x = mid + half * juce::jlimit(-1.0f, 1.0f, s.cents / pitch::kMaxCents);
    const auto colour = inTune ? theme::kBrandYellow : theme::kWhite;
    const juce::Line<float> needle(x, baseY + 2.0f, x, baseY - 18.0f);
    juce::Path head;
    head.addTriangle(x - 5.0f, baseY - 24.0f, x + 5.0f, baseY - 24.0f, x, baseY - 17.0f);
    if (glow > 0.0f) {
      g.setColour(colour.withAlpha((inTune ? 0.35f : 0.15f) * glow));
      g.drawLine(needle, 7.0f);
    }
    g.setColour(colour.withAlpha(inTune ? 1.0f : 0.85f));
    g.drawLine(needle, 2.0f);
    g.fillPath(head);
  }

  // The note, big, over the scale's centre; frequency and cents beside it.
  const auto readout = inner.withTrimmedBottom(12.0f);
  if (!s.hasSignal) {
    g.setFont(mono(13));
    g.setColour(theme::kSubtle);
    g.drawText("Play a string", readout, juce::Justification::centred, false);
    return;
  }
  const float notePx = juce::jmin(60.0f, readout.getHeight());
  const auto noteBox = juce::Rectangle<float>(120.0f, readout.getHeight()).withCentre(readout.getCentre());
  const auto noteColour = inTune ? theme::kBrandYellow : theme::kWhite;
  g.setFont(mono(notePx, true));
  if (inTune && glow > 0.0f) {
    // A cheap halo: the note drawn a few times around itself, faint.
    g.setColour(noteColour.withAlpha(0.18f * glow));
    for (const auto& [dx, dy] : {std::pair{-2, 0}, std::pair{2, 0}, std::pair{0, -2}, std::pair{0, 2}})
      g.drawText(s.note, noteBox.translated(static_cast<float>(dx), static_cast<float>(dy)), juce::Justification::centred, false);
  }
  g.setColour(noteColour);
  g.drawText(s.note, noteBox, juce::Justification::centred, false);
  const int cents = juce::roundToInt(s.cents);
  g.setFont(mono(12));
  g.setColour(theme::kMuted);
  const auto side = juce::Rectangle<float>(noteBox.getRight() + 4.0f, noteBox.getCentreY() - 16.0f, 110.0f, 32.0f);
  g.drawFittedText(juce::String(s.frequency, 1) + " Hz\n" + (cents > 0 ? "+" : "") + juce::String(cents) + " cents",
                   side.toNearestInt(), juce::Justification::centredLeft, 2);
}

}  // namespace t3k::ui
