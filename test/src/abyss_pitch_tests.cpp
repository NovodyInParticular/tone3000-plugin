// Abyss Pitch tests (AbyssPitch.h). These pin:
//
//   - off is bit-exact passthrough; silence in gives silence out with every
//     layer up; output stays finite and bounded at every rate from 44.1 to
//     192 kHz and every buffer from 32 to 2048,
//   - the dry path is the input delayed by exactly the reported latency,
//   - each layer lands where it should: -1 / -2 octaves, +1 octave, an
//     interval, a bend,
//   - a tracking change and switching off stay click-free and finite, and
//     off is exact again afterwards,
//   - processor level: defaults, state round trip, state from before the
//     effect lands on defaults, presets, latency reported only while on,
//     and moving / reordering the inserts without a click.
#include "AbyssPitch.h"
#include "GlitchFx.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <tuple>

namespace {

constexpr int kBlock = 256;

juce::AudioBuffer<float> stereoOf(const std::vector<float>& in) {
  juce::AudioBuffer<float> out(2, static_cast<int>(in.size()));
  for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, 0, in.data(), static_cast<int>(in.size()));
  return out;
}

void run(abyss::Engine& engine, juce::AudioBuffer<float>& buf, const abyss::Params& p, int block = kBlock) {
  for (int off = 0; off < buf.getNumSamples(); off += block) {
    const int n = std::min(block, buf.getNumSamples() - off);
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), off, n);
    engine.process(view, p);
  }
}

float peak(const juce::AudioBuffer<float>& b, int from = 0) {
  float m = 0.0f;
  for (int ch = 0; ch < b.getNumChannels(); ++ch)
    for (int i = from; i < b.getNumSamples(); ++i) m = std::max(m, std::abs(b.getSample(ch, i)));
  return m;
}

bool allFinite(const juce::AudioBuffer<float>& b) {
  for (int ch = 0; ch < b.getNumChannels(); ++ch)
    for (int i = 0; i < b.getNumSamples(); ++i)
      if (!std::isfinite(b.getSample(ch, i))) return false;
  return true;
}

abyss::Params wetOnly() {
  abyss::Params p;
  p.on = true;
  p.dry = 0.0f;
  p.subOneLevel = 0.0f;
  return p;
}

// Power at `freq` in the second half of channel 0.
double powerAt(const juce::AudioBuffer<float>& b, double freq) {
  const int n = b.getNumSamples();
  return goertzelPower(b.getReadPointer(0) + n / 2, static_cast<size_t>(n / 2), freq);
}

}  // namespace

TEST(AbyssPitchTest, OffIsBitExactPassthrough) {
  abyss::Engine engine;
  engine.prepare(kFs, kBlock);
  const auto in = stereoOf(makeSine(kBlock * 40, 220.0, 0.4f));
  auto buf = in;
  run(engine, buf, {});
  for (int i = 0; i < buf.getNumSamples(); ++i) ASSERT_EQ(buf.getSample(0, i), in.getSample(0, i));
}

TEST(AbyssPitchTest, SilenceInGivesSilenceOut) {
  for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
    abyss::Engine engine;
    engine.prepare(fs, 512);
    abyss::Params p;
    p.on = true;
    p.subTwoLevel = p.subOneLevel = p.upLevel = p.shiftLevel = 1.0f;
    p.detuneCents = 60.0f;
    p.interval = 3;
    juce::AudioBuffer<float> buf(2, static_cast<int>(fs * 0.5));
    buf.clear();
    run(engine, buf, p, 512);
    EXPECT_EQ(peak(buf), 0.0f) << fs;
  }
}

TEST(AbyssPitchTest, StaysFiniteAndBoundedAtEveryRateAndBlock) {
  for (const double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
    for (const int block : {32, 64, 128, 256, 512, 1024, 2048}) {
      abyss::Engine engine;
      engine.prepare(fs, block);
      abyss::Params p;
      p.on = true;
      p.subTwoLevel = p.subOneLevel = p.upLevel = p.shiftLevel = 1.0f;
      p.detuneCents = 40.0f;
      p.bend = 0.5f;
      p.glideMs = 0.0f;
      auto buf = stereoOf(makeSine(static_cast<int>(fs * 0.4), 110.0, 0.5f, fs));
      run(engine, buf, p, block);
      ASSERT_TRUE(allFinite(buf)) << fs << " / " << block;
      // Dry plus four layers at full: bounded by their sum.
      EXPECT_LT(peak(buf), 0.5f * 5.0f) << fs << " / " << block;
    }
  }
}

TEST(AbyssPitchTest, DryIsTheInputDelayedByTheReportedLatency) {
  for (const int tracking : {0, 1}) {
    abyss::Engine engine;
    engine.prepare(kFs, kBlock);
    abyss::Params p;
    p.on = true;
    p.subOneLevel = 0.0f;
    p.tracking = tracking;
    const auto in = makeSine(kBlock * 40, 330.0, 0.4f);
    auto buf = stereoOf(in);
    run(engine, buf, p);
    const int latency = abyss::latencySamples(tracking, kFs);
    EXPECT_EQ(latency, tracking == 0 ? 528 : 1008);
    for (int i = kBlock * 10; i < buf.getNumSamples(); i += 17)
      ASSERT_FLOAT_EQ(buf.getSample(0, i), in[static_cast<size_t>(i - latency)]) << i;
  }
}

TEST(AbyssPitchTest, LayersLandOnTheirPitches) {
  struct Case {
    const char* name;
    float abyss::Params::*level;
    int interval;
    float bend;
    double expect;
  };
  const Case cases[] = {
      {"-1 oct", &abyss::Params::subOneLevel, 0, 0.0f, 110.0},
      {"-2 oct", &abyss::Params::subTwoLevel, 0, 0.0f, 55.0},
      {"+1 oct", &abyss::Params::upLevel, 0, 0.0f, 440.0},
      {"tritone", &abyss::Params::shiftLevel, 3, 0.0f, 220.0 * std::pow(2.0, 6.0 / 12.0)},
      {"bend +5", &abyss::Params::shiftLevel, 0, 5.0f / 12.0f, 220.0 * std::pow(2.0, 5.0 / 12.0)},
  };
  for (const auto& c : cases) {
    abyss::Engine engine;
    engine.prepare(kFs, kBlock);
    auto p = wetOnly();
    p.*(c.level) = 1.0f;
    p.interval = c.interval;
    p.bend = c.bend;
    p.glideMs = 0.0f;
    auto buf = stereoOf(makeSine(static_cast<int>(kFs), 220.0, 0.4f));
    run(engine, buf, p);
    ASSERT_TRUE(allFinite(buf));
    const double target = powerAt(buf, c.expect);
    EXPECT_GT(target, 10.0 * powerAt(buf, 220.0)) << c.name;
    EXPECT_GT(target, 1e-3) << c.name;
  }
}

TEST(AbyssPitchTest, TrackingChangeAndSwitchOffStayClickFree) {
  abyss::Engine engine;
  engine.prepare(kFs, kBlock);
  abyss::Params p;
  p.on = true;
  const auto in = makeSine(kBlock * 300, 110.0, 0.3f);
  auto buf = stereoOf(in);
  float maxStep = 0.0f, prev = 0.0f;
  for (int b = 0; b < 300; ++b) {
    if (b == 100) p.tracking = 1;
    if (b == 200) p.on = false;
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, b * kBlock, kBlock);
    engine.process(view, p);
    const float* x = buf.getReadPointer(0) + b * kBlock;
    for (int i = 0; i < kBlock; ++i) {
      if (b > 30) maxStep = std::max(maxStep, std::abs(x[i] - prev));
      prev = x[i];
    }
  }
  ASSERT_TRUE(allFinite(buf));
  EXPECT_FALSE(engine.running());
  // A 110 Hz sine at 0.3 plus its octave below steps ~0.005 per sample.
  EXPECT_LT(maxStep, 0.06f);
  for (int i = kBlock * 260; i < buf.getNumSamples(); ++i) ASSERT_EQ(buf.getSample(0, i), in[static_cast<size_t>(i)]);
}

// Processor-level contracts.

namespace {

const char* const kAbyssIds[] = {"abyssEnabled", "abyssSubTwo",    "abyssSubOne",  "abyssUp",        "abyssShiftLevel",
                                 "abyssShift",   "abyssInterval",  "abyssDetune",  "abyssBend",      "abyssBendRange",
                                 "abyssGlide",   "abyssTracking",  "abyssDry",     "abyssWet",       "abyssOutput",
                                 "abyssPlacement", "insertOrder"};

float denorm(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

void setDenorm(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

}  // namespace

TEST(ProcessorTest, AbyssDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  for (const auto* id : kAbyssIds) ASSERT_NE(proc.parameters.getParameter(id), nullptr) << id;
  const abyss::Params d;
  EXPECT_EQ(denorm(proc, "abyssEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "abyssSubTwo"), d.subTwoLevel, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssSubOne"), d.subOneLevel, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssUp"), d.upLevel, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssShiftLevel"), d.shiftLevel, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssShift"), d.shiftSemitones, 1e-4f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "abyssInterval")), d.interval);
  EXPECT_NEAR(denorm(proc, "abyssDetune"), d.detuneCents, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssBend"), d.bend, 1e-4f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "abyssBendRange")), d.bendRange);
  EXPECT_NEAR(denorm(proc, "abyssGlide"), d.glideMs, 0.1f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "abyssTracking")), d.tracking);
  EXPECT_NEAR(denorm(proc, "abyssDry"), d.dry, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssWet"), d.wet, 1e-4f);
  EXPECT_NEAR(denorm(proc, "abyssOutput"), d.outputDb, 1e-4f);
  EXPECT_EQ(denorm(proc, "abyssPlacement"), 0.0f);  // pre-amp
  EXPECT_EQ(denorm(proc, "insertOrder"), 0.0f);     // Abyss first
}

TEST(ProcessorTest, AbyssSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    setDenorm(a, "abyssEnabled", 1.0f);
    setDenorm(a, "abyssSubTwo", 0.4f);
    setDenorm(a, "abyssInterval", 3.0f);
    setDenorm(a, "abyssBend", -0.5f);
    setDenorm(a, "abyssTracking", 1.0f);
    setDenorm(a, "abyssPlacement", 1.0f);
    setDenorm(a, "insertOrder", 1.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(denorm(b, "abyssEnabled"), 1.0f);
  EXPECT_NEAR(denorm(b, "abyssSubTwo"), 0.4f, 1e-3f);
  EXPECT_EQ(denorm(b, "abyssInterval"), 3.0f);
  EXPECT_NEAR(denorm(b, "abyssBend"), -0.5f, 1e-3f);
  EXPECT_EQ(denorm(b, "abyssTracking"), 1.0f);
  EXPECT_EQ(denorm(b, "abyssPlacement"), 1.0f);
  EXPECT_EQ(denorm(b, "insertOrder"), 1.0f);
}

TEST(ProcessorTest, StateFromBeforeAbyssLandsOnItsDefaults) {
  juce::MemoryBlock saved;
  {
    TONE3000Processor old;
    old.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(static_cast<const char*>(saved.getData()) + 4,
                                                       saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  juce::ValueTree params = tree.getChildWithName("PARAMETERS");
  for (const auto* id : kAbyssIds) {
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
  setDenorm(proc, "abyssEnabled", 1.0f);
  setDenorm(proc, "abyssUp", 0.9f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_EQ(denorm(proc, "abyssEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "abyssUp"), 0.0f, 1e-4f);
}

TEST(ProcessorTest, PresetsCarryAbyss) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-abyss-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    setDenorm(proc, "abyssEnabled", 1.0f);
    setDenorm(proc, "abyssDetune", 35.0f);
    setDenorm(proc, "abyssPlacement", 1.0f);
    const juce::var saved = proc.savePreset("Abyss");
    ASSERT_TRUE(saved.isObject());
    setDenorm(proc, "abyssEnabled", 0.0f);
    setDenorm(proc, "abyssDetune", 0.0f);
    setDenorm(proc, "abyssPlacement", 0.0f);
    ASSERT_TRUE(proc.loadPreset(saved["id"].toString()));
    EXPECT_EQ(denorm(proc, "abyssEnabled"), 1.0f);
    EXPECT_NEAR(denorm(proc, "abyssDetune"), 35.0f, 0.01f);
    EXPECT_EQ(denorm(proc, "abyssPlacement"), 1.0f);
  }
  tmp.deleteRecursively();
}

TEST(ProcessorTest, AbyssReportsItsLatencyOnlyWhileOn) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  pump();
  const int base = proc.getLatencySamples();
  setDenorm(proc, "abyssEnabled", 1.0f);
  pump();
  EXPECT_EQ(proc.getLatencySamples(), base + abyss::latencySamples(0, kFs));
  setDenorm(proc, "abyssTracking", 1.0f);
  pump();
  EXPECT_EQ(proc.getLatencySamples(), base + abyss::latencySamples(1, kFs));
  setDenorm(proc, "abyssEnabled", 0.0f);
  pump();
  EXPECT_EQ(proc.getLatencySamples(), base);
  proc.releaseResources();
}

namespace {
// The largest sample-to-sample step of the two inserts on a 110 Hz sine,
// with `moves` (block -> parameter, value) applied along the way.
float maxStepWith(const std::vector<std::tuple<int, const char*, float>>& moves) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenorm(proc, "abyssEnabled", 1.0f);
  setDenorm(proc, "glitchEnabled", 1.0f);
  setDenorm(proc, "glitchBits", 8.0f);
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto sine = makeSine(kBlock * 800, 110.0, 0.2f);
  float maxStep = 0.0f, prev = 0.0f;
  for (int b = 0; b < 800; ++b) {
    for (const auto& [at, id, value] : moves)
      if (b == at) setDenorm(proc, id, value);
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, sine.data() + b * kBlock, kBlock);
    proc.processBlock(buf, midi);
    const float* x = buf.getReadPointer(0);
    for (int i = 0; i < kBlock; ++i) {
      if (b > 40) maxStep = std::max(maxStep, std::abs(x[i] - prev));
      prev = x[i];
    }
    EXPECT_TRUE(allFinite(buf)) << b;
  }
  proc.releaseResources();
  return maxStep;
}
}  // namespace

TEST(ProcessorTest, InsertsMoveAndReorderWithoutAClick) {
  const float still = maxStepWith({});
  const float moving = maxStepWith({{200, "abyssPlacement", 1.0f},   // both post now
                                    {400, "insertOrder", 1.0f},      // glitch first
                                    {600, "glitchPlacement", 0.0f}});
  // Every move dips through silence instead of splicing two different
  // signals, so moving adds no step bigger than the effects already make.
  EXPECT_LE(moving, still * 1.25f + 0.005f) << "still " << still;
}

// CPU benchmark (not part of the suite): each insert's cost as a percent
// of one core in real time, 48 kHz, 128-sample blocks, stereo.
//   DspTests --gtest_also_run_disabled_tests --gtest_filter='*InsertCpu*'
TEST(InsertBench, DISABLED_InsertCpu) {
  constexpr int block = 128;
  const int seconds = 10;
  const auto sine = makeSine(block * 375, 110.0, 0.3f);  // one second
  const auto measure = [&](const char* name, auto&& process) {
    juce::AudioBuffer<float> buf(2, block);
    const auto start = juce::Time::getHighResolutionTicks();
    for (int s = 0; s < seconds; ++s)
      for (int b = 0; b < 375; ++b) {
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, sine.data() + b * block, block);
        process(buf);
      }
    const double elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
    std::printf("%-44s %6.2f %% of one core\n", name, 100.0 * elapsed / seconds);
  };
  {
    glitchfx::Engine g;
    g.prepare(kFs, block);
    measure("Glitch off", [&](auto& b) { g.process(b, {}, {}); });
    glitchfx::Params p;
    p.on = true;
    measure("Glitch on, neutral", [&](auto& b) { g.process(b, p, {}); });
    p.bits = 4.0f;
    p.rateHz = 6000.0f;
    p.chance = 0.6f;
    p.intensity = 1.0f;
    p.freeze = true;
    glitchfx::Transport t;
    t.playing = true;
    measure("Glitch on, crush + random + freeze", [&](auto& b) {
      g.process(b, p, t);
      t.ppq += block * 2.0 / kFs;
    });
  }
  {
    abyss::Engine a;
    a.prepare(kFs, block);
    measure("Abyss off", [&](auto& b) { a.process(b, {}); });
    abyss::Params p;
    p.on = true;  // the default: -1 octave
    measure("Abyss on, default (-1 oct, Tight)", [&](auto& b) { a.process(b, p); });
    p.subTwoLevel = p.upLevel = p.shiftLevel = 0.8f;
    p.detuneCents = 30.0f;
    measure("Abyss on, all five voices (Tight)", [&](auto& b) { a.process(b, p); });
    p.tracking = 1;
    measure("Abyss on, all five voices (Smooth)", [&](auto& b) { a.process(b, p); });
  }
}
