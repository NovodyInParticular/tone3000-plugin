#pragma once
#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

/**
 * Rig sharing: a whole rig (signal chain, block settings, faceplate and
 * time-FX parameters, skin) as a small file or a pasteable text code.
 *
 * Library tones travel as TONE3000 tone + model ids only, never as
 * download URLs: the importer resolves them through the signed-in API, so
 * a shared rig can't point the plugin (and its auth token) at another
 * host, and signed URLs that expired since the export don't matter.
 *
 * Local captures / IRs (dropped files, not on TONE3000) can't be fetched
 * by anyone else. A rig file embeds their bytes while the total stays
 * under kMaxEmbedBytes; the text code never does. Whatever isn't embedded
 * is listed as missing, both when exporting and when importing.
 *
 * Everything read from outside goes through parse(): unknown fields are
 * ignored, numbers must be finite and are clamped to their ranges, strings
 * and counts are capped, and embedded files are size-limited (the
 * processor then validates their contents like any dropped file). A bad
 * or hostile rig yields an error, never a crash.
 */
namespace rigshare {

inline constexpr int kVersion = 1;
inline constexpr const char* kFormat = "tone3000-rig";
inline constexpr const char* kCodePrefix = "T3KRIG1:";
inline constexpr const char* kFileExtension = ".t3krig";
inline constexpr int kMaxBlocksPerLane = 32;
inline constexpr size_t kMaxEmbedBytes = 12u * 1024u * 1024u;   // all embedded files together
inline constexpr size_t kMaxFileBytes = 24u * 1024u * 1024u;    // a rig file on disk
inline constexpr int kMaxCodeChars = 256 * 1024;
inline constexpr int kMaxTextChars = 200;  // names, titles

struct Embedded {
  juce::String fileName;  // with .nam / .wav
  juce::MemoryBlock bytes;
};

struct Block {
  juce::String type;  // "nam", "ir" or "insert"
  // Settings (ChainBlock properties), normalized 0..1 unless noted.
  bool enabled = true, normalize = true;
  double slimSize = 0, inputGain = 0.5, outputGain = 0.5, mix = 1;
  // EQ: enabled, pre, then up to 6 bands of {type, freqHz, gainDb, q}.
  bool eqEnabled = true, eqPre = false;
  struct Band {
    juce::String type;
    double freqHz = 1000, gainDb = 0, q = 1;
  };
  std::vector<Band> bands;
  // Dual IR blend (IR blocks).
  double irBlend = 0.5, irLevelA = 0, irLevelB = 0, irAlignMs = 0;
  bool irInvertA = false, irInvertB = false;

  // The tone (not for inserts).
  int toneId = 0;   // TONE3000 tone id; 0 for local
  int modelId = 0;  // TONE3000 model id (local: ignored)
  bool local = false;
  juce::String title, format, gear;
  std::optional<Embedded> embedded;  // local only
  // Second IR (catalog only).
  int irBToneId = 0, irBModelId = 0;
};

struct Rig {
  juce::String name;
  juce::String skin;  // skin id; empty = leave the viewer's skin alone
  std::map<juce::String, double> params;  // denormalized host parameter values
  bool stereo = false;
  juce::String branchSide;  // "left" / "right" / empty
  int branchAfterIndex = -1;  // index in the branch side's lane
  std::vector<Block> left, right;

  // Local blocks without embedded bytes (titles).
  juce::StringArray missingLocals() const;
};

juce::var toVar(const Rig& rig, bool includeEmbedded);
// Validates and sanitizes; error set on failure.
std::optional<Rig> parse(const juce::var& data, juce::String& error);

// The file form: JSON text.
juce::String toFileText(const Rig& rig);
std::optional<Rig> fromFileText(const juce::String& text, juce::String& error);

// The text code: prefix + base64 of the gzipped JSON, embedded files left out.
juce::String toCode(const Rig& rig);
std::optional<Rig> fromCode(const juce::String& code, juce::String& error);

// Either form, by sniffing the prefix.
std::optional<Rig> fromText(const juce::String& text, juce::String& error);

}  // namespace rigshare
