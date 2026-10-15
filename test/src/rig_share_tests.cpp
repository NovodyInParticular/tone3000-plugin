// Rig sharing tests (RigShare.h, ProcessorRig.cpp). These pin:
//
//   - a rig survives the file and code forms; codes never carry files,
//   - hostile input can't crash or slip through: garbage, wrong types,
//     non-finite and out-of-range numbers, unknown parameters, oversized
//     lists / embeds / codes, a gzip bomb, newer versions; fields a rig must
//     never carry (download URLs) are dropped,
//   - processor level: export lists local captures as missing in a code
//     and embeds them in a file; import resolves library tones only from
//     the UI-supplied JSON, rebuilds blocks, settings, parameters and the
//     stereo layout, reports unresolved and unembedded tones as missing,
//     and rejects an embedded file that isn't a valid capture.
#include "RigShare.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

rigshare::Rig sampleRig() {
  rigshare::Rig rig;
  rig.name = "Crunch";
  rig.skin = "vintage";
  rig.params["outputLevel"] = 0.6;
  rig.params["fxReverbEnabled"] = 1.0;
  rig.stereo = false;
  rigshare::Block amp;
  amp.type = "nam";
  amp.toneId = 101;
  amp.modelId = 1101;
  amp.title = "AC30";
  amp.format = "nam";
  amp.gear = "amp";
  amp.inputGain = 0.7;
  amp.bands.push_back({"bell", 800, 3, 1.2});
  rigshare::Block cab;
  cab.type = "ir";
  cab.local = true;
  cab.title = "My cab";
  cab.format = "ir";
  cab.irBlend = 0.3;
  cab.irBToneId = 202;
  cab.irBModelId = 2202;
  rigshare::Embedded e;
  e.fileName = "my-cab.wav";
  e.bytes.append("RIFFxxxx", 8);
  cab.embedded = e;
  rigshare::Block ins;
  ins.type = "insert";
  rig.left = {amp, cab, ins};
  return rig;
}

juce::var parsedJson(const juce::String& s) { return juce::JSON::parse(s); }

}  // namespace

TEST(RigShareTest, FileRoundTripKeepsEverything) {
  juce::String error;
  const auto rig = rigshare::fromFileText(rigshare::toFileText(sampleRig()), error);
  ASSERT_TRUE(rig.has_value()) << error;
  EXPECT_EQ(rig->name, "Crunch");
  EXPECT_EQ(rig->skin, "vintage");
  EXPECT_DOUBLE_EQ(rig->params.at("outputLevel"), 0.6);
  ASSERT_EQ(rig->left.size(), 3u);
  EXPECT_EQ(rig->left[0].toneId, 101);
  EXPECT_EQ(rig->left[0].modelId, 1101);
  EXPECT_DOUBLE_EQ(rig->left[0].inputGain, 0.7);
  ASSERT_EQ(rig->left[0].bands.size(), 1u);
  EXPECT_DOUBLE_EQ(rig->left[0].bands[0].gainDb, 3);
  EXPECT_TRUE(rig->left[1].local);
  ASSERT_TRUE(rig->left[1].embedded.has_value());
  EXPECT_EQ(rig->left[1].embedded->bytes.getSize(), 8u);
  EXPECT_EQ(rig->left[1].irBToneId, 202);
  EXPECT_EQ(rig->left[2].type, "insert");
  EXPECT_TRUE(rig->missingLocals().isEmpty());
}

TEST(RigShareTest, CodeRoundTripDropsFiles) {
  const juce::String code = rigshare::toCode(sampleRig());
  EXPECT_TRUE(code.startsWith(rigshare::kCodePrefix));
  EXPECT_LT(code.length(), 2000) << "a code stays small enough to paste";
  juce::String error;
  // Pasted codes get wrapped and indented by chat apps.
  const juce::String mangled = "  " + code.substring(0, 40) + "\n  " + code.substring(40) + "\n";
  const auto rig = rigshare::fromCode(mangled, error);
  ASSERT_TRUE(rig.has_value()) << error;
  EXPECT_FALSE(rig->left[1].embedded.has_value());
  EXPECT_EQ(rig->missingLocals(), juce::StringArray{"My cab"});
  EXPECT_EQ(rig->left[0].toneId, 101);
}

TEST(RigShareTest, SanitizesNumbersTextAndParams) {
  auto v = rigshare::toVar(sampleRig(), true);
  auto* left0 = v["left"][0].getDynamicObject();
  left0->setProperty("inputGain", 99.0);
  left0->setProperty("mix", juce::var(std::numeric_limits<double>::quiet_NaN()));
  left0->setProperty("enabled", "yes please");
  left0->getProperty("tone").getDynamicObject()->setProperty("title", juce::String("A\nB\tC") + juce::String::repeatedString("x", 5000));
  left0->getProperty("tone").getDynamicObject()->setProperty("model_url", "https://evil.example/m.nam");
  v["params"].getDynamicObject()->setProperty("calibrateInput", 1.0);   // not a rig parameter
  v["params"].getDynamicObject()->setProperty("inputCalGain", 24.0);    // nor this
  v["params"].getDynamicObject()->setProperty("outputLevel", juce::var(std::numeric_limits<double>::infinity()));
  juce::String error;
  const auto rig = rigshare::parse(v, error);
  ASSERT_TRUE(rig.has_value()) << error;
  EXPECT_DOUBLE_EQ(rig->left[0].inputGain, 1.0);
  EXPECT_DOUBLE_EQ(rig->left[0].mix, 1.0);
  EXPECT_TRUE(rig->left[0].enabled);
  EXPECT_FALSE(rig->left[0].title.containsAnyOf("\n\t"));
  EXPECT_LE(rig->left[0].title.length(), rigshare::kMaxTextChars);
  EXPECT_EQ(rig->params.count("calibrateInput"), 0u);
  EXPECT_EQ(rig->params.count("inputCalGain"), 0u);
  EXPECT_EQ(rig->params.count("outputLevel"), 0u);
  EXPECT_FALSE(juce::JSON::toString(rigshare::toVar(*rig, true)).contains("evil.example"))
      << "download URLs never survive a parse";
}

TEST(RigShareTest, RejectsHostileShapes) {
  juce::String error;
  for (const juce::String text : {juce::String(), juce::String("null"), juce::String("[]"), juce::String("{\"format\":1}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":99}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1,\"left\":5}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1,\"left\":[{\"type\":\"bomb\"}]}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1,\"left\":[{\"type\":\"nam\"}]}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1,\"left\":[{\"type\":\"nam\",\"tone\":{\"id\":-4}}]}"),
                                  juce::String("{\"format\":\"tone3000-rig\",\"version\":1,\"left\":[{\"type\":\"ir\",\"tone\":{\"local\":true,\"embedded\":{\"data\":\"!!!!\"}}}]}"),
                                  juce::String("T3KRIG1:not-base64-at-all%%%"), juce::String("T3KRIG1:"),
                                  juce::String("{{{{")}) {
    error = {};
    EXPECT_FALSE(rigshare::fromText(text, error).has_value()) << text;
    EXPECT_TRUE(error.isNotEmpty()) << text;
  }
  const auto newer = rigshare::fromText("{\"format\":\"tone3000-rig\",\"version\":99}", error);
  EXPECT_TRUE(error.contains("newer"));

  // Too many blocks.
  auto v = rigshare::toVar(sampleRig(), false);
  juce::Array<juce::var> many;
  for (int i = 0; i < rigshare::kMaxBlocksPerLane + 1; ++i) many.add(v["left"][2]);
  v.getDynamicObject()->setProperty("left", many);
  EXPECT_FALSE(rigshare::parse(v, error).has_value());

  // Embeds over the budget, before decoding.
  auto big = rigshare::toVar(sampleRig(), true);
  big["left"][1]["tone"]["embedded"].getDynamicObject()->setProperty(
      "data", juce::String::repeatedString("AAAA", static_cast<int>(rigshare::kMaxEmbedBytes / 3 + 10)));
  EXPECT_FALSE(rigshare::parse(big, error).has_value());
  EXPECT_TRUE(error.contains("too large"));
}

TEST(RigShareTest, GzipBombStopsAtTheCap) {
  juce::MemoryOutputStream packed;
  {
    juce::GZIPCompressorOutputStream gz(packed, 9);
    const juce::String chunk = juce::String::repeatedString(" ", 1 << 16);
    for (int i = 0; i < 64; ++i) gz.writeText(chunk, false, false, nullptr);  // 4 MB of spaces
  }
  juce::String error;
  EXPECT_FALSE(rigshare::fromCode(juce::String(rigshare::kCodePrefix) + packed.getMemoryBlock().toBase64Encoding(),
                                  error)
                   .has_value());
  EXPECT_TRUE(error.contains("too large"));
}

TEST(RigShareTest, RandomGarbageNeverCrashes) {
  juce::Random rng(1234);
  const auto valid = rigshare::toFileText(sampleRig());
  juce::String error;
  for (int i = 0; i < 300; ++i) {
    juce::String s = valid;
    for (int k = 0; k < 6; ++k) {
      const int at = rng.nextInt(s.length());
      s = s.substring(0, at) + juce::String::charToString(static_cast<juce::juce_wchar>(rng.nextInt(95) + 32)) +
          s.substring(at + 1);
    }
    (void)rigshare::fromText(s, error);
  }
  SUCCEED();
}

// Processor level.

namespace {

juce::String fileUrl(const char* name) { return juce::URL(testFile(name)).toString(false); }

// A "resolved" catalog tone as the UI would hand it over, pointing at a
// test asset (local file URLs load without the network).
juce::String resolvedTone(int toneId, int modelId, const char* format, const char* file) {
  return juce::JSON::toString(parsedJson("{\"id\":" + juce::String(toneId) + ",\"title\":\"T" + juce::String(toneId) +
                                         "\",\"format\":\"" + format + "\",\"models\":[{\"id\":" + juce::String(modelId) +
                                         ",\"name\":\"m\",\"model_url\":\"" + fileUrl(file) + "\"}]}"));
}

}  // namespace

TEST(ProcessorTest, RigExportAndImportRebuildTheRig) {
  ChainTestProcessor a;
  // A catalog NAM (bytes embedded so it loads offline) and a local IR.
  juce::ValueTree snap("ChainSnapshot");
  juce::ValueTree laneTree("ChainBlocks");
  laneTree.appendChild(makeNamBlockTree("amp", 501, 5501), nullptr);
  snap.appendChild(laneTree, nullptr);
  a.restoreFromTree(snap);
  ASSERT_TRUE(waitForChainLoaded(a));
  const juce::var local = a.loadLocalTone("cab-ir-test", filesOf({testFileEntry("cab-ir-test.wav")}));
  ASSERT_TRUE(local["error"].isVoid());
  ASSERT_TRUE(waitForChainLoaded(a));
  a.parameters.getParameter("fxDelayEnabled")->setValueNotifyingHost(1.0f);
  a.parameters.getParameter("outputLevel")->setValueNotifyingHost(0.7f);

  const juce::var code = a.exportRig("Test", "", true);
  EXPECT_TRUE(code["text"].toString().startsWith(rigshare::kCodePrefix));
  EXPECT_EQ(code["missing"].size(), 1) << "the local IR can't travel in a code";
  const juce::var file = a.exportRig("Test", "", false);
  EXPECT_EQ(file["missing"].size(), 0);
  EXPECT_EQ(file["embedded"].size(), 1);

  const juce::var info = a.inspectRig(file["text"].toString());
  ASSERT_TRUE(static_cast<bool>(info["ok"]));
  ASSERT_EQ(info["toneIds"].size(), 1);
  EXPECT_EQ(static_cast<int>(info["toneIds"][0]), 501);

  ChainTestProcessor b;
  juce::DynamicObject::Ptr resolved = new juce::DynamicObject();
  resolved->setProperty("501", resolvedTone(501, 5501, "nam", "a2-amp-test.nam"));
  const juce::var applied = b.applyRig(file["text"].toString(), resolved.get());
  ASSERT_TRUE(static_cast<bool>(applied["ok"])) << applied["error"].toString();
  EXPECT_EQ(applied["missing"].size(), 0);
  ASSERT_TRUE(waitForChainLoaded(b));
  const juce::var state = b.getChainState(-1);
  int tones = 0;
  for (const auto& item : *state["chain"].getArray())
    if (item["kind"].toString() == "tone") {
      ++tones;
      EXPECT_TRUE(static_cast<bool>(item["loaded"]));
    }
  EXPECT_EQ(tones, 2);
  EXPECT_EQ(b.parameters.getParameter("fxDelayEnabled")->getValue(), 1.0f);
  EXPECT_NEAR(b.parameters.getParameter("outputLevel")->getValue(), 0.7f, 1e-3f);

  // From the code: the local IR is missing, and an unresolved library tone
  // is too (nothing is fetched from what the rig itself says).
  ChainTestProcessor c;
  const juce::var fromCode = c.applyRig(code["text"].toString(), juce::var(new juce::DynamicObject()));
  ASSERT_TRUE(static_cast<bool>(fromCode["ok"]));
  EXPECT_EQ(fromCode["missing"].size(), 2);
}

TEST(ProcessorTest, RigImportRejectsAFakeEmbeddedCapture) {
  rigshare::Rig rig;
  rig.name = "Fake";
  rigshare::Block nam;
  nam.type = "nam";
  nam.local = true;
  nam.title = "Not a model";
  rigshare::Embedded e;
  e.fileName = "x.nam";
  e.bytes.append("{\"this is\": \"not A2\"}", 21);
  nam.embedded = e;
  rig.left = {nam};
  TONE3000Processor proc;
  const juce::var applied = proc.applyRig(rigshare::toFileText(rig), {});
  ASSERT_TRUE(static_cast<bool>(applied["ok"]));
  EXPECT_EQ(applied["missing"].size(), 1);
  const juce::var state = proc.getChainState(-1);
  for (const auto& item : *state["chain"].getArray()) EXPECT_NE(item["kind"].toString(), "tone");
}
