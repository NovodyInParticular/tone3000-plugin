#include "Skin.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "Theme.h"
#include "UiBinaryData.h"

namespace t3k::ui::skin {

namespace {

struct BuiltIn {
  const char* id;
  const char* data;
  int size;
  const char* background = nullptr;  // embedded bg.svg, if the skin has one
  int backgroundSize = 0;
};
const std::vector<BuiltIn>& builtIns() {
  static const std::vector<BuiltIn> list = {
      {"neural", UiBinaryData::skin_neural_json, UiBinaryData::skin_neural_jsonSize},
      {"minimal", UiBinaryData::skin_minimal_json, UiBinaryData::skin_minimal_jsonSize},
      {"vintage", UiBinaryData::skin_vintage_json, UiBinaryData::skin_vintage_jsonSize},
      {"classic", UiBinaryData::skin_classic_json, UiBinaryData::skin_classic_jsonSize},
      {"burningchurch", UiBinaryData::skin_burningchurch_json, UiBinaryData::skin_burningchurch_jsonSize,
       UiBinaryData::skin_burningchurch_bg_svg, UiBinaryData::skin_burningchurch_bg_svgSize},
  };
  return list;
}

std::unique_ptr<juce::Drawable>& backgroundSlot() {
  static std::unique_ptr<juce::Drawable> art;
  return art;
}

// "#RRGGBB" or "#RRGGBBAA".
bool parseColour(const juce::var& v, juce::Colour& out) {
  const juce::String s = v.toString().trim();
  if (!s.startsWithChar('#')) return false;
  const juce::String hex = s.substring(1);
  if ((hex.length() != 6 && hex.length() != 8) || !hex.containsOnly("0123456789abcdefABCDEF")) return false;
  const auto value = static_cast<juce::uint32>(hex.getHexValue64());
  out = hex.length() == 6 ? juce::Colour(0xff000000u | value)
                          : juce::Colour((value >> 8) | ((value & 0xffu) << 24));
  return true;
}

float unit(const juce::var& v, float fallback) {
  if (!(v.isDouble() || v.isInt())) return fallback;
  const double d = static_cast<double>(v);
  return std::isfinite(d) ? juce::jlimit(0.0f, 1.0f, static_cast<float>(d)) : fallback;
}

juce::String readCapped(const juce::File& f, juce::int64 cap) {
  if (!f.existsAsFile() || f.getSize() > cap) return {};
  return f.loadFileAsString();
}

std::optional<Info> infoFrom(const juce::String& id, const juce::String& json, bool builtIn) {
  juce::var v;
  if (juce::JSON::parse(json, v).failed() || v.getDynamicObject() == nullptr) return std::nullopt;
  Info info;
  info.id = id;
  info.name = v["name"].toString().substring(0, 40).trim();
  if (info.name.isEmpty()) info.name = id;
  info.author = v["author"].toString().substring(0, 60);
  info.version = v["version"].toString().substring(0, 20);
  info.description = v["description"].toString().substring(0, 200);
  info.builtIn = builtIn;
  return info;
}

}  // namespace

juce::File userSkinsDir() {
#if JUCE_LINUX || JUCE_BSD
  const juce::String xdg = juce::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME", {});
  const juce::File base = xdg.startsWithChar('/') ? juce::File(xdg)
                                                  : juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                                        .getChildFile(".config");
  return base.getChildFile("tone3000").getChildFile("skins");
#elif JUCE_MAC
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("Application Support/TONE3000/Skins");
#else
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TONE3000/Skins");
#endif
}

std::vector<Info> available() {
  std::vector<Info> out;
  for (const auto& b : builtIns())
    if (auto info = infoFrom(b.id, juce::String::fromUTF8(b.data, b.size), true)) out.push_back(*info);
  std::vector<Info> user;
  for (const auto& dir : userSkinsDir().findChildFiles(juce::File::findDirectories, false)) {
    const juce::String id = dir.getFileName();
    if (std::any_of(out.begin(), out.end(), [&](const Info& i) { return i.id == id; })) continue;
    if (auto info = infoFrom(id, readCapped(dir.getChildFile("skin.json"), kMaxManifestBytes), false))
      user.push_back(*info);
  }
  std::sort(user.begin(), user.end(), [](const Info& a, const Info& b) { return a.name.compareIgnoreCase(b.name) < 0; });
  out.insert(out.end(), user.begin(), user.end());
  return out;
}

bool applyManifest(const juce::String& json, const juce::File& dir, const juce::String& builtInBackground) {
  juce::var v;
  if (json.isEmpty() || juce::JSON::parse(json, v).failed() || v.getDynamicObject() == nullptr) return false;

  // Built on the classic base: tokens the skin leaves out keep it.
  theme::resetToClassic();
  using namespace theme;
  const std::map<juce::String, juce::Colour*> tokens = {
      {"accent", &kBrandYellow}, {"brandRed", &kBrandRed},   {"brandBlue", &kBrandBlue},
      {"link", &kLinkBlue},      {"text", &kWhite},          {"black", &kBlack},
      {"muted", &kMuted},        {"subtle", &kSubtle},       {"highlight", &kHighlight},
      {"gray", &kGray},          {"border", &kBorder},       {"surface", &kSurface},
      {"surfaceRaised", &kSurfaceRaised}, {"segmentedTrack", &kSegmentedTrack}, {"panelBg", &kPanelBg},
      {"meterLow", &kMeterLow},  {"meterMid", &kMeterMid},   {"meterHigh", &kMeterHigh},
  };
  if (const auto* colors = v["colors"].getDynamicObject())
    for (const auto& [name, target] : tokens) {
      juce::Colour c;
      if (parseColour(colors->getProperty(name), c)) *target = c;
    }
  const auto& st = v["style"];
  if (st.isObject()) {
    kStyle.knob = st["knob"].toString() == "ring" ? KnobStyle::ring : KnobStyle::classic;
    kStyle.glow = unit(st["glow"], 0.0f);
    kStyle.scanlines = unit(st["scanlines"], 0.0f);
    kStyle.grid = unit(st["grid"], 0.0f);
    kStyle.monoLabels = st["monoLabels"].isBool() && static_cast<bool>(st["monoLabels"]);
    kStyle.animations = !st["animations"].isBool() || static_cast<bool>(st["animations"]);
    kStyle.glitchOnLoad = st["glitchOnLoad"].isBool() && static_cast<bool>(st["glitchOnLoad"]);
    const juce::String grille = st["grille"].toString();
    kStyle.grille = grille == "hex" ? Grille::hex : grille == "dots" ? Grille::dots : Grille::photo;
    juce::Colour c;
    if (parseColour(st["knobFace"], c)) kStyle.knobFace = c;
    kStyle.background = kBlack;
    if (parseColour(st["background"], c)) kStyle.background = c;
  } else {
    kStyle.background = kBlack;
  }

  // The header wordmark.
  if (const auto& logo = v["logo"]; logo.isObject()) {
    const juce::String style = logo["style"].toString();
    kLogo.style = style == "outline" ? LogoStyle::outline : style == "fill" ? LogoStyle::fill : LogoStyle::brand;
    juce::Colour c;
    if (parseColour(logo["color"], c)) kLogo.colour = c;
    if (parseColour(logo["echo"], c)) kLogo.echo = c;
    kLogo.glow = unit(logo["glow"], 0.0f);
  }

  // Optional background art: a small SVG inside the skin's own folder, or
  // embedded alongside a built-in's manifest.
  backgroundSlot().reset();
  const juce::String bg = v["backgroundImage"].toString();
  if (builtInBackground.isNotEmpty()) {
    backgroundSlot() = juce::Drawable::createFromSVGString(builtInBackground);
  } else if (bg.isNotEmpty() && dir != juce::File() && !bg.containsAnyOf("/\\") && bg.endsWithIgnoreCase(".svg")) {
    const juce::String svg = readCapped(dir.getChildFile(bg), kMaxBackgroundBytes);
    if (svg.isNotEmpty()) backgroundSlot() = juce::Drawable::createFromSVGString(svg);
  }
  ++kGeneration;
  return true;
}

juce::String apply(const juce::String& requested) {
  const juce::String id = requested.isNotEmpty() ? requested : juce::String(kDefaultId);
  for (const auto& b : builtIns())
    if (id == b.id &&
        applyManifest(juce::String::fromUTF8(b.data, b.size), {},
                      b.background != nullptr ? juce::String::fromUTF8(b.background, b.backgroundSize) : juce::String()))
      return id;
  const juce::File dir = userSkinsDir().getChildFile(juce::File::createLegalFileName(id));
  if (id.isNotEmpty() && dir.isDirectory() &&
      applyManifest(readCapped(dir.getChildFile("skin.json"), kMaxManifestBytes), dir))
    return id;
  if (id != kDefaultId) return apply(kDefaultId);
  theme::resetToClassic();
  ++theme::kGeneration;
  return "classic";
}

const juce::Drawable* background() { return backgroundSlot().get(); }

}  // namespace t3k::ui::skin
