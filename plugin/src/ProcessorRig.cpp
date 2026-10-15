// Rig sharing (RigShare.h): export the live rig, inspect a shared one, and
// apply it once the UI has resolved its library tones.
#include "Processor.h"
#include "RigShare.h"

namespace {

juce::String laneSideName(bool right) { return right ? "right" : "left"; }

juce::var stringArray(const juce::StringArray& items) {
  juce::Array<juce::var> out;
  for (const auto& s : items) out.add(s);
  return out;
}

}  // namespace

rigshare::Rig TONE3000Processor::captureRig(const juce::String& name, const juce::String& skin,
                                            bool embedLocals) const {
  rigshare::Rig rig;
  rig.name = name;
  rig.skin = skin;
  for (const auto& id : presetParameterIds())
    if (auto* p = parameters.getParameter(id))
      rig.params[id] = static_cast<double>(p->convertFrom0to1(p->getValue()));

  size_t budget = rigshare::kMaxEmbedBytes;
  juce::ScopedLock lock(chainMutex);
  rig.stereo = stereoEnabled.load();
  for (const bool right : {false, true}) {
    const auto& src = lane(right ? ChainSide::Right : ChainSide::Left);
    auto& dst = right ? rig.right : rig.left;
    for (const auto& blockPtr : src) {
      const ChainBlock& block = *blockPtr;
      rigshare::Block b;
      b.type = block.type == ChainBlockType::INSERT ? "insert" : block.type == ChainBlockType::NAM ? "nam" : "ir";
      b.enabled = block.enabled;
      b.normalize = block.normalizeEnabled;
      b.slimSize = block.namSlimSize;
      b.inputGain = block.inputGainNormalized;
      b.outputGain = block.outputGainNormalized;
      b.mix = block.mixNormalized;
      if (block.type != ChainBlockType::INSERT) {
        const juce::ValueTree eq = block.eq.toValueTree();
        b.eqEnabled = static_cast<bool>(eq.getProperty("enabled", true));
        b.eqPre = static_cast<bool>(eq.getProperty("pre", false));
        for (int i = 0; i < eq.getNumChildren(); ++i) {
          const auto band = eq.getChild(i);
          b.bands.push_back({band.getProperty("type").toString(), static_cast<double>(band.getProperty("freqHz")),
                             static_cast<double>(band.getProperty("gainDb")), static_cast<double>(band.getProperty("q"))});
        }
        b.irBlend = block.irBlend.blend;
        b.irLevelA = block.irBlend.levelADb;
        b.irLevelB = block.irBlend.levelBDb;
        b.irInvertA = block.irBlend.invertA;
        b.irInvertB = block.irBlend.invertB;
        b.irAlignMs = block.irBlend.alignMs;
        // A catalog second IR travels by id; a local one stays behind.
        if (block.irBModelId != 0 && !static_cast<bool>(block.irBToneVar["local"])) {
          b.irBToneId = static_cast<int>(block.irBToneVar["id"]);
          b.irBModelId = block.irBModelId;
        }
        b.local = static_cast<bool>(block.toneVar["local"]);
        b.title = block.toneVar["title"].toString().substring(0, rigshare::kMaxTextChars);
        b.format = b.type;
        b.gear = block.toneVar["gear"].toString();
        if (!b.local) {
          b.toneId = block.toneId;
          b.modelId = block.activeModelId;
        } else if (embedLocals) {
          const auto bytes = block.modelCache.find(block.activeModelId);
          if (bytes != block.modelCache.end() && !bytes->second.empty() && bytes->second.size() <= budget) {
            juce::String fileName;
            if (const auto* models = block.toneVar["models"].getArray())
              for (const auto& m : *models)
                if (static_cast<int>(m["id"]) == block.activeModelId)
                  fileName = m["name"].toString();
            rigshare::Embedded e;
            e.fileName = (fileName.isNotEmpty() ? fileName : b.title) + (b.type == "nam" ? ".nam" : ".wav");
            e.bytes.append(bytes->second.data(), bytes->second.size());
            budget -= bytes->second.size();
            b.embedded = std::move(e);
          }
        }
      }
      dst.push_back(std::move(b));
    }
  }
  if (!branchAfterBlockId.empty()) {
    const bool right = branchSourceSide == ChainSide::Right;
    const auto& src = lane(right ? ChainSide::Right : ChainSide::Left);
    for (size_t i = 0; i < src.size(); ++i)
      if (src[i]->id == branchAfterBlockId) {
        rig.branchSide = laneSideName(right);
        rig.branchAfterIndex = static_cast<int>(i);
      }
  }
  return rig;
}

juce::var TONE3000Processor::exportRig(const juce::String& name, const juce::String& skin, bool asCode) {
  const rigshare::Rig rig = captureRig(name, skin, !asCode);
  juce::StringArray embedded;
  for (const auto* l : {&rig.left, &rig.right})
    for (const auto& b : *l)
      if (b.embedded) embedded.add(b.title);
  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  out->setProperty("text", asCode ? rigshare::toCode(rig) : rigshare::toFileText(rig));
  out->setProperty("missing", stringArray(rig.missingLocals()));
  out->setProperty("embedded", stringArray(embedded));
  return out.get();
}

juce::var TONE3000Processor::inspectRig(const juce::String& text) {
  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  juce::String error;
  const auto rig = rigshare::fromText(text, error);
  out->setProperty("ok", rig.has_value());
  if (!rig) {
    out->setProperty("error", error);
    return out.get();
  }
  out->setProperty("name", rig->name);
  out->setProperty("skin", rig->skin);
  juce::Array<juce::var> tones;
  std::set<int> seen;
  for (const auto* l : {&rig->left, &rig->right})
    for (const auto& b : *l) {
      for (const int id : {b.local ? 0 : b.toneId, b.irBToneId})
        if (id > 0 && seen.insert(id).second) tones.add(id);
    }
  out->setProperty("toneIds", tones);
  out->setProperty("missing", stringArray(rig->missingLocals()));
  return out.get();
}

juce::var TONE3000Processor::applyRig(const juce::String& text, const juce::var& resolvedTones) {
  juce::DynamicObject::Ptr out = new juce::DynamicObject();
  juce::String error;
  const auto rig = rigshare::fromText(text, error);
  if (!rig) {
    out->setProperty("ok", false);
    out->setProperty("error", error);
    return out.get();
  }

  // A resolved tone: the JSON the UI fetched for that id, as an object whose
  // id matches and whose models carry the download URLs.
  const auto resolved = [&](int toneId) -> juce::var {
    const juce::String json = resolvedTones[juce::Identifier(juce::String(toneId))].toString();
    if (json.isEmpty() || json.length() > 1024 * 1024) return {};
    const juce::var tone = juce::JSON::parse(json);
    if (tone.getDynamicObject() == nullptr || static_cast<int>(tone["id"]) != toneId || tone["models"].getArray() == nullptr ||
        tone["models"].getArray()->isEmpty())
      return {};
    return tone;
  };
  const auto pickModel = [](const juce::var& tone, int wanted) {
    for (const auto& m : *tone["models"].getArray())
      if (static_cast<int>(m["id"]) == wanted) return wanted;
    return static_cast<int>(tone["models"][0]["id"]);
  };

  juce::StringArray missing = rig->missingLocals();
  juce::ValueTree snapshot("ChainSnapshot");
  snapshot.setProperty("stereoEnabled", rig->stereo, nullptr);
  std::string branchBlockId;

  for (const bool right : {false, true}) {
    juce::ValueTree laneTree(right ? "RightChainBlocks" : "ChainBlocks");
    const auto& blocks = right ? rig->right : rig->left;
    for (size_t i = 0; i < blocks.size(); ++i) {
      const auto& b = blocks[i];
      const std::string id = juce::Uuid().toString().toStdString();
      juce::ValueTree bt("ChainBlock");
      bt.setProperty("id", juce::String(id), nullptr);
      bt.setProperty("enabled", b.enabled, nullptr);
      bt.setProperty("normalize", b.normalize, nullptr);
      bt.setProperty("slimSize", b.slimSize, nullptr);
      bt.setProperty("inputGain", b.inputGain, nullptr);
      bt.setProperty("outputGain", b.outputGain, nullptr);
      bt.setProperty("mix", b.mix, nullptr);

      if (b.type == "insert") {
        bt.setProperty("type", chainBlockTypeToString(ChainBlockType::INSERT), nullptr);
      } else {
        juce::var tone;
        int modelId = 0;
        juce::ValueTree cache("ModelCache");
        if (!b.local) {
          tone = resolved(b.toneId);
          if (tone.getDynamicObject() == nullptr) {
            missing.add(b.title.isNotEmpty() ? b.title : "Tone " + juce::String(b.toneId));
            continue;
          }
          modelId = pickModel(tone, b.modelId);
        } else if (b.embedded) {
          // Validated and stashed like a dropped file; a bad file is missing.
          juce::String stashError;
          const juce::var model = stashLocalModelBytes(b.embedded->fileName, b.embedded->bytes, stashError);
          if (model.getDynamicObject() == nullptr) {
            missing.add(b.title.isNotEmpty() ? b.title : b.embedded->fileName);
            continue;
          }
          juce::DynamicObject::Ptr t = new juce::DynamicObject();
          t->setProperty("id", 0);
          t->setProperty("local", true);
          t->setProperty("title", b.title.isNotEmpty() ? b.title : model["name"].toString());
          t->setProperty("format", b.type);
          t->setProperty("models", juce::Array<juce::var>{model});
          if (b.gear.isNotEmpty()) t->setProperty("gear", b.gear);
          tone = t.get();
          modelId = static_cast<int>(model["id"]);
          juce::ValueTree cached("CachedModel");
          cached.setProperty("modelId", modelId, nullptr);
          cached.setProperty("data", juce::var(b.embedded->bytes), nullptr);
          cache.appendChild(cached, nullptr);
        } else {
          continue;  // already listed as missing
        }
        // The resolved format decides the block type, not the rig's word.
        const bool nam = tone["format"].toString().equalsIgnoreCase("nam");
        bt.setProperty("type", chainBlockTypeToString(nam ? ChainBlockType::NAM : ChainBlockType::IR), nullptr);
        bt.setProperty("toneId", b.local ? 0 : b.toneId, nullptr);
        bt.setProperty("toneJson", juce::JSON::toString(tone, true), nullptr);
        bt.setProperty("activeModelId", modelId, nullptr);
        if (b.irBToneId > 0) {
          const juce::var toneB = resolved(b.irBToneId);
          if (toneB.getDynamicObject() != nullptr) {
            const int bModel = pickModel(toneB, b.irBModelId);
            // B is a one-model tone (see DualIr): keep only the chosen model.
            juce::var oneModel = toneB.clone();
            juce::Array<juce::var> models;
            for (const auto& m : *toneB["models"].getArray())
              if (static_cast<int>(m["id"]) == bModel) models.add(m);
            oneModel.getDynamicObject()->setProperty("models", models);
            bt.setProperty("irBModelId", bModel, nullptr);
            bt.setProperty("irBToneJson", juce::JSON::toString(oneModel, true), nullptr);
          } else {
            missing.add((b.title.isNotEmpty() ? b.title : juce::String("IR")) + " (second IR)");
          }
        }
        bt.setProperty("irBlend", b.irBlend, nullptr);
        bt.setProperty("irLevelA", b.irLevelA, nullptr);
        bt.setProperty("irLevelB", b.irLevelB, nullptr);
        bt.setProperty("irInvertA", b.irInvertA, nullptr);
        bt.setProperty("irInvertB", b.irInvertB, nullptr);
        bt.setProperty("irAlignMs", b.irAlignMs, nullptr);
        juce::ValueTree eq("Eq");
        eq.setProperty("enabled", b.eqEnabled, nullptr);
        eq.setProperty("pre", b.eqPre, nullptr);
        for (const auto& band : b.bands) {
          juce::ValueTree bandTree("Band");
          bandTree.setProperty("type", band.type, nullptr);
          bandTree.setProperty("freqHz", band.freqHz, nullptr);
          bandTree.setProperty("gainDb", band.gainDb, nullptr);
          bandTree.setProperty("q", band.q, nullptr);
          eq.appendChild(bandTree, nullptr);
        }
        bt.appendChild(eq, nullptr);
        if (cache.getNumChildren() > 0) bt.appendChild(cache, nullptr);
      }
      if (rig->branchSide == laneSideName(right) && static_cast<int>(i) == rig->branchAfterIndex)
        branchBlockId = id;
      laneTree.appendChild(bt, nullptr);
    }
    snapshot.appendChild(laneTree, nullptr);
  }
  if (!branchBlockId.empty()) {
    snapshot.setProperty("branchSide", rig->branchSide, nullptr);
    snapshot.setProperty("branchAfterBlockId", juce::String(branchBlockId), nullptr);
  }

  // Applied like a preset: one undo step, muted splice held until the
  // models land, every rig parameter set as a host gesture (the rest of the
  // preset-managed set goes to its default, so the rig sounds the same
  // wherever it lands).
  ChainEditFade editFade(*this);
  Lane retired;
  {
    juce::ScopedLock lock(chainMutex);
    pushChainHistory();
    retired = restoreChainSnapshot(snapshot);
    activePresetId = {};
    activePresetName = {};
  }
  retired.clear();
  for (const auto& paramId : presetParameterIds()) {
    auto* p = parameters.getParameter(paramId);
    if (p == nullptr) continue;
    const auto it = rig->params.find(paramId);
    const float norm = it != rig->params.end()
                           ? juce::jlimit(0.0f, 1.0f, p->convertTo0to1(static_cast<float>(it->second)))
                           : p->getDefaultValue();
    p->beginChangeGesture();
    p->setValueNotifyingHost(norm);
    p->endChangeGesture();
  }
  editFade.releaseWhenChainLoadsSettle();
  juce::Logger::writeToLog("[Rig] Applied rig '" + rig->name + "' (" + juce::String(missing.size()) + " missing)");

  out->setProperty("ok", true);
  out->setProperty("name", rig->name);
  out->setProperty("skin", rig->skin);
  out->setProperty("missing", stringArray(missing));
  return out.get();
}
