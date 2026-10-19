// Skins: swappable looks over the shared layout (see SKINS.md for the
// format). A skin is a folder with a skin.json manifest (name, author,
// version, colour tokens, style flags) and optionally a small background
// SVG. Built-ins ship inside the plugin; users add their own by dropping a
// folder into userSkinsDir(). Applying one rewrites the theme tokens
// (core/Theme.h); the editor then rebuilds its view tree. Audio and loaded
// models are never touched.
//
// Robustness: anything wrong falls back instead of breaking the UI. A token
// a skin leaves out or spells wrong keeps the classic value; an unreadable,
// oversized or malformed manifest makes apply() use the default skin.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace t3k::ui::skin {

inline constexpr const char* kDefaultId = "neural";
inline constexpr juce::int64 kMaxManifestBytes = 64 * 1024;
inline constexpr juce::int64 kMaxBackgroundBytes = 256 * 1024;

struct Info {
  juce::String id;  // folder name
  juce::String name, author, version, description;
  bool builtIn = false;
};

// $XDG_CONFIG_HOME/tone3000/skins (~/.config/tone3000/skins) on Linux,
// ~/Library/Application Support/TONE3000/Skins on macOS,
// %APPDATA%\TONE3000\Skins on Windows.
juce::File userSkinsDir();

// Built-ins first (Neural, Minimal, Vintage, Classic, Burning Church), then valid user skins
// by name. A user folder named like a built-in is skipped.
std::vector<Info> available();

// Applies `id` (empty: the default). Returns the id actually applied,
// which is the default when `id` is missing or broken.
juce::String apply(const juce::String& id);

// Parses a manifest onto the classic base and applies it; false (and
// nothing changed) when it isn't a usable manifest. `dir` resolves the
// optional background asset; a built-in passes its embedded SVG as
// `builtInBackground` instead. Exposed for tests.
bool applyManifest(const juce::String& json, const juce::File& dir, const juce::String& builtInBackground = {});

// The applied skin's background art, or null.
const juce::Drawable* background();

}  // namespace t3k::ui::skin
