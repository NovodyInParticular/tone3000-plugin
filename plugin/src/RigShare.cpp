#include "RigShare.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace rigshare {

namespace {

double num(const juce::var& v, double lo, double hi, double fallback) {
  if (!(v.isDouble() || v.isInt() || v.isInt64() || v.isBool())) return fallback;
  const double d = static_cast<double>(v);
  if (!std::isfinite(d)) return fallback;
  return juce::jlimit(lo, hi, d);
}

int integer(const juce::var& v, int lo, int hi, int fallback) {
  if (!(v.isInt() || v.isInt64() || v.isDouble())) return fallback;
  const double d = static_cast<double>(v);
  if (!std::isfinite(d) || d < lo || d > hi) return fallback;
  return static_cast<int>(d);
}

bool flag(const juce::var& v, bool fallback) { return v.isBool() || v.isInt() ? static_cast<bool>(v) : fallback; }

juce::String text(const juce::var& v, int maxChars = kMaxTextChars) {
  if (!v.isString()) return {};
  // Control characters out: names show in the UI and in file names.
  const juce::String s = v.toString().substring(0, maxChars);
  juce::String out;
  for (auto p = s.getCharPointer(); !p.isEmpty();) {
    const juce::juce_wchar c = p.getAndAdvance();
    if (c >= 0x20 && c != 0x7f) out += juce::String::charToString(c);
  }
  return out;
}

juce::String oneOf(const juce::var& v, std::initializer_list<const char*> allowed, const char* fallback) {
  const juce::String s = v.toString();
  for (const auto* a : allowed)
    if (s == a) return a;
  return fallback;
}

// Parameter ids a rig may set: the preset-managed faceplate and time-FX set.
// Anything else in a rig is ignored (calibration, solos, oversampling stay
// the viewer's own).
bool allowedParam(const juce::String& id) {
  static const juce::StringArray ids = {
      "inputLevel",     "outputLevel",      "outputBalance",   "toneBass",        "toneMid",
      "toneTreble",     "toneBassFreq",     "toneMidFreq",     "toneTrebleFreq",
      "gateThreshold",    "gateEnabled",     "gateRelease",     "gateHold",
      "gateRange",      "toneEqEnabled",    "pitchEnabled",    "pitchSemitones",  "pitchStep",
      "pitchTonality",  "pitchWindow",      "spreadEnabled",   "spreadOffset",    "spreadWobble",
      "spreadWobbleEnabled", "spreadCrossover", "spreadCrossoverEnabled", "spreadDiffuseEnabled",
      "alignEnabled",   "alignOffset",      "alignWobble",     "alignWobbleEnabled", "alignCrossover",
      "alignCrossoverEnabled", "alignDiffuseEnabled", "chainPanLeft", "chainPanRight", "chainPanLinked",
      "chainInvertLeft", "chainInvertRight", "fxDelayEnabled", "fxDelayTime",     "fxDelaySync",
      "fxDelayDivision", "fxDelayFeedback", "fxDelayMix",      "fxDelayPingPong", "fxDelayDamping",
      "fxReverbEnabled", "fxReverbSize",    "fxReverbDecay",   "fxReverbDamping", "fxReverbMix",
      "fxModEnabled",   "fxModType",        "fxModRate",       "fxModDepth",      "fxModMix",
      "fxOrder",        "cloudsEnabled",    "cloudsMode",      "cloudsQuality",   "cloudsFreeze",
      "cloudsPosition", "cloudsSize",       "cloudsPitch",     "cloudsDensity",   "cloudsTexture",
      "cloudsBlend",    "cloudsSpread",     "cloudsFeedback",  "cloudsReverb",
      "glitchEnabled",  "glitchMix",        "glitchOutput",    "glitchStutter",   "glitchStutterRate",
      "glitchStutterLength", "glitchBits",  "glitchRate",      "glitchChance",    "glitchIntensity",
      "glitchSeed",     "glitchFreeze",     "glitchFreezeFade", "glitchFreezeMode", "glitchStutterMode",
      "glitchPlacement"};
  return ids.contains(id);
}

juce::var blockToVar(const Block& b, bool includeEmbedded) {
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("type", b.type);
  o->setProperty("enabled", b.enabled);
  o->setProperty("normalize", b.normalize);
  o->setProperty("slimSize", b.slimSize);
  o->setProperty("inputGain", b.inputGain);
  o->setProperty("outputGain", b.outputGain);
  o->setProperty("mix", b.mix);
  if (b.type == "insert") return o.get();

  juce::DynamicObject::Ptr eq = new juce::DynamicObject();
  eq->setProperty("enabled", b.eqEnabled);
  eq->setProperty("pre", b.eqPre);
  juce::Array<juce::var> bands;
  for (const auto& band : b.bands) {
    juce::DynamicObject::Ptr bo = new juce::DynamicObject();
    bo->setProperty("type", band.type);
    bo->setProperty("freqHz", band.freqHz);
    bo->setProperty("gainDb", band.gainDb);
    bo->setProperty("q", band.q);
    bands.add(bo.get());
  }
  eq->setProperty("bands", bands);
  o->setProperty("eq", eq.get());

  if (b.type == "ir") {
    juce::DynamicObject::Ptr ir = new juce::DynamicObject();
    ir->setProperty("blend", b.irBlend);
    ir->setProperty("levelA", b.irLevelA);
    ir->setProperty("levelB", b.irLevelB);
    ir->setProperty("invertA", b.irInvertA);
    ir->setProperty("invertB", b.irInvertB);
    ir->setProperty("alignMs", b.irAlignMs);
    if (b.irBToneId > 0) {
      ir->setProperty("bToneId", b.irBToneId);
      ir->setProperty("bModelId", b.irBModelId);
    }
    o->setProperty("dualIr", ir.get());
  }

  juce::DynamicObject::Ptr tone = new juce::DynamicObject();
  tone->setProperty("local", b.local);
  if (!b.local) {
    tone->setProperty("id", b.toneId);
    tone->setProperty("modelId", b.modelId);
  }
  tone->setProperty("title", b.title);
  tone->setProperty("format", b.format);
  if (b.gear.isNotEmpty()) tone->setProperty("gear", b.gear);
  if (b.local && includeEmbedded && b.embedded) {
    juce::DynamicObject::Ptr e = new juce::DynamicObject();
    e->setProperty("fileName", b.embedded->fileName);
    e->setProperty("data", b.embedded->bytes.toBase64Encoding());
    tone->setProperty("embedded", e.get());
  }
  o->setProperty("tone", tone.get());
  return o.get();
}

std::optional<Block> parseBlock(const juce::var& v, size_t& embedBudget, juce::String& error) {
  if (v.getDynamicObject() == nullptr) {
    error = "A block isn't an object";
    return std::nullopt;
  }
  Block b;
  b.type = oneOf(v["type"], {"nam", "ir", "insert"}, "");
  if (b.type.isEmpty()) {
    error = "Unknown block type";
    return std::nullopt;
  }
  b.enabled = flag(v["enabled"], true);
  b.normalize = flag(v["normalize"], true);
  b.slimSize = num(v["slimSize"], 0, 1, 0);
  b.inputGain = num(v["inputGain"], 0, 1, 0.5);
  b.outputGain = num(v["outputGain"], 0, 1, 0.5);
  b.mix = num(v["mix"], 0, 1, 1);
  if (b.type == "insert") return b;

  const auto& eq = v["eq"];
  if (eq.getDynamicObject() != nullptr) {
    b.eqEnabled = flag(eq["enabled"], true);
    b.eqPre = flag(eq["pre"], false);
    if (const auto* bands = eq["bands"].getArray()) {
      for (int i = 0; i < juce::jmin(6, bands->size()); ++i) {
        const auto& bv = bands->getReference(i);
        Block::Band band;
        band.type = oneOf(bv["type"], {"lowcut", "highcut", "lowshelf", "highshelf", "bell"}, "bell");
        band.freqHz = num(bv["freqHz"], 10, 24000, 1000);
        band.gainDb = num(bv["gainDb"], -24, 24, 0);
        band.q = num(bv["q"], 0.1, 18, 1);
        b.bands.push_back(band);
      }
    }
  }
  const auto& ir = v["dualIr"];
  if (b.type == "ir" && ir.getDynamicObject() != nullptr) {
    b.irBlend = num(ir["blend"], 0, 1, 0.5);
    b.irLevelA = num(ir["levelA"], -12, 12, 0);
    b.irLevelB = num(ir["levelB"], -12, 12, 0);
    b.irInvertA = flag(ir["invertA"], false);
    b.irInvertB = flag(ir["invertB"], false);
    b.irAlignMs = num(ir["alignMs"], -2, 2, 0);
    b.irBToneId = integer(ir["bToneId"], 1, std::numeric_limits<int>::max(), 0);
    b.irBModelId = b.irBToneId > 0 ? integer(ir["bModelId"], 1, std::numeric_limits<int>::max(), 0) : 0;
    if (b.irBModelId == 0) b.irBToneId = 0;
  }

  const auto& tone = v["tone"];
  if (tone.getDynamicObject() == nullptr) {
    error = "A block has no tone";
    return std::nullopt;
  }
  b.local = flag(tone["local"], false);
  b.title = text(tone["title"]);
  b.format = b.type == "nam" ? "nam" : "ir";
  b.gear = oneOf(tone["gear"], {"amp", "amp-cab", "cab", "pedal", "outboard", "full-rig", "ir", "experimental"}, "");
  if (!b.local) {
    b.toneId = integer(tone["id"], 1, std::numeric_limits<int>::max(), 0);
    b.modelId = integer(tone["modelId"], 1, std::numeric_limits<int>::max(), 0);
    if (b.toneId == 0) {
      error = "A library tone has no valid id";
      return std::nullopt;
    }
  } else if (const auto& e = tone["embedded"]; e.getDynamicObject() != nullptr) {
    juce::String name = juce::File::createLegalFileName(text(e["fileName"], 120));
    const juce::String ext = b.type == "nam" ? ".nam" : ".wav";
    if (!name.endsWithIgnoreCase(ext)) name = (name.isEmpty() ? juce::String("capture") : name) + ext;
    const juce::String data = e["data"].toString();
    // Base64 inflates by 4/3: refuse before decoding anything too big.
    if (static_cast<size_t>(data.length()) / 4 * 3 > embedBudget) {
      error = "Embedded files are too large";
      return std::nullopt;
    }
    Embedded emb;
    emb.fileName = name;
    if (!emb.bytes.fromBase64Encoding(data) || emb.bytes.getSize() == 0 || emb.bytes.getSize() > embedBudget) {
      error = "An embedded file is damaged";
      return std::nullopt;
    }
    embedBudget -= emb.bytes.getSize();
    b.embedded = std::move(emb);
  }
  return b;
}

std::optional<std::vector<Block>> parseLane(const juce::var& v, size_t& budget, juce::String& error) {
  std::vector<Block> lane;
  if (v.isVoid()) return lane;
  const auto* arr = v.getArray();
  if (arr == nullptr) {
    error = "A chain isn't a list";
    return std::nullopt;
  }
  if (arr->size() > kMaxBlocksPerLane) {
    error = "Too many blocks";
    return std::nullopt;
  }
  for (const auto& item : *arr) {
    auto block = parseBlock(item, budget, error);
    if (!block) return std::nullopt;
    lane.push_back(std::move(*block));
  }
  return lane;
}

}  // namespace

juce::StringArray Rig::missingLocals() const {
  juce::StringArray out;
  for (const auto* lane : {&left, &right})
    for (const auto& b : *lane)
      if (b.type != "insert" && b.local && !b.embedded) out.add(b.title.isNotEmpty() ? b.title : "Local file");
  return out;
}

juce::var toVar(const Rig& rig, bool includeEmbedded) {
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("format", kFormat);
  o->setProperty("version", kVersion);
  o->setProperty("name", rig.name);
  if (rig.skin.isNotEmpty()) o->setProperty("skin", rig.skin);
  juce::DynamicObject::Ptr params = new juce::DynamicObject();
  for (const auto& [id, value] : rig.params) params->setProperty(id, value);
  o->setProperty("params", params.get());
  o->setProperty("stereo", rig.stereo);
  if (rig.branchSide.isNotEmpty() && rig.branchAfterIndex >= 0) {
    o->setProperty("branchSide", rig.branchSide);
    o->setProperty("branchAfterIndex", rig.branchAfterIndex);
  }
  for (const auto& [key, lane] : {std::pair<const char*, const std::vector<Block>*>{"left", &rig.left},
                                  std::pair<const char*, const std::vector<Block>*>{"right", &rig.right}}) {
    juce::Array<juce::var> blocks;
    for (const auto& b : *lane) blocks.add(blockToVar(b, includeEmbedded));
    o->setProperty(key, blocks);
  }
  return o.get();
}

std::optional<Rig> parse(const juce::var& data, juce::String& error) {
  if (data.getDynamicObject() == nullptr || data["format"].toString() != kFormat) {
    error = "Not a TONE3000 rig";
    return std::nullopt;
  }
  const int version = integer(data["version"], 0, 1000000, 0);
  if (version < 1 || version > kVersion) {
    error = version > kVersion ? "This rig needs a newer version of the plugin" : "Not a TONE3000 rig";
    return std::nullopt;
  }
  Rig rig;
  rig.name = text(data["name"]);
  rig.skin = juce::File::createLegalFileName(text(data["skin"], 64));
  if (const auto* params = data["params"].getDynamicObject()) {
    for (const auto& prop : params->getProperties()) {
      const juce::String id = prop.name.toString();
      if (!allowedParam(id)) continue;
      const juce::var& value = prop.value;
      if (!(value.isDouble() || value.isInt() || value.isInt64() || value.isBool())) continue;
      const double d = static_cast<double>(value);
      // Ranges are the parameters' own business (convertTo0to1 clamps);
      // only reject what can't be a value at all.
      if (std::isfinite(d) && std::abs(d) < 1.0e6) rig.params[id] = d;
    }
  }
  rig.stereo = flag(data["stereo"], false);
  size_t budget = kMaxEmbedBytes;
  auto left = parseLane(data["left"], budget, error);
  if (!left) return std::nullopt;
  auto right = parseLane(data["right"], budget, error);
  if (!right) return std::nullopt;
  rig.left = std::move(*left);
  rig.right = std::move(*right);
  if (rig.left.empty() && rig.right.empty()) {
    error = "The rig has no blocks";
    return std::nullopt;
  }
  rig.branchSide = oneOf(data["branchSide"], {"left", "right"}, "");
  if (rig.branchSide.isNotEmpty()) {
    const auto& lane = rig.branchSide == "left" ? rig.left : rig.right;
    rig.branchAfterIndex = integer(data["branchAfterIndex"], 0, static_cast<int>(lane.size()) - 1, -1);
    if (rig.branchAfterIndex < 0) rig.branchSide = {};
  }
  return rig;
}

juce::String toFileText(const Rig& rig) { return juce::JSON::toString(toVar(rig, true), false); }

std::optional<Rig> fromFileText(const juce::String& text, juce::String& error) {
  if (static_cast<size_t>(text.getNumBytesAsUTF8()) > kMaxFileBytes) {
    error = "The rig file is too large";
    return std::nullopt;
  }
  juce::var data;
  if (juce::JSON::parse(text, data).failed()) {
    error = "The rig file is damaged";
    return std::nullopt;
  }
  return parse(data, error);
}

juce::String toCode(const Rig& rig) {
  const juce::String json = juce::JSON::toString(toVar(rig, false), true);
  juce::MemoryOutputStream compressed;
  {
    juce::GZIPCompressorOutputStream gz(compressed, 9);
    gz.writeText(json, false, false, nullptr);
  }
  return juce::String(kCodePrefix) + compressed.getMemoryBlock().toBase64Encoding();
}

std::optional<Rig> fromCode(const juce::String& code, juce::String& error) {
  const juce::String trimmed = code.trim().removeCharacters(" \r\n\t");
  if (!trimmed.startsWith(kCodePrefix)) {
    error = "Not a TONE3000 rig code";
    return std::nullopt;
  }
  if (trimmed.length() > kMaxCodeChars) {
    error = "The rig code is too long";
    return std::nullopt;
  }
  juce::MemoryBlock packed;
  if (!packed.fromBase64Encoding(trimmed.substring(static_cast<int>(strlen(kCodePrefix))))) {
    error = "The rig code is damaged";
    return std::nullopt;
  }
  juce::MemoryInputStream in(packed, false);
  juce::GZIPDecompressorInputStream gz(in);
  // Bounded read: a zip bomb stops at the cap.
  juce::MemoryOutputStream json;
  constexpr size_t kMaxJson = 2u * 1024u * 1024u;
  char buffer[8192];
  while (!gz.isExhausted()) {
    const int n = gz.read(buffer, sizeof(buffer));
    if (n <= 0) break;
    json.write(buffer, static_cast<size_t>(n));
    if (json.getDataSize() > kMaxJson) {
      error = "The rig code is too large";
      return std::nullopt;
    }
  }
  if (json.getDataSize() == 0) {
    error = "The rig code is damaged";
    return std::nullopt;
  }
  juce::var data;
  if (juce::JSON::parse(json.toUTF8(), data).failed()) {
    error = "The rig code is damaged";
    return std::nullopt;
  }
  auto rig = parse(data, error);
  // Codes never carry files, whatever the text claims.
  if (rig)
    for (auto* lane : {&rig->left, &rig->right})
      for (auto& b : *lane) b.embedded.reset();
  return rig;
}

std::optional<Rig> fromText(const juce::String& text, juce::String& error) {
  if (text.trimStart().startsWith(kCodePrefix)) return fromCode(text, error);
  return fromFileText(text, error);
}

}  // namespace rigshare
