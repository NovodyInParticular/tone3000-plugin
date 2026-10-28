// Shared theme tokens (port of theme.ts): black surfaces, white/gray chrome
// and three brand accents. Every colour/size literal in the UI comes from
// here, so the palette changes in one place.
//
// Skins (core/Skin.h) restyle the UI at runtime: the colour tokens below are
// mutable for that reason (sizes stay constexpr: a skin changes the look,
// never the layout). Only the message thread writes them, through
// applySkin(), and the editor rebuilds its view tree right after, so no
// widget keeps a colour from the previous skin. `kBrandYellow` is the
// accent role (value arcs, armed chrome, highlights) whatever its colour.
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <optional>

namespace t3k::ui::theme {

// Lucide / custom glyph size inside ICON_BOX_SIZE chrome boxes.
inline constexpr int kIconSize = 14;
// Square hit-target for icon buttons beside knobs and in card headers.
inline constexpr int kIconBoxSize = 20;
// Corner radius for every icon/text chrome box.
inline constexpr float kIconBoxRadius = 2.0f;
// Height for text chrome (EQ, PRE, LITE/FULL segments).
inline constexpr int kTextBoxHeight = 20;

// The two knob footprints; every knob is one of these.
inline constexpr int kKnobSizePrimary = 48;
inline constexpr int kKnobSizeSecondary = 36;

// The one dim applied to every disabled/off control (`.ui-off`).
inline constexpr float kDisabledOpacity = 0.45f;

// Knob-to-label gap.
inline constexpr int kKnobLabelGap = 8;

// Vertical lift for a chrome icon box in a bottom-aligned faceplate row: from
// the label baseline up to the centre of a secondary knob.
inline constexpr int faceplateChromeLift(int secondaryKnobSize) {
  return -(kKnobLabelGap + 14 + secondaryKnobSize / 2 - kIconBoxSize / 2);
}

inline juce::Colour kBrandBlue{0xff0000ff};
inline juce::Colour kBrandYellow{0xffffff00};
inline juce::Colour kBrandRed{0xffff0000};
// Level meter ramp (MeterScale.h): quiet, middle and hot ends of the scale.
inline juce::Colour kMeterLow{0xff0000ff};
inline juce::Colour kMeterMid{0xffffff00};
inline juce::Colour kMeterHigh{0xffff0000};
// Inline doc / "Learn More" links in settings.
inline juce::Colour kLinkBlue{0xff40a6ff};

inline juce::Colour kWhite{0xffffffff};
inline juce::Colour kBlack{0xff000000};

// rgba(235, 235, 245, a) family: primary muted text, secondary labels,
// pressed/active fill.
inline juce::Colour kMuted = juce::Colour(235, 235, 245).withAlpha(0.60f);
inline juce::Colour kSubtle = juce::Colour(235, 235, 245).withAlpha(0.40f);
inline juce::Colour kHighlight = juce::Colour(235, 235, 245).withAlpha(0.18f);
// Disabled/idle icon gray.
inline juce::Colour kGray{0xff8d8d93};
// Hairline used by every card/section/segment border (1px).
inline juce::Colour kBorder = juce::Colour(84, 84, 88).withAlpha(0.65f);
// Card body background / raised chrome.
inline juce::Colour kSurface{0xff151517};
inline juce::Colour kSurfaceRaised{0xff1c1c1e};
// Shared fill behind LITE/FULL and the EQ view switcher.
inline juce::Colour kSegmentedTrack = juce::Colour(120, 120, 128).withAlpha(0.36f);
// Floating panels (popovers, menus, the image deck): #141416, hairline
// border, 14px corners.
inline juce::Colour kPanelBg{0xff141416};
inline constexpr float kPanelCorner = 14.0f;

// Non-colour skin tokens: drawing styles and effects, all cheap enough for
// software rendering (no blurs; glows are a few translucent strokes, the
// textures are cached images).
enum class KnobStyle { classic, ring };
enum class Grille { photo, dots, hex };
struct Style {
  KnobStyle knob = KnobStyle::classic;
  float glow = 0.0f;       // 0..1: accent halo strength on arcs and lit chrome
  float scanlines = 0.0f;  // 0..1: horizontal line texture over the chain area
  float grid = 0.0f;       // 0..1: grid texture over the chain area
  bool monoLabels = false; // knob and section labels in the mono face
  bool animations = true;  // pulses and the load flicker (off: static)
  bool glitchOnLoad = false;
  Grille grille = Grille::photo;  // how the rig view draws a cab
  juce::Colour knobFace{0xff1d1d1d};  // ring-style knob body
  juce::Colour background{0xff000000};  // behind the chain
  bool psychedelic = false;  // the Ether art (core/Ether.h) on every view that has it
  bool displayTitles = false;  // headings and slot titles in the display face (Shrikhand)
  bool roundText = false;      // body text and labels in the rounded face (Varela Round)
};
inline Style kStyle;

// How the header draws the TONE3000 wordmark (skin.json "logo").
//  brand:   the original artwork (yellow face, red and blue slices).
//  fill:    the face in one colour.
//  outline: the face as an outline, with a halo of `glow` strength.
// The offset slices take `echo`, or are left out when the skin gives none.
enum class LogoStyle { brand, fill, outline };
struct Logo {
  LogoStyle style = LogoStyle::brand;
  std::optional<juce::Colour> colour;  // the face; unset: the accent
  std::optional<juce::Colour> echo;    // the slices; unset: none
  float glow = 0.0f;
};
inline Logo kLogo;
// The "Reduce animations" setting (UiPrefs::kReduceAnimations), machine-wide
// like the skin: turns off every moving visual whatever the skin asks for.
inline bool kReduceMotion = false;
// Whether moving visuals (pulses, marquees, live mini visuals) should run.
inline bool animate() { return kStyle.animations && !kReduceMotion; }
// Bumped whenever a skin is applied; caches of rendered artwork key on it.
inline int kGeneration = 0;

// Every token back to the built-in (upstream) look: the "classic" skin and
// the base every other skin starts from, so a skin that leaves a token out
// gets this value.
inline void resetToClassic() {
  kBrandBlue = juce::Colour{0xff0000ff};
  kBrandYellow = juce::Colour{0xffffff00};
  kBrandRed = juce::Colour{0xffff0000};
  kMeterLow = juce::Colour{0xff0000ff};
  kMeterMid = juce::Colour{0xffffff00};
  kMeterHigh = juce::Colour{0xffff0000};
  kLinkBlue = juce::Colour{0xff40a6ff};
  kWhite = juce::Colour{0xffffffff};
  kBlack = juce::Colour{0xff000000};
  kMuted = juce::Colour(235, 235, 245).withAlpha(0.60f);
  kSubtle = juce::Colour(235, 235, 245).withAlpha(0.40f);
  kHighlight = juce::Colour(235, 235, 245).withAlpha(0.18f);
  kGray = juce::Colour{0xff8d8d93};
  kBorder = juce::Colour(84, 84, 88).withAlpha(0.65f);
  kSurface = juce::Colour{0xff151517};
  kSurfaceRaised = juce::Colour{0xff1c1c1e};
  kSegmentedTrack = juce::Colour(120, 120, 128).withAlpha(0.36f);
  kPanelBg = juce::Colour{0xff141416};
  kStyle = Style{};
  kLogo = Logo{};
}

}  // namespace t3k::ui::theme
