#include "InsertFaces.h"

#include <cmath>

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui::rigface {

namespace {

juce::Font label(float px, bool bold = false) {
  return theme::kStyle.monoLabels ? Fonts::mono(px - 1, bold) : Fonts::sans(px, bold);
}

void insertLed(juce::Graphics& g, juce::Point<float> c, bool on) {
  if (on && theme::kStyle.glow > 0.0f) {
    g.setColour(theme::kBrandYellow.withAlpha(0.25f * theme::kStyle.glow));
    g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre(c));
  }
  g.setColour(on ? theme::kBrandYellow : theme::kGray.withAlpha(0.5f));
  g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre(c));
}

void pill(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, bool hold) {
  const float corner = r.getHeight() * 0.5f;
  if (on) {
    if (theme::kStyle.glow > 0.0f) {
      g.setColour(theme::kBrandYellow.withAlpha(0.22f * theme::kStyle.glow));
      g.fillRoundedRectangle(r.expanded(2.5f), corner + 2.5f);
    }
    g.setColour(theme::kBrandYellow);
    g.fillRoundedRectangle(r, corner);
  } else {
    g.setColour(theme::kBlack.withAlpha(0.4f));
    g.fillRoundedRectangle(r, corner);
    g.setColour(theme::kBorder);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
  }
  g.setColour(on ? theme::kBlack : theme::kMuted);
  g.setFont(label(9.5f, true));
  g.drawText(text, r, juce::Justification::centred, false);
  if (hold) {
    // Momentary: a small notch on the right, like a spring-loaded switch.
    g.setColour((on ? theme::kBlack : theme::kSubtle).withAlpha(0.8f));
    g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre({r.getRight() - 6.0f, r.getCentreY()}));
  }
}

void insertSwitch(juce::Graphics& g, juce::Rectangle<float> box, bool on) {
  const auto fs = insertFootswitch(box);
  if (on) {
    if (theme::kStyle.glow > 0.0f) {
      g.setColour(theme::kBrandYellow.withAlpha(0.22f * theme::kStyle.glow));
      g.fillEllipse(fs.expanded(6.0f));
    }
    g.setColour(theme::kGray.withAlpha(0.75f));
    g.fillEllipse(fs);
    g.setColour(theme::kBrandYellow);
    g.drawEllipse(fs.reduced(0.75f), 2.0f);
  } else {
    g.setColour(theme::kGray.withAlpha(0.3f));
    g.fillEllipse(fs);
    g.setColour(theme::kBorder);
    g.drawEllipse(fs, 1.5f);
  }
}

}  // namespace

juce::Rectangle<float> glitchScreen(juce::Rectangle<float> box) {
  return {box.getX() + 8, box.getY() + 30, box.getWidth() - 16, 56};
}

juce::Rectangle<float> glitchPill(juce::Rectangle<float> box, int which) {
  const float w = (box.getWidth() - 20) * 0.5f;
  return {box.getX() + 8 + static_cast<float>(which) * (w + 4), box.getY() + 92, w, 16};
}

juce::Rectangle<float> insertFootswitch(juce::Rectangle<float> box) {
  return juce::Rectangle<float>(26, 26).withCentre({box.getCentreX(), box.getBottom() - 22});
}

void drawGlitch(juce::Graphics& g, juce::Rectangle<float> box, const GlitchLook& look) {
  insertLed(g, {box.getX() + 12, box.getY() + 14}, look.on);
  g.setColour(look.on ? theme::kWhite : theme::kGray);
  g.setFont(label(12, true));
  g.drawText("GLITCH", box.withHeight(28).withTrimmedLeft(16), juce::Justification::centred, false);

  // The screen: a wave drawn through the crusher, stepped by the Bits and
  // the rate reduction; repeats tick across it while a stutter plays, and
  // freeze frosts it over.
  const auto screen = glitchScreen(box);
  g.setColour(theme::kBlack.withAlpha(0.55f));
  g.fillRoundedRectangle(screen, 4.0f);
  {
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(screen.reduced(1.0f).toNearestInt());
    const auto pic = screen.reduced(6, 8);
    const float levels = std::exp2(juce::jlimit(1.0f, 6.0f, look.bits * 0.4f));  // how many steps show
    const int holdSteps = 2 + static_cast<int>(look.crush * 10.0f);
    const auto colour = look.on ? theme::kBrandYellow : theme::kGray.withAlpha(0.45f);
    juce::Path wave;
    const int points = 64;
    float held = 0.0f;
    for (int i = 0; i <= points; ++i) {
      const float u = static_cast<float>(i) / static_cast<float>(points);
      if (i % holdSteps == 0) {
        const float v = std::sin((u * 2.0f + look.time * 0.5f) * juce::MathConstants<float>::twoPi);
        held = std::round(v * levels * 0.5f) / (levels * 0.5f);
      }
      const float x = pic.getX() + u * pic.getWidth();
      const float y = pic.getCentreY() - held * pic.getHeight() * 0.45f;
      if (i == 0) wave.startNewSubPath(x, y);
      else {
        wave.lineTo(x, wave.getCurrentPosition().y);
        wave.lineTo(x, y);
      }
    }
    if (look.on && theme::kStyle.glow > 0.0f) {
      g.setColour(colour.withAlpha(0.2f * theme::kStyle.glow));
      g.strokePath(wave, juce::PathStrokeType(3.5f));
    }
    g.setColour(colour);
    g.strokePath(wave, juce::PathStrokeType(1.3f));
    if (look.on && look.repeating) {
      // Repeat markers: the loop's seams.
      g.setColour(theme::kWhite.withAlpha(0.35f));
      for (int k = 1; k < 4; ++k)
        g.drawVerticalLine(juce::roundToInt(pic.getX() + pic.getWidth() * static_cast<float>(k) / 4.0f), pic.getY(),
                           pic.getBottom());
    }
    if (look.freezeLevel > 0.01f) {
      // Frost: a pale wash and a few ice lines, as strong as the freeze fade.
      const float f = juce::jlimit(0.0f, 1.0f, look.freezeLevel);
      g.setColour(theme::kWhite.withAlpha(0.10f * f));
      g.fillRect(screen);
      g.setColour(theme::kWhite.withAlpha(0.45f * f));
      juce::Random rng(7);
      for (int k = 0; k < 6; ++k) {
        const juce::Point<float> c(screen.getX() + rng.nextFloat() * screen.getWidth(),
                                   screen.getY() + rng.nextFloat() * screen.getHeight());
        const float r = 4.0f + rng.nextFloat() * 6.0f;
        for (int arm = 0; arm < 3; ++arm) {
          const float a = juce::MathConstants<float>::pi / 3.0f * static_cast<float>(arm);
          g.drawLine({c.translated(-r * std::cos(a), -r * std::sin(a)), c.translated(r * std::cos(a), r * std::sin(a))},
                     0.8f);
        }
      }
    }
    if (look.on && look.eventAge < 0.25f) {
      // The glitch that just fired, named in the corner.
      static constexpr const char* kNames[] = {"", "RPT", "REV", "DROP", "OCT+", "OCT-"};
      const int e = juce::jlimit(0, 5, look.event);
      g.setColour(theme::kWhite.withAlpha(0.9f * (1.0f - look.eventAge / 0.25f)));
      g.setFont(label(9, true));
      g.drawText(kNames[e], screen.reduced(4, 2), juce::Justification::topRight, false);
    }
  }

  pill(g, glitchPill(box, 0), "STUT", look.stutter, look.stutterHold);
  pill(g, glitchPill(box, 1), "FRZ", look.freeze, look.freezeHold);

  g.setColour(theme::kSubtle);
  g.setFont(label(9));
  g.drawText(look.pre ? "PRE-AMP" : "POST-AMP", juce::Rectangle<float>(box.getX(), box.getY() + 112, box.getWidth(), 12),
             juce::Justification::centred, false);

  insertSwitch(g, box, look.on);
}

void drawAbyss(juce::Graphics& g, juce::Rectangle<float> box, const AbyssLook& look) {
  insertLed(g, {box.getX() + 12, box.getY() + 14}, look.on);
  g.setColour(look.on ? theme::kWhite : theme::kGray);
  g.setFont(label(12, true));
  g.drawText("ABYSS", box.withHeight(28).withTrimmedLeft(16), juce::Justification::centred, false);

  // The screen: one line per layer at its pitch (up high, octaves down
  // low), lit by its level and its live output; the bend moves them all,
  // detune splits each into two strands. The dry pitch is a faint guide.
  const auto screen = abyssScreen(box);
  g.setColour(theme::kBlack.withAlpha(0.55f));
  g.fillRoundedRectangle(screen, 4.0f);
  {
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(screen.reduced(1.0f).toNearestInt());
  const auto pic = screen.reduced(4, 6);
  const float perSemitone = pic.getHeight() * 0.5f / 30.0f;
  const auto yOf = [&](float semitones) { return pic.getCentreY() - semitones * perSemitone; };
  g.setColour(theme::kGray.withAlpha(0.35f));
  g.drawHorizontalLine(juce::roundToInt(yOf(0.0f)), pic.getX(), pic.getRight());

  struct Layer {
    float semitones, setting, live;
    const char* name;
  };
  const Layer layers[] = {
      {-24.0f, look.subTwo, look.activity[0], "-2"},
      {-12.0f, look.subOne, look.activity[1], "-1"},
      {12.0f, look.up, look.activity[2], "+1"},
      {look.shiftSemitones, look.shift, std::max(look.activity[3], look.activity[4]), "S"},
  };
  const auto colour = look.on ? theme::kBrandYellow : theme::kGray.withAlpha(0.45f);
  const float split = juce::jlimit(0.0f, 1.0f, look.detuneCents / 100.0f) * 4.0f;
  for (const auto& layer : layers) {
    if (layer.setting <= 0.001f) continue;
    const float y = yOf(layer.semitones + look.bend);
    const float bright = look.on ? juce::jlimit(0.2f, 1.0f, 0.3f + 0.4f * layer.setting + 1.2f * layer.live) : 0.3f;
    for (int strand = 0; strand < (split > 0.2f ? 2 : 1); ++strand) {
      const float offset = strand == 0 ? -split * 0.5f : split * 0.5f;
      juce::Path line;
      for (int i = 0; i <= 32; ++i) {
        const float u = static_cast<float>(i) / 32.0f;
        // A slow ripple, faster for higher layers.
        const float rate = 1.0f + layer.semitones / 24.0f;
        const float wobble = std::sin((u * 3.0f + look.time * rate * (strand == 0 ? 1.0f : 1.3f)) *
                                      juce::MathConstants<float>::twoPi) *
                             (0.8f + 2.0f * layer.live);
        const float x = pic.getX() + 10 + u * (pic.getWidth() - 10);
        if (i == 0) line.startNewSubPath(x, y + offset + wobble);
        else line.lineTo(x, y + offset + wobble);
      }
      if (look.on && theme::kStyle.glow > 0.0f) {
        g.setColour(colour.withAlpha(0.25f * bright * theme::kStyle.glow));
        g.strokePath(line, juce::PathStrokeType(4.0f + 3.0f * layer.live));
      }
      g.setColour(colour.withAlpha(bright));
      g.strokePath(line, juce::PathStrokeType(1.2f + layer.setting));
    }
    g.setColour(theme::kSubtle);
    g.setFont(label(8));
    g.drawText(layer.name, juce::Rectangle<float>(pic.getX(), y - 5, 12, 10), juce::Justification::centredLeft, false);
  }
  }

  g.setColour(theme::kSubtle);
  g.setFont(label(9));
  g.drawText(juce::String(look.pre ? "PRE-AMP" : "POST-AMP") + juce::String::fromUTF8(look.smooth ? " \u00b7 SMOOTH" : " \u00b7 TIGHT"),
             juce::Rectangle<float>(box.getX(), box.getY() + 108, box.getWidth(), 12), juce::Justification::centred,
             false);
  insertSwitch(g, box, look.on);
}

}  // namespace t3k::ui::rigface
