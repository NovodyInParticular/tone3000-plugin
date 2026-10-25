// Clouds tests (CloudsFx.h). These pin:
//
//   - off is bit-exact passthrough, and so is Blend at 0 while on (the dry
//     path never leaves the host rate),
//   - the looping delay replays the input at about the dry level, so the
//     32 kHz round trip neither loses nor boosts the wet path,
//   - granular mode makes sound from a held note, and freeze keeps
//     replaying the buffer after the input stops,
//   - every mode and quality at every common host rate stays finite and
//     bounded with feedback and reverb at full,
//   - switching off fades the wet path out, then the effect is exact again;
//     mono buffers work,
//   - processor level: every new parameter defaults off / to the DSP's
//     defaults, survives a state round trip, lands on its default from a
//     state saved before it existed, rides presets, extends the host tail
//     and adds no latency.
#include "CloudsFx.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr int kBlock = 256;

juce::AudioBuffer<float> stereoOf(const std::vector<float>& in) {
  juce::AudioBuffer<float> out(2, static_cast<int>(in.size()));
  for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, 0, in.data(), static_cast<int>(in.size()));
  return out;
}

void run(cloudsfx::Engine& engine, juce::AudioBuffer<float>& buf, const cloudsfx::Params& p, int block = kBlock) {
  for (int off = 0; off < buf.getNumSamples(); off += block) {
    const int n = std::min(block, buf.getNumSamples() - off);
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), off, n);
    engine.process(view, p);
  }
}

double rms(const float* x, int from, int to) {
  double acc = 0.0;
  for (int i = from; i < to; ++i) acc += static_cast<double>(x[i]) * x[i];
  return std::sqrt(acc / std::max(1, to - from));
}

float peak(const juce::AudioBuffer<float>& b) {
  float m = 0.0f;
  for (int ch = 0; ch < b.getNumChannels(); ++ch)
    for (int i = 0; i < b.getNumSamples(); ++i) m = std::max(m, std::abs(b.getSample(ch, i)));
  return m;
}

bool allFinite(const juce::AudioBuffer<float>& b) {
  for (int ch = 0; ch < b.getNumChannels(); ++ch)
    for (int i = 0; i < b.getNumSamples(); ++i)
      if (!std::isfinite(b.getSample(ch, i))) return false;
  return true;
}

cloudsfx::Params looper() {
  cloudsfx::Params p;
  p.on = true;
  p.mode = 2;  // looping delay
  p.blend = 1.0f;
  p.density = 0.0f;  // no diffusion
  p.texture = 0.5f;  // filters open
  p.feedback = 0.0f;
  p.reverb = 0.0f;
  p.position = 0.2f;
  p.size = 0.5f;
  return p;
}

}  // namespace

TEST(CloudsFxTest, OffIsBitExactPassthrough) {
  cloudsfx::Engine engine;
  engine.prepare(kFs, kBlock);
  const auto in = makeNoise(kBlock * 8, 7, 0.5f);
  auto buf = stereoOf(in);
  run(engine, buf, cloudsfx::Params{});
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < buf.getNumSamples(); ++i) ASSERT_EQ(buf.getSample(ch, i), in[static_cast<size_t>(i)]);
  EXPECT_FALSE(engine.running());
}

TEST(CloudsFxTest, BlendAtZeroIsBitExactWhileOn) {
  cloudsfx::Engine engine;
  engine.prepare(kFs, kBlock);
  cloudsfx::Params p;
  p.on = true;
  p.blend = 0.0f;
  const auto in = makeNoise(kBlock * 16, 3, 0.5f);
  auto buf = stereoOf(in);
  run(engine, buf, p);
  for (int i = 0; i < buf.getNumSamples(); ++i) ASSERT_EQ(buf.getSample(0, i), in[static_cast<size_t>(i)]);
  EXPECT_TRUE(engine.running());
}

TEST(CloudsFxTest, LoopingDelayReplaysAtAboutTheDryLevel) {
  for (const double rate : {44100.0, 48000.0, 96000.0}) {
    cloudsfx::Engine engine;
    engine.prepare(rate, kBlock);
    const int n = static_cast<int>(rate * 3.0);
    const auto in = makeSine(n, 220.0, 0.3f, rate);
    auto buf = stereoOf(in);
    run(engine, buf, looper());
    const double dry = rms(in.data(), n / 2, n);
    const double wet = rms(buf.getReadPointer(0), n / 2, n);
    EXPECT_GT(wet, dry * 0.5) << rate;
    EXPECT_LT(wet, dry * 2.0) << rate;
  }
}

TEST(CloudsFxTest, GranularMakesSoundAndFreezeHoldsIt) {
  cloudsfx::Engine engine;
  engine.prepare(kFs, kBlock);
  cloudsfx::Params p;
  p.on = true;
  p.blend = 1.0f;
  p.reverb = 0.0f;
  p.density = 0.8f;
  const int n = static_cast<int>(kFs * 2.0);
  auto buf = stereoOf(makeSine(n, 330.0, 0.3f));
  run(engine, buf, p);
  EXPECT_GT(rms(buf.getReadPointer(0), n / 2, n), 0.01);

  // Freeze, then silence in: the frozen buffer keeps playing.
  p.freeze = true;
  juce::AudioBuffer<float> silence(2, n);
  silence.clear();
  run(engine, silence, p);
  EXPECT_GT(rms(silence.getReadPointer(0), n / 2, n), 0.01);
}

TEST(CloudsFxTest, EveryModeQualityAndRateStaysBounded) {
  for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    for (int mode = 0; mode < 4; ++mode)
      for (int quality = 0; quality < 4; ++quality) {
        cloudsfx::Engine engine;
        engine.prepare(rate, 512);
        cloudsfx::Params p;
        p.on = true;
        p.mode = mode;
        p.quality = quality;
        p.blend = 1.0f;
        p.feedback = 1.0f;
        p.reverb = 1.0f;
        p.density = 1.0f;
        p.texture = 1.0f;
        p.pitch = 24.0f;
        auto buf = stereoOf(makeNoise(static_cast<int>(rate * 0.5), 11, 1.0f));
        run(engine, buf, p, 512);
        ASSERT_TRUE(allFinite(buf)) << rate << " " << mode << " " << quality;
        EXPECT_LT(peak(buf), 4.0f) << rate << " " << mode << " " << quality;
      }
}

TEST(CloudsFxTest, SwitchingOffFadesThenGoesExact) {
  cloudsfx::Engine engine;
  engine.prepare(kFs, kBlock);
  auto p = looper();
  auto warm = stereoOf(makeSine(static_cast<int>(kFs), 220.0, 0.3f));
  run(engine, warm, p);

  p.on = false;
  const auto in = makeNoise(kBlock * 8, 5, 0.2f);
  auto buf = stereoOf(in);
  run(engine, buf, p);
  EXPECT_FALSE(engine.running());
  // The 20 ms fade is over well before the last block, which is exact.
  for (int i = kBlock * 7; i < kBlock * 8; ++i) ASSERT_EQ(buf.getSample(0, i), in[static_cast<size_t>(i)]);
  // No step at the switch: the first sample moves by the fade's first step only.
  EXPECT_LT(std::abs(buf.getSample(0, 0) - warm.getSample(0, warm.getNumSamples() - 1)), 0.2f);
}

TEST(CloudsFxTest, MonoBufferWorks) {
  cloudsfx::Engine engine;
  engine.prepare(kFs, kBlock);
  const int n = static_cast<int>(kFs * 2.0);
  const auto in = makeSine(n, 220.0, 0.3f);
  juce::AudioBuffer<float> buf(1, n);
  buf.copyFrom(0, 0, in.data(), n);
  run(engine, buf, looper());
  ASSERT_TRUE(allFinite(buf));
  EXPECT_GT(rms(buf.getReadPointer(0), n / 2, n), 0.05);
}

// Processor-level contracts.

namespace {

const char* const kCloudsIds[] = {"cloudsEnabled", "cloudsMode",    "cloudsQuality", "cloudsFreeze",  "cloudsPosition",
                                  "cloudsSize",    "cloudsPitch",   "cloudsDensity", "cloudsTexture", "cloudsBlend",
                                  "cloudsSpread",  "cloudsFeedback", "cloudsReverb"};

float denorm(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

void setDenorm(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

}  // namespace

TEST(ProcessorTest, CloudsDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  for (const auto* id : kCloudsIds) ASSERT_NE(proc.parameters.getParameter(id), nullptr) << id;
  const cloudsfx::Params d;
  EXPECT_EQ(denorm(proc, "cloudsEnabled"), 0.0f);
  EXPECT_EQ(denorm(proc, "cloudsFreeze"), 0.0f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "cloudsMode")), d.mode);
  EXPECT_EQ(static_cast<int>(denorm(proc, "cloudsQuality")), d.quality);
  EXPECT_NEAR(denorm(proc, "cloudsPosition"), d.position, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsSize"), d.size, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsPitch"), d.pitch, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsDensity"), d.density, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsTexture"), d.texture, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsBlend"), d.blend, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsSpread"), d.spread, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsFeedback"), d.feedback, 1e-4f);
  EXPECT_NEAR(denorm(proc, "cloudsReverb"), d.reverb, 1e-4f);
}

TEST(ProcessorTest, CloudsSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    setDenorm(a, "cloudsEnabled", 1.0f);
    setDenorm(a, "cloudsMode", 3.0f);
    setDenorm(a, "cloudsQuality", 2.0f);
    setDenorm(a, "cloudsPitch", -7.0f);
    setDenorm(a, "cloudsTexture", 0.8f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(denorm(b, "cloudsEnabled"), 1.0f);
  EXPECT_EQ(denorm(b, "cloudsMode"), 3.0f);
  EXPECT_EQ(denorm(b, "cloudsQuality"), 2.0f);
  EXPECT_NEAR(denorm(b, "cloudsPitch"), -7.0f, 0.01f);
  EXPECT_NEAR(denorm(b, "cloudsTexture"), 0.8f, 1e-3f);
}

TEST(ProcessorTest, StateFromBeforeCloudsLandsOnItsDefaults) {
  juce::MemoryBlock saved;
  {
    TONE3000Processor old;
    old.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(static_cast<const char*>(saved.getData()) + 4,
                                                       saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  juce::ValueTree params = tree.getChildWithName("PARAMETERS");
  for (const auto* id : kCloudsIds) {
    const auto child = params.getChildWithProperty("id", id);
    ASSERT_TRUE(child.isValid()) << id;
    params.removeChild(child, nullptr);
  }
  juce::MemoryBlock reframed;
  {
    juce::MemoryOutputStream out(reframed, false);
    out.write("T3KB", 4);
    tree.writeToStream(out);
  }
  TONE3000Processor proc;
  setDenorm(proc, "cloudsEnabled", 1.0f);
  setDenorm(proc, "cloudsBlend", 0.9f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_EQ(denorm(proc, "cloudsEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "cloudsBlend"), 0.5f, 1e-3f);
}

TEST(ProcessorTest, PresetsCarryClouds) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-clouds-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    setDenorm(proc, "cloudsEnabled", 1.0f);
    setDenorm(proc, "cloudsDensity", 0.9f);
    setDenorm(proc, "cloudsMode", 2.0f);
    const juce::var saved = proc.savePreset("Texture");
    ASSERT_TRUE(saved.isObject());
    setDenorm(proc, "cloudsEnabled", 0.0f);
    setDenorm(proc, "cloudsDensity", 0.1f);
    setDenorm(proc, "cloudsMode", 0.0f);
    ASSERT_TRUE(proc.loadPreset(saved["id"].toString()));
    EXPECT_EQ(denorm(proc, "cloudsEnabled"), 1.0f);
    EXPECT_NEAR(denorm(proc, "cloudsDensity"), 0.9f, 1e-3f);
    EXPECT_EQ(denorm(proc, "cloudsMode"), 2.0f);
  }
  tmp.deleteRecursively();
}

TEST(ProcessorTest, CloudsExtendsTheTailAndAddsNoLatency) {
  TONE3000Processor proc;
  const double base = proc.getTailLengthSeconds();
  const int latency = proc.getLatencySamples();
  setDenorm(proc, "cloudsEnabled", 1.0f);
  EXPECT_GE(proc.getTailLengthSeconds(), 8.0);
  EXPECT_EQ(proc.getLatencySamples(), latency);
  setDenorm(proc, "cloudsEnabled", 0.0f);
  EXPECT_DOUBLE_EQ(proc.getTailLengthSeconds(), base);
}

TEST(ProcessorTest, CloudsRunsInProcessBlock) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenorm(proc, "cloudsEnabled", 1.0f);
  setDenorm(proc, "cloudsMode", 2.0f);
  setDenorm(proc, "cloudsBlend", 1.0f);
  setDenorm(proc, "cloudsReverb", 0.0f);
  setDenorm(proc, "cloudsDensity", 0.0f);
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto sine = makeSine(kBlock * 400, 220.0, 0.3f);
  double last = 0.0;
  for (int b = 0; b < 400; ++b) {
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, sine.data() + b * kBlock, kBlock);
    proc.processBlock(buf, midi);
    if (b >= 300) last = std::max(last, rms(buf.getReadPointer(0), 0, kBlock));
  }
  proc.releaseResources();
  EXPECT_GT(last, 0.02) << "the looping delay's wet signal reaches the output";
}
