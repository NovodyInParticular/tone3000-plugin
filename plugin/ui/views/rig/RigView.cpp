#include "RigView.h"

#include <algorithm>
#include <cmath>

#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/HoverTip.h"
#include "widgets/KnobFace.h"

namespace t3k::ui {

namespace {

constexpr int kPad = 24;
constexpr int kGap = 26;          // between modules (the cable)
constexpr int kSpectrumHeight = 96;
constexpr int kGlitchMs = 220;
constexpr int kModuleHeight = 170;  // every module is drawn at this height, then scaled
// Long names: the full name floats under the module after this hover, and
// on animated skins the cut title scrolls (units per second, with a rest at
// each end).
constexpr juce::uint32 kTipDelayMs = 350;
constexpr float kMarqueeSpeed = 28.0f;
constexpr float kMarqueeRestS = 1.0f;

enum class Kind { pedal, amp, cab, fx };

Kind kindOf(const ChainItem& item) {
  if (!item.tone.isNam()) return Kind::cab;
  return item.tone.gear == "pedal" ? Kind::pedal : Kind::amp;
}

int widthOf(Kind kind) {
  switch (kind) {
    case Kind::pedal: return 104;
    case Kind::amp: return 236;
    case Kind::cab: return 150;
    case Kind::fx: return 124;
  }
  return 120;
}

juce::Font label(float px, bool bold = false) {
  return theme::kStyle.monoLabels ? Fonts::mono(px - 1, bold) : Fonts::sans(px, bold);
}

float dbToUnit(float db) { return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); }

void miniKnob(juce::Graphics& g, juce::Point<float> centre, float size, float value, const char* name) {
  const auto box = juce::Rectangle<float>(size, size).withCentre(centre);
  drawKnobFace(g, box, -135.0f + 270.0f * juce::jlimit(0.0f, 1.0f, value), -135.0f, KnobTone::secondary);
  g.setColour(theme::kSubtle);
  g.setFont(label(10));
  g.drawText(name, box.withY(box.getBottom() + 2).withHeight(12).expanded(10, 0), juce::Justification::centred,
             false);
}

void led(juce::Graphics& g, juce::Point<float> c, bool on) {
  if (on && theme::kStyle.glow > 0.0f) {
    g.setColour(theme::kBrandYellow.withAlpha(0.25f * theme::kStyle.glow));
    g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre(c));
  }
  g.setColour(on ? theme::kBrandYellow : theme::kGray.withAlpha(0.5f));
  g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre(c));
}

// The pedal's footswitch, in module coordinates.
juce::Rectangle<float> footswitchBox() {
  const auto box = juce::Rectangle<float>(static_cast<float>(widthOf(Kind::pedal)), static_cast<float>(kModuleHeight)).reduced(1.0f);
  return juce::Rectangle<float>(26, 26).withCentre({box.getCentreX(), box.getBottom() - 22});
}

void footswitch(juce::Graphics& g, bool on) {
  const auto fs = footswitchBox();
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

void grille(juce::Graphics& g, juce::Rectangle<float> area) {
  g.setColour(theme::kBlack.withAlpha(0.6f));
  g.fillRoundedRectangle(area, 4.0f);
  const auto colour = theme::kGray.withAlpha(0.45f);
  g.setColour(colour);
  if (theme::kStyle.grille == theme::Grille::hex) {
    const float r = 5.0f, w = r * std::sqrt(3.0f);
    for (float y = area.getY() + r, row = 0; y < area.getBottom() - r * 0.5f; y += r * 1.5f, ++row)
      for (float x = area.getX() + w * 0.5f + (static_cast<int>(row) % 2 ? w * 0.5f : 0.0f); x < area.getRight() - w * 0.4f;
           x += w) {
        juce::Path hex;
        for (int k = 0; k < 6; ++k) {
          const float a = juce::MathConstants<float>::pi / 3.0f * static_cast<float>(k) + juce::MathConstants<float>::pi / 6.0f;
          const juce::Point<float> p(x + (r - 1.2f) * std::cos(a), y + (r - 1.2f) * std::sin(a));
          if (k == 0) hex.startNewSubPath(p);
          else hex.lineTo(p);
        }
        hex.closeSubPath();
        g.strokePath(hex, juce::PathStrokeType(0.8f));
      }
  } else {
    for (float y = area.getY() + 5; y < area.getBottom() - 2; y += 7)
      for (float x = area.getX() + 5; x < area.getRight() - 2; x += 7) g.fillEllipse(x - 1.4f, y - 1.4f, 2.8f, 2.8f);
  }
}

}  // namespace

// One piece of gear. Repaints itself on its block's meter (pulse) and on a
// new tone (flicker).
class RigView::Module : public juce::Component, private MeterStore::Listener {
public:
  Module(Services& services, Kind kind, const ChainItem* item)
      : services_(services), kind_(kind) {
    if (item != nullptr) setItem(*item);
    else setHelpText("Time FX: modulation, delay and reverb. Right-click the Time FX group on the plate to edit.");
    setMouseCursor(item != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    services_.meters.addListener(this);
  }
  ~Module() override { services_.meters.removeListener(this); }

  void setItem(const ChainItem& item) {
    const bool newTone = item_.blockId == item.blockId && (item_.tone.id != item.tone.id || item_.activeModelId != item.activeModelId);
    item_ = item;
    bypassed_ = {};
    titleCut_ = kind_ != Kind::fx && isCut(titleText(), titleSpot());
    setHelpText(item.tone.title + ": click to open the block.");
    if (newTone && theme::kStyle.glitchOnLoad && theme::animate())
      glitchUntil_ = juce::Time::getMillisecondCounter() + kGlitchMs;
    repaint();
  }
  const std::string& blockId() const { return item_.blockId; }
  Kind kind() const { return kind_; }
  std::function<void(const std::string&)> onOpen;

  void advance() {
    hoverTick();
    if (kind_ == Kind::fx) {
      // Mirrors three parameters: repaint only when one flips.
      int bits = 0, bit = 1;
      for (const auto* id : {"fxModEnabled", "fxDelayEnabled", "fxReverbEnabled"}) {
        if (auto* p = services_.backend.parameter(id); p != nullptr && p->getValue() >= 0.5f) bits |= bit;
        bit <<= 1;
      }
      if (bits != fxBits_) {
        fxBits_ = bits;
        bypassed_ = {};
        repaint();
      }
      return;
    }
    if (glitching() || wasGlitching_) {
      wasGlitching_ = glitching();
      repaint();
      return;
    }
    if (!theme::animate() || kind_ != Kind::amp || bypassed()) return;
    phase_ += 0.18f;
    // Only the pulse trace moves.
    const float scale = static_cast<float>(getHeight()) / static_cast<float>(kModuleHeight);
    const auto scope = juce::Rectangle<float>(static_cast<float>(widthOf(kind_)), static_cast<float>(kModuleHeight))
                           .reduced(1.0f)
                           .reduced(12)
                           .removeFromBottom(30);
    repaint(scope.transformedBy(juce::AffineTransform::scale(scale)).getSmallestIntegerContainer().expanded(2));
  }

  void mouseUp(const juce::MouseEvent& e) override {
    if (kind_ == Kind::fx || !e.mouseWasClicked()) return;
    if (kind_ == Kind::pedal && overFootswitch(e.position)) {
      // Bypass in place, like a real stomp; the chain poll confirms it.
      item_.params.enabled = !item_.params.enabled;
      services_.chain.setBlockParam(item_.blockId, "enabled", item_.params.enabled);
      repaint();
      return;
    }
    if (onOpen) onOpen(item_.blockId);
  }

  void mouseMove(const juce::MouseEvent& e) override {
    const bool over = kind_ == Kind::pedal && overFootswitch(e.position);
    if (over != overSwitch_) {
      overSwitch_ = over;
      setHelpText(over ? juce::String("Footswitch: turn this pedal on or off.")
                       : item_.tone.title + ": click to open the block.");
    }
  }

  void resized() override {
    bypassed_ = {};
    tip_.hide();
  }

  void mouseEnter(const juce::MouseEvent&) override {
    hoverSince_ = juce::Time::getMillisecondCounter();
  }

  void mouseExit(const juce::MouseEvent&) override {
    hoverSince_ = 0;
    tip_.hide();
    if (marqueeX_ != 0.0f) {
      marqueeX_ = 0.0f;
      repaintTitle();
    }
  }

  void paint(juce::Graphics& g) override {
    if (bypassed()) {
      // Bypassed gear: drawn once, desaturated and dimmed, then reused
      // until the module changes (nothing on it moves).
      const float px = g.getInternalContext().getPhysicalPixelScaleFactor();
      const int w = juce::jmax(1, juce::roundToInt(static_cast<float>(getWidth()) * px));
      const int h = juce::jmax(1, juce::roundToInt(static_cast<float>(getHeight()) * px));
      if (bypassed_.isNull() || bypassed_.getWidth() != w || bypassed_.getHeight() != h) {
        bypassed_ = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Graphics ig(bypassed_);
        ig.addTransform(juce::AffineTransform::scale(px));
        draw(ig);
        bypassed_.desaturate();
        bypassed_.multiplyAllAlphas(0.55f);
      }
      g.drawImage(bypassed_, getLocalBounds().toFloat());
      return;
    }
    if (!glitching()) {
      draw(g);
      return;
    }
    // Load flicker: the module in horizontal slices, each nudged sideways.
    juce::Random rng(static_cast<juce::int64>(juce::Time::getMillisecondCounter() / 30));
    const int slices = 7;
    for (int i = 0; i < slices; ++i) {
      juce::Graphics::ScopedSaveState save(g);
      const int y0 = getHeight() * i / slices, y1 = getHeight() * (i + 1) / slices;
      g.reduceClipRegion(0, y0, getWidth(), y1 - y0);
      g.addTransform(juce::AffineTransform::translation(static_cast<float>(rng.nextInt(9) - 4), 0.0f));
      draw(g);
    }
    g.setColour(theme::kBrandYellow.withAlpha(0.12f));
    g.fillRect(getLocalBounds());
  }

private:
  struct TitleSpot {
    juce::Rectangle<float> area;  // nominal module units
    int lines = 2;
  };

  TitleSpot titleSpot() const {
    const auto box = juce::Rectangle<float>(static_cast<float>(widthOf(kind_)), static_cast<float>(kModuleHeight)).reduced(1.0f);
    switch (kind_) {
      case Kind::pedal: return {box.withTrimmedTop(74).withHeight(34).reduced(6, 0), 2};
      case Kind::amp: return {box.reduced(10).removeFromTop(34).reduced(6, 2), 2};
      case Kind::cab: return {box.reduced(8).removeFromBottom(26), 1};
      case Kind::fx: break;
    }
    return {};
  }

  juce::String titleText() const {
    return item_.tone.title.isNotEmpty() ? item_.tone.title : juce::String("Untitled");
  }

  // Whether the title, fitted into `spot` as title() draws it, loses
  // characters to an ellipsis.
  static bool isCut(const juce::String& text, const TitleSpot& spot) {
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(label(12, true), text, spot.area.getX(), spot.area.getY(), spot.area.getWidth(),
                         spot.area.getHeight(), juce::Justification::centred, spot.lines, 0.8f);
    const int count = glyphs.getNumGlyphs();
    if (count == 0) return false;
    int shown = 0, wanted = 0;
    for (int i = 0; i < count; ++i)
      if (!glyphs.getGlyph(i).isWhitespace()) ++shown;
    for (auto c : text)
      if (!juce::CharacterFunctions::isWhitespace(c)) ++wanted;
    // Characters missing, or an ellipsis the name itself doesn't end with
    // (a name cut by one letter gains three dots and loses one).
    const auto last = glyphs.getGlyph(count - 1).getCharacter();
    const bool endsInDots = last == 0x2026 || last == '.';
    return shown < wanted || (endsInDots && !text.trimEnd().endsWithChar(last));
  }

  float scale() const { return static_cast<float>(getHeight()) / static_cast<float>(kModuleHeight); }

  void repaintTitle() {
    repaint(titleSpot().area.transformedBy(juce::AffineTransform::scale(scale())).getSmallestIntegerContainer().expanded(2));
  }

  // Hover on a cut title: the tooltip after a moment, and the marquee on
  // skins that animate (rests at each end, then runs back).
  void hoverTick() {
    if (!titleCut_ || hoverSince_ == 0) return;
    const auto now = juce::Time::getMillisecondCounter();
    const auto spot = titleSpot();
    if (!tip_.showing() && now - hoverSince_ >= kTipDelayMs)
      tip_.show(*this, spot.area.transformedBy(juce::AffineTransform::scale(scale())), titleText());
    if (!theme::animate() || bypassed()) return;
    const float travel = juce::jmax(0.0f, Fonts::width(label(12, true), titleText()) - spot.area.getWidth() + 8.0f);
    const float runS = travel / kMarqueeSpeed;
    const float cycle = 2.0f * (runS + kMarqueeRestS);
    float t = std::fmod(static_cast<float>(now - hoverSince_) / 1000.0f, cycle);
    float x = 0.0f;
    if (t < kMarqueeRestS) x = 0.0f;
    else if ((t -= kMarqueeRestS) < runS) x = t * kMarqueeSpeed;
    else if ((t -= runS) < kMarqueeRestS) x = travel;
    else x = travel - (t - kMarqueeRestS) * kMarqueeSpeed;
    if (!juce::approximatelyEqual(x, marqueeX_)) {
      marqueeX_ = x;
      repaintTitle();
    }
  }

  bool bypassed() const { return kind_ == Kind::fx ? fxBits_ == 0 : !item_.params.enabled; }
  bool overFootswitch(juce::Point<float> p) const {
    const float scale = static_cast<float>(getHeight()) / static_cast<float>(kModuleHeight);
    return footswitchBox().expanded(4.0f).contains(p / scale);
  }

  bool glitching() const { return juce::Time::getMillisecondCounter() < glitchUntil_; }

  void meterChanged(const juce::String& id) override {
    if (kind_ == Kind::fx || bypassed() || id != MeterStore::blockOutId(item_.blockId)) return;
    if (kind_ == Kind::amp && theme::animate()) return;  // the tick repaints amps
    repaint();
  }

  float outLevel() const { return dbToUnit(services_.meters.level(MeterStore::blockOutId(item_.blockId))); }

  void body(juce::Graphics& g, juce::Rectangle<float> box, bool lit) {
    g.setColour(theme::kSurfaceRaised);
    g.fillRoundedRectangle(box, 10.0f);
    if (lit && theme::kStyle.glow > 0.0f) {
      g.setColour(theme::kBrandYellow.withAlpha(0.20f * theme::kStyle.glow));
      g.drawRoundedRectangle(box.expanded(1.5f), 11.5f, 3.0f);
    }
    g.setColour(lit ? theme::kBrandYellow.withAlpha(0.65f) : theme::kBorder);
    g.drawRoundedRectangle(box.reduced(0.5f), 10.0f, 1.0f);
  }

  void title(juce::Graphics& g) {
    const auto spot = titleSpot();
    g.setColour(item_.params.enabled ? theme::kWhite : theme::kGray);
    g.setFont(label(12, true));
    if (hoverSince_ != 0 && titleCut_ && theme::animate() && !bypassed()) {
      // Marquee: one line, slid left inside the title area.
      juce::Graphics::ScopedSaveState save(g);
      g.reduceClipRegion(spot.area.toNearestInt());
      const float w = Fonts::width(label(12, true), titleText()) + 2.0f;
      g.drawText(titleText(), juce::Rectangle<float>(spot.area.getX() + 4.0f - marqueeX_, spot.area.getY(), w, spot.area.getHeight()),
                 juce::Justification::centredLeft, false);
      return;
    }
    g.drawFittedText(titleText(), spot.area.toNearestInt(), juce::Justification::centred, spot.lines, 0.8f);
  }

  void draw(juce::Graphics& g) {
    // Drawn at the nominal size and scaled uniformly, so a long or stereo
    // chain shrinks the gear instead of crowding it.
    const float scale = static_cast<float>(getHeight()) / static_cast<float>(kModuleHeight);
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(scale));
    const auto box = juce::Rectangle<float>(static_cast<float>(widthOf(kind_)), static_cast<float>(kModuleHeight)).reduced(1.0f);
    const bool enabled = !bypassed();
    switch (kind_) {
      case Kind::pedal: {
        body(g, box, enabled && item_.loaded);
        led(g, {box.getCentreX(), box.getY() + 14}, enabled);
        miniKnob(g, {box.getX() + box.getWidth() * 0.3f, box.getY() + 42}, 26, static_cast<float>(item_.params.inputGain), "IN");
        miniKnob(g, {box.getX() + box.getWidth() * 0.7f, box.getY() + 42}, 26, static_cast<float>(item_.params.outputGain), "OUT");
        title(g);
        footswitch(g, enabled);
        break;
      }
      case Kind::amp: {
        body(g, box, enabled && item_.loaded);
        // Name plate.
        auto plate = box.reduced(10).removeFromTop(34);
        g.setColour(theme::kBlack.withAlpha(0.55f));
        g.fillRoundedRectangle(plate, 5.0f);
        title(g);
        // Knobs.
        const float ky = box.getY() + 74;
        miniKnob(g, {box.getX() + box.getWidth() * 0.22f, ky}, 30, static_cast<float>(item_.params.inputGain), "IN");
        miniKnob(g, {box.getX() + box.getWidth() * 0.50f, ky}, 30, static_cast<float>(item_.params.outputGain), "OUT");
        miniKnob(g, {box.getX() + box.getWidth() * 0.78f, ky}, 30, static_cast<float>(item_.params.mix), "MIX");
        // Pulse: a trace whose swing follows the block's output level.
        auto scope = box.reduced(12).removeFromBottom(30);
        g.setColour(theme::kBlack.withAlpha(0.5f));
        g.fillRoundedRectangle(scope, 4.0f);
        const float amp = enabled ? outLevel() : 0.0f;
        juce::Path trace;
        for (int i = 0; i <= 60; ++i) {
          const float t = static_cast<float>(i) / 60.0f;
          const float y = scope.getCentreY() -
                          amp * (scope.getHeight() * 0.42f) * std::sin(t * 13.0f + phase_) * std::sin(t * 3.14159f);
          const float x = scope.getX() + 4 + t * (scope.getWidth() - 8);
          if (i == 0) trace.startNewSubPath(x, y);
          else trace.lineTo(x, y);
        }
        if (theme::kStyle.glow > 0.0f) {
          g.setColour(theme::kBrandYellow.withAlpha(0.2f * theme::kStyle.glow));
          g.strokePath(trace, juce::PathStrokeType(4.0f));
        }
        g.setColour(theme::kBrandYellow.withAlpha(0.4f + 0.6f * amp));
        g.strokePath(trace, juce::PathStrokeType(1.4f));
        led(g, {box.getRight() - 14, box.getY() + 14}, enabled);
        break;
      }
      case Kind::cab: {
        body(g, box, enabled && item_.loaded);
        grille(g, box.reduced(10).withTrimmedBottom(32));
        title(g);
        if (item_.dualIr && item_.dualIr->hasB) {
          g.setColour(theme::kBrandYellow);
          g.setFont(label(10, true));
          g.drawText("2x IR", box.reduced(14).removeFromTop(14), juce::Justification::topRight, false);
        }
        break;
      }
      case Kind::fx: {
        const auto on = [this](const char* id) {
          auto* p = services_.backend.parameter(id);
          return p != nullptr && p->getValue() >= 0.5f;
        };
        const bool any = on("fxModEnabled") || on("fxDelayEnabled") || on("fxReverbEnabled");
        body(g, box, any);
        g.setColour(theme::kWhite);
        g.setFont(label(12, true));
        g.drawText("TIME FX", box.withHeight(30), juce::Justification::centred, false);
        float y = box.getY() + 46;
        for (const auto& [id, name] : {std::pair{"fxModEnabled", "MOD"}, std::pair{"fxDelayEnabled", "DLY"},
                                       std::pair{"fxReverbEnabled", "REV"}}) {
          led(g, {box.getX() + 24, y}, on(id));
          g.setColour(on(id) ? theme::kWhite : theme::kGray);
          g.setFont(label(12));
          g.drawText(name, juce::Rectangle<float>(box.getX() + 36, y - 8, 60, 16), juce::Justification::centredLeft,
                     false);
          y += 26;
        }
        break;
      }
    }
  }

  Services& services_;
  Kind kind_;
  ChainItem item_;
  float phase_ = 0.0f;
  juce::uint32 glitchUntil_ = 0;
  bool wasGlitching_ = false;
  int fxBits_ = -1;
  bool overSwitch_ = false;
  juce::Image bypassed_;  // the desaturated bypass look, cached
  bool titleCut_ = false;          // the title doesn't fit its spot
  juce::uint32 hoverSince_ = 0;    // 0: not hovered
  float marqueeX_ = 0.0f;          // marquee offset, nominal units
  HoverTip tip_;
};

RigView::RigView(Services& services) : services_(services), spectrum_(services.backend, services.clock) {
  addAndMakeVisible(spectrum_);
  spectrum_.setRate(services_.prefs.get(UiPrefs::kSpectrumRate, "normal"));
  services_.chain.addListener(this);
  services_.prefs.addListener(this);
  services_.clock.addListener(this);
  rebuild();
}

RigView::~RigView() {
  services_.clock.removeListener(this);
  services_.prefs.removeListener(this);
  services_.chain.removeListener(this);
}

void RigView::prefChanged(const juce::String& key) {
  if (key == UiPrefs::kSpectrumRate) {
    spectrum_.setRate(services_.prefs.get(UiPrefs::kSpectrumRate, "normal"));
    resized();
  }
}

void RigView::tick() {
  for (auto& m : modules_) m->advance();
  if (fx_ != nullptr) fx_->advance();
}

void RigView::chainChanged(const ChainState&) { rebuild(); }

void RigView::rebuild() {
  const auto& state = services_.chain.state();
  // Keep modules whose block is still there (so the flicker can compare
  // tones), in the chain's new order.
  std::vector<std::unique_ptr<Module>> next;
  const auto take = [&](const ChainItem& item, int row) {
    if (item.isInsert) return;
    std::unique_ptr<Module> m;
    for (auto& old : modules_)
      if (old != nullptr && old->blockId() == item.blockId && old->kind() == kindOf(item)) {
        m = std::move(old);
        break;
      }
    if (m == nullptr) {
      m = std::make_unique<Module>(services_, kindOf(item), &item);
      m->onOpen = [this](const std::string& id) {
        if (onOpenBlock) onOpenBlock(id);
      };
      addAndMakeVisible(*m);
    }
    m->setItem(item);
    m->getProperties().set("row", row);
    next.push_back(std::move(m));
  };
  for (const auto& item : state.chain) take(item, 0);
  stereo_ = state.stereoEnabled && state.chainRight.has_value();
  if (stereo_)
    for (const auto& item : *state.chainRight) take(item, 1);
  for (auto& old : modules_)
    if (old != nullptr) removeChildComponent(old.get());
  modules_ = std::move(next);
  if (fx_ == nullptr) {
    fx_ = std::make_unique<Module>(services_, Kind::fx, nullptr);
    addAndMakeVisible(*fx_);
  }
  rowCount_ = stereo_ ? 2 : 1;
  resized();
  repaint();
}

void RigView::resized() {
  auto area = getLocalBounds().reduced(kPad, 12);
  const bool spectrumShown = services_.prefs.get(UiPrefs::kSpectrumRate, "normal") != "off";
  spectrum_.setVisible(spectrumShown);
  if (spectrumShown) {
    spectrum_.setBounds(area.removeFromBottom(kSpectrumHeight));
    area.removeFromBottom(14);
  }
  // Rows of modules, then the FX unit at the end of the (first) row.
  const int rowH = juce::jmin(170, (area.getHeight() - (rowCount_ - 1) * 14) / rowCount_);
  for (int row = 0; row < rowCount_; ++row) {
    int total = 0;
    std::vector<Module*> inRow;
    for (auto& m : modules_)
      if (static_cast<int>(m->getProperties()["row"]) == row) {
        inRow.push_back(m.get());
        total += widthOf(m->kind()) + kGap;
      }
    if (row == 0) total += widthOf(Kind::fx);
    else total -= kGap;
    // Shrink everything to fit a long chain or a second row.
    const float scale = std::min({1.0f, static_cast<float>(area.getWidth()) / static_cast<float>(juce::jmax(1, total)),
                                    static_cast<float>(rowH) / static_cast<float>(kModuleHeight)});
    const int h = juce::roundToInt(kModuleHeight * scale);
    const int y = area.getY() + row * (rowH + 14) + (rowH - h) / 2;
    float x = area.getX() + (area.getWidth() - total * scale) / 2.0f;
    for (auto* m : inRow) {
      const int w = juce::roundToInt(widthOf(m->kind()) * scale);
      m->setBounds(juce::roundToInt(x), y, w, h);
      x += (widthOf(m->kind()) + kGap) * scale;
    }
    if (row == 0)
      fx_->setBounds(juce::roundToInt(x), y, juce::roundToInt(widthOf(Kind::fx) * scale), h);
  }
}

void RigView::paint(juce::Graphics& g) {
  // Cables between neighbouring modules (and into the FX unit).
  const auto cable = [&](juce::Rectangle<int> a, juce::Rectangle<int> b) {
    const float y = static_cast<float>(a.getCentreY());
    const juce::Line<float> line(static_cast<float>(a.getRight()), y, static_cast<float>(b.getX()), y);
    if (theme::kStyle.glow > 0.0f) {
      g.setColour(theme::kBrandYellow.withAlpha(0.15f * theme::kStyle.glow));
      g.drawLine(line, 5.0f);
    }
    g.setColour(theme::kBrandYellow.withAlpha(0.7f));
    g.drawLine(line, 1.5f);
  };
  for (int row = 0; row < rowCount_; ++row) {
    Module* prev = nullptr;
    for (auto& m : modules_) {
      if (static_cast<int>(m->getProperties()["row"]) != row) continue;
      if (prev != nullptr) cable(prev->getBounds(), m->getBounds());
      prev = m.get();
    }
    if (row == 0 && prev != nullptr && fx_ != nullptr) cable(prev->getBounds(), fx_->getBounds());
  }
  if (modules_.empty()) {
    g.setColour(theme::kSubtle);
    g.setFont(label(14));
    g.drawText("No tones yet. Switch to the gallery to add some.", getLocalBounds().withTrimmedBottom(kSpectrumHeight),
               juce::Justification::centred, false);
  }
}

}  // namespace t3k::ui
