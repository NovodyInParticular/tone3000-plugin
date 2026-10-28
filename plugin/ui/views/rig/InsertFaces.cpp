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

  // Footswitch: power.
  const auto fs = insertFootswitch(box);
  if (look.on) {
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

}  // namespace t3k::ui::rigface
