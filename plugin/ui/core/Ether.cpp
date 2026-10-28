#include "Ether.h"

#include <array>
#include <atomic>
#include <cmath>

#include "Theme.h"

#if JUCE_MAC
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#if JUCE_WINDOWS
// From user32 (always linked by JUCE); declared here so this file doesn't
// pull in <windows.h>.
extern "C" __declspec(dllimport) int __stdcall SystemParametersInfoW(unsigned int, unsigned int, void*, unsigned int);
#endif

namespace t3k::ui::ether {

namespace {

constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
constexpr double kHueCycleSeconds = 60.0;  // one full turn of the palette
constexpr double kFrameBudgetMs = 20.0;    // a 30 fps frame's painting budget, with headroom
constexpr int kOverBudgetFrames = 60;      // ~2 s at 30 fps
// The backdrop and the cards' slow art (borders, mandalas, kaleidoscopes)
// move at this rate; the drift is slow enough that it reads as smooth.
constexpr double kBackdropFps = 10.0;

// Message-thread state (the UI is single-threaded).
float gTrip = 0.5f;
double gClock = 0.0;
float gEnergy = 0.0f;
double gFrameEma = 0.0;
int gOverBudget = 0;
bool gDegraded = false;

// The accent wheel: magenta, electric violet, turquoise, acid green,
// tangerine, hot pink (and back to magenta).
const std::array<juce::Colour, 6> kWheel = {juce::Colour(0xffff2bd6), juce::Colour(0xff8a5cff),
                                            juce::Colour(0xff2ee6d6), juce::Colour(0xff9dff4a),
                                            juce::Colour(0xffff9a3c), juce::Colour(0xffff4f9a)};

float t() { return static_cast<float>(gClock); }

// Deterministic pseudo-random in 0..1 for artwork layouts.
float hash01(int n) {
  auto x = static_cast<juce::uint32>(n) * 2654435761u;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  return static_cast<float>(x & 0xffffff) / 16777215.0f;
}

// Energy-driven boost (glow / saturation), scaled by Trip Intensity.
float boost() { return 1.0f + 0.6f * gEnergy * gTrip; }

bool readSystemReducedMotion() {
#if JUCE_MAC
  // [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion]
  auto* cls = objc_getClass("NSWorkspace");
  if (cls == nullptr) return false;
  using Send = id (*)(id, SEL);
  using SendBool = BOOL (*)(id, SEL);
  id ws = reinterpret_cast<Send>(objc_msgSend)(reinterpret_cast<id>(cls), sel_registerName("sharedWorkspace"));
  const SEL ask = sel_registerName("accessibilityDisplayShouldReduceMotion");
  if (ws == nullptr || !class_respondsToSelector(object_getClass(ws), ask)) return false;
  return reinterpret_cast<SendBool>(objc_msgSend)(ws, ask) != NO;
#elif JUCE_WINDOWS
  int animations = 1;
  constexpr unsigned int kGetClientAreaAnimation = 0x1042;  // SPI_GETCLIENTAREAANIMATION
  if (SystemParametersInfoW(kGetClientAreaAnimation, 0, &animations, 0) != 0) return animations == 0;
  return false;
#elif JUCE_LINUX || JUCE_BSD
  // GTK's setting file, then GNOME's key (only if gsettings is installed).
  for (const auto* rel : {".config/gtk-4.0/settings.ini", ".config/gtk-3.0/settings.ini"}) {
    const auto file = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(rel);
    if (!file.existsAsFile()) continue;
    for (const auto& line : juce::StringArray::fromLines(file.loadFileAsString())) {
      const auto l = line.removeCharacters(" \t").toLowerCase();
      if (l.startsWith("gtk-enable-animations=")) return l.endsWith("=0") || l.endsWith("=false");
    }
  }
  for (const auto* bin : {"/usr/bin/gsettings", "/bin/gsettings"}) {
    if (!juce::File(bin).existsAsFile()) continue;
    juce::ChildProcess p;
    if (p.start(juce::StringArray{bin, "get", "org.gnome.desktop.interface", "enable-animations"},
                juce::ChildProcess::wantStdOut) &&
        p.waitForProcessToFinish(400))
      return p.readAllProcessOutput().trim() == "false";
    break;
  }
  return false;
#else
  return false;
#endif
}

void stars(juce::Graphics& g, juce::Rectangle<float> area) {
  // Fixed positions, slow twinkle (well under 1 Hz).
  const int count = juce::jlimit(40, 220, static_cast<int>(area.getWidth() * area.getHeight() / 6000.0f));
  for (int i = 0; i < count; ++i) {
    const float x = area.getX() + hash01(i * 3 + 1) * area.getWidth();
    const float y = area.getY() + hash01(i * 3 + 2) * area.getHeight();
    const float tw = 0.55f + 0.45f * std::sin(t() * (0.3f + hash01(i) * 0.5f) + static_cast<float>(i));
    const float r = 0.5f + hash01(i * 7) * 0.9f;
    g.setColour(juce::Colours::white.withAlpha(0.18f + 0.35f * tw * hash01(i * 11)));
    g.fillEllipse(x - r, y - r, 2 * r, 2 * r);
  }
}

}  // namespace

bool active() { return theme::kStyle.psychedelic; }
float trip() { return gTrip; }
void setTrip(float value) { gTrip = juce::jlimit(0.0f, 1.0f, value); }

bool systemReducedMotion() {
  static const bool reduced = readSystemReducedMotion();
  return reduced;
}

bool motion() {
  return active() && theme::animate() && !systemReducedMotion() && gTrip > 0.001f && !gDegraded;
}

double clock() { return motion() ? gClock : 0.0; }

void advance(double seconds) {
  if (!motion()) return;
  // Slower at low trip; the music nudges it along a little.
  gClock += seconds * (0.35 + 0.65 * gTrip) * (1.0 + 0.3 * gEnergy * gTrip);
}

void setEnergy(float level01) {
  const float target = juce::jlimit(0.0f, 1.0f, level01);
  // Slow both ways: a breath, not a flicker.
  gEnergy += (target - gEnergy) * (target > gEnergy ? 0.15f : 0.05f);
}
float energy() { return motion() ? gEnergy : 0.0f; }

void reportFrameMs(double ms) {
  if (!motion()) return;
  gFrameEma = gFrameEma * 0.9 + ms * 0.1;
  gOverBudget = gFrameEma > kFrameBudgetMs ? gOverBudget + 1 : 0;
  if (gOverBudget > kOverBudgetFrames) gDegraded = true;
}
bool degraded() { return gDegraded; }
void resetWatchdog() {
  gDegraded = false;
  gOverBudget = 0;
  gFrameEma = 0.0;
}

juce::Colour base() { return juce::Colour(0xff120726); }

juce::Colour palette(float pos, float alpha) {
  const float drift = static_cast<float>(clock() / kHueCycleSeconds);
  float u = std::fmod(pos + drift, 1.0f);
  if (u < 0.0f) u += 1.0f;
  const float scaled = u * static_cast<float>(kWheel.size());
  const int i = static_cast<int>(scaled) % static_cast<int>(kWheel.size());
  const auto next = kWheel[static_cast<size_t>((i + 1) % static_cast<int>(kWheel.size()))];
  return kWheel[static_cast<size_t>(i)].interpolatedWith(next, scaled - std::floor(scaled)).withAlpha(alpha);
}

bool FlashGate::allow() {
  const auto now = juce::Time::getMillisecondCounter();
  if (last_ != 0 && now - last_ < static_cast<juce::uint32>(kMinGapMs)) return false;
  last_ = now;
  return true;
}

void frosted(juce::Graphics& g, juce::Rectangle<float> r, float corner, float strength) {
  g.setColour(base().darker(0.4f).withAlpha(0.66f * strength));
  g.fillRoundedRectangle(r, corner);
  g.setGradientFill(juce::ColourGradient::vertical(juce::Colours::white.withAlpha(0.07f * strength), r.getY(),
                                                   juce::Colours::white.withAlpha(0.0f), r.getCentreY()));
  g.fillRoundedRectangle(r, corner);
  g.setColour(juce::Colours::white.withAlpha(0.10f * strength));
  g.drawRoundedRectangle(r.reduced(0.5f), corner, 0.8f);
}

void liquidCard(juce::Graphics& g, juce::Rectangle<float> r, float corner, bool lit, float seed) {
  const float c = corner * 1.6f;
  g.setGradientFill(juce::ColourGradient::vertical(juce::Colour(0xf01d0f3f), r.getY(), juce::Colour(0xf00c0522),
                                                   r.getBottom()));
  g.fillRoundedRectangle(r, c);
  // A soft wash of colour pooled in one corner, drifting.
  const float a = t() * 0.25f + seed * kTwoPi;
  const juce::Point<float> pool(r.getCentreX() + std::cos(a) * r.getWidth() * 0.3f,
                                r.getCentreY() + std::sin(a * 0.7f) * r.getHeight() * 0.3f);
  juce::ColourGradient wash(palette(seed, (lit ? 0.22f : 0.08f) * boost()), pool, palette(seed, 0.0f),
                            pool.translated(r.getWidth() * 0.6f, 0.0f), true);
  g.setGradientFill(wash);
  g.fillRoundedRectangle(r, c);
  // The swirling border: a gradient whose direction turns with time.
  const float turn = t() * 0.4f + seed * kTwoPi;
  const auto centre = r.getCentre();
  const float reach = std::max(r.getWidth(), r.getHeight()) * 0.6f;
  juce::ColourGradient border(palette(seed), centre.translated(std::cos(turn) * reach, std::sin(turn) * reach),
                              palette(seed + 0.5f), centre.translated(-std::cos(turn) * reach, -std::sin(turn) * reach),
                              false);
  border.addColour(0.25, palette(seed + 0.17f));
  border.addColour(0.5, palette(seed + 0.33f));
  border.addColour(0.75, palette(seed + 0.67f));
  const float alpha = lit ? 1.0f : 0.45f;
  if (lit) {
    g.setGradientFill(border);
    g.setOpacity(0.25f * std::min(1.0f, boost()));
    g.drawRoundedRectangle(r.expanded(1.5f), c + 1.5f, 4.0f);
  }
  g.setGradientFill(border);
  g.setOpacity(alpha);
  g.drawRoundedRectangle(r.reduced(0.75f), c, 1.6f);
  g.setOpacity(1.0f);
}

void lavaSwitch(juce::Graphics& g, juce::Rectangle<float> r, bool on, float seed) {
  if (!on) {
    g.setColour(base().brighter(0.15f));
    g.fillEllipse(r);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawEllipse(r, 1.2f);
    return;
  }
  const auto c = r.getCentre();
  // Halo.
  juce::ColourGradient halo(palette(seed + 0.1f, 0.35f * boost()), c, palette(seed + 0.1f, 0.0f),
                            c.translated(r.getWidth() * 0.95f, 0.0f), true);
  g.setGradientFill(halo);
  g.fillEllipse(r.expanded(r.getWidth() * 0.45f));
  // The bubble: a hot core that wanders inside a glassy shell.
  const float wob = r.getWidth() * 0.12f;
  const juce::Point<float> core(c.x + std::sin(t() * 0.9f + seed * 6.0f) * wob,
                                c.y + std::cos(t() * 0.7f + seed * 4.0f) * wob);
  juce::ColourGradient fill(palette(seed + 0.05f).brighter(0.3f), core, palette(seed + 0.3f).darker(0.4f),
                            core.translated(r.getWidth() * 0.7f, 0.0f), true);
  g.setGradientFill(fill);
  g.fillEllipse(r);
  // Two little blobs rising inside.
  juce::Graphics::ScopedSaveState save(g);
  juce::Path shell;
  shell.addEllipse(r);
  g.reduceClipRegion(shell);
  for (int i = 0; i < 2; ++i) {
    const float rise = std::fmod(t() * 0.12f + static_cast<float>(i) * 0.5f + seed, 1.0f);
    const float bx = r.getX() + r.getWidth() * (0.3f + 0.4f * hash01(i + 5));
    const float by = r.getBottom() - rise * r.getHeight() * 1.2f;
    const float br = r.getWidth() * (0.12f + 0.06f * static_cast<float>(i));
    g.setColour(palette(seed + 0.5f + 0.1f * static_cast<float>(i), 0.65f));
    g.fillEllipse(bx - br, by - br, 2 * br, 2 * br);
  }
  g.setColour(juce::Colours::white.withAlpha(0.35f));
  g.fillEllipse(r.getX() + r.getWidth() * 0.25f, r.getY() + r.getHeight() * 0.15f, r.getWidth() * 0.22f,
                r.getHeight() * 0.14f);
}

void mandala(juce::Graphics& g, juce::Rectangle<float> r, float level) {
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(r.toNearestInt());
  const auto c = r.getCentre();
  const float reach = std::hypot(r.getWidth(), r.getHeight()) * 0.55f;
  const float spin = t() * 0.08f;
  const int rays = 24;
  for (int i = 0; i < rays; ++i) {
    const float a0 = spin + kTwoPi * static_cast<float>(i) / rays;
    const float a1 = a0 + kTwoPi / rays * 0.55f;
    juce::Path ray;
    ray.startNewSubPath(c);
    ray.lineTo(c.x + std::cos(a0) * reach, c.y + std::sin(a0) * reach);
    ray.lineTo(c.x + std::cos(a1) * reach, c.y + std::sin(a1) * reach);
    ray.closeSubPath();
    g.setColour(palette(static_cast<float>(i % 6) / 6.0f, (0.10f + 0.12f * level) * boost()));
    g.fillPath(ray);
  }
  for (int ring = 1; ring <= 3; ++ring) {
    const float rr = reach * 0.22f * static_cast<float>(ring);
    for (int k = 0; k < 12; ++k) {
      const float a = -spin * 1.5f + kTwoPi * static_cast<float>(k) / 12.0f + static_cast<float>(ring) * 0.26f;
      const juce::Point<float> p(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr * 0.6f);
      g.setColour(palette(0.15f * static_cast<float>(ring) + static_cast<float>(k) / 24.0f, 0.22f));
      g.fillEllipse(juce::Rectangle<float>(rr * 0.35f, rr * 0.2f).withCentre(p));
    }
  }
}

void rainbowStroke(juce::Graphics& g, const juce::Path& path, float width, float phase, float alpha) {
  // One stroke with a rainbow gradient along the path's longer side: the
  // ribbons, traces and waves this draws all run mostly one way.
  const auto b = path.getBounds();
  if (b.isEmpty() && b.getWidth() <= 0.0f && b.getHeight() <= 0.0f) return;
  const bool across = b.getWidth() >= b.getHeight();
  juce::ColourGradient grad(palette(phase, alpha), b.getX(), b.getY(), palette(phase + 0.999f, alpha),
                            across ? b.getRight() : b.getX(), across ? b.getY() : b.getBottom(), false);
  for (int i = 1; i < 6; ++i) grad.addColour(i / 6.0, palette(phase + static_cast<float>(i) / 6.0f, alpha));
  g.setGradientFill(grad);
  g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void kaleidoscope(juce::Graphics& g, juce::Rectangle<float> r, float lit, float seed) {
  juce::Graphics::ScopedSaveState save(g);
  juce::Path clip;
  clip.addRoundedRectangle(r, 6.0f);
  g.reduceClipRegion(clip);
  g.setColour(base().darker(0.5f));
  g.fillRect(r);
  const auto c = r.getCentre();
  const float reach = std::max(r.getWidth(), r.getHeight()) * 0.75f;
  const float spin = t() * 0.1f + seed;
  const float a = 0.35f + 0.5f * lit;
  // Six-fold petals on three rings, each ring turning its own way.
  for (int ring = 0; ring < 3; ++ring) {
    const float rr = reach * (0.18f + 0.22f * static_cast<float>(ring));
    const float dir = ring % 2 == 0 ? 1.0f : -1.0f;
    for (int k = 0; k < 12; ++k) {
      const float ang = spin * dir + kTwoPi * static_cast<float>(k) / 12.0f;
      juce::Path petal;
      petal.addEllipse(-rr * 0.32f, -rr * 0.1f, rr * 0.64f, rr * 0.2f);
      petal.applyTransform(juce::AffineTransform::rotation(ang).translated(c.x + std::cos(ang) * rr * 0.6f,
                                                                           c.y + std::sin(ang) * rr * 0.6f));
      g.setColour(palette(seed + 0.12f * static_cast<float>(ring) + (k % 2) * 0.33f, a * boost() * 0.8f));
      g.fillPath(petal);
    }
  }
  // The grille's own lattice on top, so it still reads as a cab: a
  // diagonal weave (two line sets), much cheaper than dots.
  g.setColour(juce::Colours::black.withAlpha(0.3f));
  const float cell = 6.0f;
  for (float d = -r.getHeight(); d < r.getWidth(); d += cell) {
    g.drawLine(r.getX() + d, r.getY(), r.getX() + d + r.getHeight(), r.getBottom(), 0.9f);
    g.drawLine(r.getX() + d + r.getHeight(), r.getY(), r.getX() + d, r.getBottom(), 0.9f);
  }
}

void dualKaleidoscope(juce::Graphics& g, juce::Rectangle<float> r, float blend) {
  const float split = r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, blend);
  const float band = r.getWidth() * 0.18f;
  {
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(r.withRight(split + band * 0.5f).toNearestInt());
    kaleidoscope(g, r, 1.0f - blend, 0.0f);
  }
  // B melts in across the band in thin slices of rising opacity.
  constexpr int slices = 8;
  for (int i = 0; i <= slices; ++i) {
    const float x0 = split - band * 0.5f + band * static_cast<float>(i) / slices;
    juce::Graphics::ScopedSaveState save(g);
    const auto area = i == slices ? r.withLeft(x0) : r.withLeft(x0).withWidth(band / slices + 0.5f);
    g.reduceClipRegion(area.toNearestInt());
    g.beginTransparencyLayer(static_cast<float>(i) / slices);
    kaleidoscope(g, r, blend, 0.5f);
    g.endTransparencyLayer();
  }
}

void ripples(juce::Graphics& g, juce::Rectangle<float> r, bool on, float feedback) {
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(r.toNearestInt());
  const juce::Point<float> c(r.getX() + 6.0f, r.getCentreY());
  for (int i = 0; i < 5; ++i) {
    const float u = std::fmod(t() * 0.35f + static_cast<float>(i) / 5.0f, 1.0f);
    const float rad = 4.0f + u * r.getWidth();
    const float alpha = (on ? 0.8f : 0.25f) * (1.0f - u) * (0.4f + 0.6f * feedback);
    g.setColour(palette(static_cast<float>(i) / 5.0f, alpha));
    g.drawEllipse(juce::Rectangle<float>(2 * rad, 2 * rad).withCentre(c), 1.2f);
  }
}

void nebula(juce::Graphics& g, juce::Rectangle<float> r, bool on, float decay) {
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(r.toNearestInt());
  for (int i = 0; i < 5; ++i) {
    const float drift = t() * (0.05f + 0.03f * static_cast<float>(i));
    const float x = r.getX() + r.getWidth() * std::fmod(hash01(i) + drift * (0.5f + decay), 1.0f);
    const float y = r.getY() + r.getHeight() * (0.3f + 0.4f * hash01(i + 9));
    const float rad = r.getHeight() * (0.6f + 0.6f * decay);
    // Dissolving: each cloud's opacity breathes slowly.
    const float breathe = 0.5f + 0.5f * std::sin(t() * 0.4f + static_cast<float>(i) * 1.7f);
    juce::ColourGradient cloud(palette(0.2f * static_cast<float>(i), (on ? 0.35f : 0.1f) * breathe),
                               {x, y}, palette(0.2f * static_cast<float>(i), 0.0f), {x + rad, y}, true);
    g.setGradientFill(cloud);
    g.fillEllipse(juce::Rectangle<float>(2 * rad, 2 * rad).withCentre({x, y}));
  }
}

void spiralWave(juce::Graphics& g, juce::Rectangle<float> r, bool on, float rate, float depth) {
  for (int strand = 0; strand < 2; ++strand) {
    juce::Path wave;
    for (int i = 0; i <= 40; ++i) {
      const float u = static_cast<float>(i) / 40.0f;
      const float ph = (u * 2.0f - t() * rate * 0.5f) * kTwoPi + static_cast<float>(strand) * juce::MathConstants<float>::pi;
      const float y = r.getCentreY() - std::sin(ph) * depth * r.getHeight() * 0.42f * (0.6f + 0.4f * u);
      const float x = r.getX() + u * r.getWidth();
      if (i == 0) wave.startNewSubPath(x, y);
      else wave.lineTo(x, y);
    }
    rainbowStroke(g, wave, strand == 0 ? 1.6f : 1.0f, static_cast<float>(strand) * 0.5f, on ? 0.95f : 0.35f);
  }
}

void vortex(juce::Graphics& g, juce::Rectangle<float> r, const Vortex& v) {
  juce::Graphics::ScopedSaveState save(g);
  juce::Path clip;
  clip.addRoundedRectangle(r, 4.0f);
  g.reduceClipRegion(clip);
  g.setColour(base().darker(0.6f));
  g.fillRect(r);
  const float lift = juce::jlimit(-1.0f, 1.0f, v.bend / 24.0f) * r.getHeight() * 0.3f;
  const juce::Point<float> c(r.getCentreX(), r.getCentreY() - lift);
  const float reach = std::hypot(r.getWidth(), r.getHeight()) * 0.55f;
  const float dim = v.on ? 1.0f : 0.35f;
  const int copies = v.detune > 0.05f ? 2 : 1;
  for (int copy = 0; copy < copies; ++copy) {
    const float offset = copies == 2 ? (copy == 0 ? -1.0f : 1.0f) * v.detune * 3.0f : 0.0f;
    const float shimmer = copies == 2 ? 0.55f + 0.45f * std::sin(t() * 1.3f + static_cast<float>(copy) * 2.0f) : 1.0f;
    // Rings travel inward with the octaves down and outward with the up.
    const float flow = t() * (0.12f + 0.25f * v.up - 0.25f * v.down);
    for (int i = 0; i < 9; ++i) {
      float u = std::fmod(static_cast<float>(i) / 9.0f + flow, 1.0f);
      if (u < 0.0f) u += 1.0f;
      const float rad = 2.0f + u * reach;
      const float twist = t() * 0.6f + u * 5.0f + static_cast<float>(copy);
      juce::Path arc;
      arc.addCentredArc(c.x + offset, c.y, rad, rad * 0.62f, twist, 0.0f, kTwoPi * 0.7f, true);
      const float weight = u < 0.5f ? 0.4f + v.down : 0.4f + v.up;
      g.setColour(palette(u + v.shift * 0.3f, dim * shimmer * juce::jlimit(0.1f, 0.9f, weight * (1.0f - u * 0.6f))));
      g.strokePath(arc, juce::PathStrokeType(1.0f + 2.0f * weight * (1.0f - u)));
    }
  }
  // The eye of the vortex.
  juce::ColourGradient eye(palette(0.75f, dim * (0.4f + 0.4f * v.down)), c, palette(0.75f, 0.0f),
                           c.translated(reach * 0.25f, 0.0f), true);
  g.setGradientFill(eye);
  g.fillEllipse(juce::Rectangle<float>(reach * 0.5f, reach * 0.5f).withCentre(c));
}

void crystal(juce::Graphics& g, juce::Rectangle<float> r, float amount) {
  if (amount <= 0.01f) return;
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(r.toNearestInt());
  g.setColour(juce::Colour(0xffbfefff).withAlpha(0.22f * amount));
  g.fillRect(r);
  // Facets: fixed shards, so the frozen picture holds still.
  for (int i = 0; i < 9; ++i) {
    juce::Path shard;
    const juce::Point<float> p(r.getX() + hash01(i * 5) * r.getWidth(), r.getY() + hash01(i * 5 + 1) * r.getHeight());
    const float s = 6.0f + 10.0f * hash01(i * 5 + 2);
    shard.addTriangle(p, p.translated(s, s * 0.4f), p.translated(s * 0.3f, s * 1.1f));
    g.setColour(juce::Colours::white.withAlpha(0.12f * amount + 0.1f * hash01(i)));
    g.fillPath(shard);
    g.setColour(juce::Colours::white.withAlpha(0.5f * amount));
    g.strokePath(shard, juce::PathStrokeType(0.6f));
  }
}

void rainbowKnob(juce::Graphics& g, juce::Rectangle<float> box, float angleDeg, float arcFromDeg) {
  const auto c = box.getCentre();
  const float R = box.getWidth() * 0.5f;
  const auto rad = [](float deg) { return juce::degreesToRadians(deg); };
  // A rainbow across the knob, left (start of travel) to right (end): an
  // arc stroked with it shows more of the wheel the further it turns.
  const auto wheel = [&](float alpha, float shift) {
    juce::ColourGradient grad(palette(shift, alpha), c.x - R, c.y + R * 0.5f, palette(shift + 0.83f, alpha), c.x + R,
                              c.y + R * 0.5f, false);
    grad.addColour(0.33, palette(shift + 0.28f, alpha));
    grad.addColour(0.5, palette(shift + 0.42f, alpha));
    grad.addColour(0.67, palette(shift + 0.56f, alpha));
    return grad;
  };
  juce::Path track;
  track.addCentredArc(c.x, c.y, R * 0.86f, R * 0.86f, 0.0f, rad(-135.0f), rad(135.0f), true);
  g.setGradientFill(wheel(0.2f, 0.0f));
  g.strokePath(track, juce::PathStrokeType(R * 0.1f));
  const float from = std::min(arcFromDeg, angleDeg), to = std::max(arcFromDeg, angleDeg);
  if (to - from >= 0.25f) {
    juce::Path value;
    value.addCentredArc(c.x, c.y, R * 0.86f, R * 0.86f, 0.0f, rad(from), rad(to), true);
    g.setGradientFill(wheel(0.25f * boost(), 0.0f));
    g.strokePath(value, juce::PathStrokeType(R * 0.26f));
    g.setGradientFill(wheel(1.0f, 0.0f));
    g.strokePath(value, juce::PathStrokeType(R * 0.11f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  }
  // Inner ring: a thin band of colour turning slowly.
  juce::Path inner;
  const float spin = t() * 0.2f;
  inner.addCentredArc(c.x, c.y, R * 0.6f, R * 0.6f, 0.0f, spin, spin + kTwoPi * 0.75f, true);
  g.setGradientFill(wheel(0.35f, 0.5f));
  g.strokePath(inner, juce::PathStrokeType(R * 0.05f));
  // The pointer: a glowing bead at the value.
  const float a = rad(angleDeg);
  const juce::Point<float> p(c.x + R * 0.86f * std::sin(a), c.y - R * 0.86f * std::cos(a));
  const float hue = (angleDeg + 135.0f) / 270.0f * 0.83f;
  juce::ColourGradient glow(palette(hue, 0.7f), p, palette(hue, 0.0f), p.translated(R * 0.35f, 0.0f), true);
  g.setGradientFill(glow);
  g.fillEllipse(juce::Rectangle<float>(R * 0.7f, R * 0.7f).withCentre(p));
  g.setColour(juce::Colours::white);
  g.fillEllipse(juce::Rectangle<float>(R * 0.16f, R * 0.16f).withCentre(p));
  g.drawLine({c.translated(R * 0.25f * std::sin(a), -R * 0.25f * std::cos(a)),
              c.translated(R * 0.55f * std::sin(a), -R * 0.55f * std::cos(a))},
             std::max(1.2f, R * 0.07f));
}

void pill(juce::Graphics& g, juce::Rectangle<float> r, bool active, bool hovered) {
  const float c = r.getHeight() * 0.5f;
  g.setColour(base().brighter(active ? 0.25f : 0.1f).withAlpha(0.85f));
  g.fillRoundedRectangle(r, c);
  if (active) {
    // The inner glow blooms from the middle.
    const auto mid = r.getCentre();
    juce::ColourGradient bloom(palette(0.05f, 0.85f * std::min(1.0f, boost())), mid, palette(0.25f, 0.15f),
                               mid.translated(r.getWidth() * 0.55f, 0.0f), true);
    g.setGradientFill(bloom);
    g.fillRoundedRectangle(r, c);
    g.setColour(palette(0.05f, 0.35f));
    g.drawRoundedRectangle(r.expanded(1.5f), c + 1.5f, 2.5f);
  }
  g.setColour(palette(active ? 0.05f : 0.6f, active ? 0.9f : (hovered ? 0.7f : 0.4f)));
  g.drawRoundedRectangle(r.reduced(0.5f), c, 1.0f);
}

void cable(juce::Graphics& g, juce::Line<float> line, bool live, float phase, float level) {
  juce::Path ribbon;
  ribbon.startNewSubPath(line.getStart());
  // A gentle wave in the ribbon, flowing with time.
  const int steps = 12;
  for (int i = 1; i <= steps; ++i) {
    const float u = static_cast<float>(i) / steps;
    const auto p = line.getPointAlongLineProportionally(u);
    ribbon.lineTo(p.x, p.y + std::sin(u * kTwoPi + t() * 1.5f) * 1.5f * (live ? 1.0f : 0.3f));
  }
  const float flow = t() * 0.2f;
  if (live) rainbowStroke(g, ribbon, 5.0f, flow, 0.15f * boost());
  rainbowStroke(g, ribbon, 1.8f, flow, live ? 0.85f : 0.3f);
  if (live && phase >= 0.0f) {
    const auto p = line.getPointAlongLineProportionally(phase);
    const float rr = 3.0f + 4.0f * level;
    juce::ColourGradient light(juce::Colours::white.withAlpha(0.8f), p, palette(phase, 0.0f),
                               p.translated(rr * 1.6f, 0.0f), true);
    g.setGradientFill(light);
    g.fillEllipse(juce::Rectangle<float>(rr * 3.2f, rr * 3.2f).withCentre(p));
  }
}

void landingRipple(juce::Graphics& g, juce::Rectangle<float> r, float age) {
  if (age < 0.0f || age >= 1.0f) return;
  juce::Graphics::ScopedSaveState save(g);
  g.reduceClipRegion(r.toNearestInt());
  const auto c = r.getCentre();
  const float reach = std::hypot(r.getWidth(), r.getHeight()) * 0.6f;
  for (int i = 0; i < 3; ++i) {
    const float u = age - static_cast<float>(i) * 0.12f;
    if (u <= 0.0f) continue;
    const float rad = u * reach;
    g.setColour(palette(static_cast<float>(i) * 0.3f, 0.55f * (1.0f - age)));
    g.drawEllipse(juce::Rectangle<float>(2 * rad, 2 * rad).withCentre(c), 2.5f);
  }
}

void spectrumFill(juce::Graphics& g, const juce::Path& fill, const juce::Path& line, juce::Rectangle<float> plot) {
  juce::ColourGradient rainbow(palette(0.0f, 0.55f), plot.getX(), 0.0f, palette(0.85f, 0.55f), plot.getRight(), 0.0f,
                               false);
  for (int i = 1; i < 6; ++i) rainbow.addColour(i / 6.0, palette(static_cast<float>(i) / 6.0f * 0.85f, 0.55f));
  // The mirrored reflection first, squashed under the baseline.
  {
    juce::Graphics::ScopedSaveState save(g);
    const float base = plot.getBottom();
    auto mirror = fill;
    mirror.applyTransform(juce::AffineTransform::scale(1.0f, -0.3f, 0.0f, base));
    g.setGradientFill(rainbow);
    g.setOpacity(0.25f);
    g.fillPath(mirror);
    g.setOpacity(1.0f);
  }
  g.setGradientFill(rainbow);
  g.fillPath(fill);
  // Darker toward the floor, so it reads as a liquid with a lit surface.
  g.setGradientFill(juce::ColourGradient::vertical(base().withAlpha(0.0f), plot.getY(), base().withAlpha(0.65f),
                                                   plot.getBottom()));
  g.fillPath(fill);
  rainbowStroke(g, line, 4.0f, 0.0f, 0.2f * boost());
  rainbowStroke(g, line, 1.6f, 0.0f, 1.0f);
}

void backdrop(juce::Graphics& g, juce::Rectangle<int> area, juce::Image& cache) {
  // The whole picture is composed once per animation frame (or once, when
  // static) into a full-size image; every paint in between, such as a meter
  // or the analyser redrawing over it, only blits its own patch.
  static juce::Image composed;
  static double composedAt = -1.0;
  static int composedGeneration = -1;
  const double now = std::floor(clock() * kBackdropFps) / kBackdropFps;
  if (composed.getWidth() == area.getWidth() && composed.getHeight() == area.getHeight() && composedAt == now &&
      composedGeneration == theme::kGeneration) {
    g.drawImageAt(composed, area.getX(), area.getY());
    return;
  }
  composedAt = now;
  composedGeneration = theme::kGeneration;
  if (composed.getWidth() != area.getWidth() || composed.getHeight() != area.getHeight())
    composed = juce::Image(juce::Image::RGB, juce::jmax(1, area.getWidth()), juce::jmax(1, area.getHeight()), false);
  // The blobs at an eighth of the size, scaled up: smooth and cheap.
  const int w = juce::jmax(8, area.getWidth() / 8), h = juce::jmax(8, area.getHeight() / 8);
  if (cache.getWidth() != w || cache.getHeight() != h) cache = juce::Image(juce::Image::RGB, w, h, false);
  {
    juce::Graphics lg(cache);
    lg.fillAll(base());
    const float e = 0.75f + 0.35f * energy() * gTrip;
    const float tt = t();
    for (int i = 0; i < 7; ++i) {
      // Lissajous drifts: blobs merge, split and wander, never fast.
      const float sx = 0.035f + 0.02f * hash01(i * 3);
      const float sy = 0.028f + 0.02f * hash01(i * 3 + 1);
      const float x = w * (0.5f + 0.42f * std::sin(tt * sx * kTwoPi * 0.5f + static_cast<float>(i) * 1.9f));
      const float y = h * (0.5f + 0.40f * std::cos(tt * sy * kTwoPi * 0.5f + static_cast<float>(i) * 2.7f));
      const float rad = std::min(w, h) * (0.32f + 0.18f * std::sin(tt * 0.05f + static_cast<float>(i)));
      juce::ColourGradient blob(palette(static_cast<float>(i) / 7.0f, 0.55f * e), {x, y},
                                palette(static_cast<float>(i) / 7.0f, 0.0f), {x + rad, y}, true);
      blob.addColour(0.55, palette(static_cast<float>(i) / 7.0f + 0.08f, 0.22f * e));
      lg.setGradientFill(blob);
      lg.fillEllipse(juce::Rectangle<float>(2 * rad, 2 * rad).withCentre({x, y}));
    }
  }
  {
    juce::Graphics cg(composed);
    const auto local = composed.getBounds().toFloat();
    cg.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    cg.drawImage(cache, local);
    stars(cg, local);
    // A veil, so the light show sits behind the controls, not on them.
    cg.setColour(base().withAlpha(0.35f));
    cg.fillRect(local);
  }
  g.drawImageAt(composed, area.getX(), area.getY());
}

}  // namespace t3k::ui::ether
