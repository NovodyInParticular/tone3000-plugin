// Glitch Engine tests (GlitchFx.h). These pin:
//
//   - off is bit-exact passthrough, and so is on with every section neutral,
//   - silence in gives silence out with everything engaged, and nothing goes
//     non-finite or louder than the safety ceiling (1.4x the input peak) at
//     every rate from 44.1 to 192 kHz and every buffer from 32 to 2048,
//   - a non-finite input sample doesn't poison the engine,
//   - a stutter repeats the slice it caught, with every repeat the same,
//   - a seed replays the same random pattern at the same song position, a
//     different seed doesn't,
//   - freeze keeps sounding after the input stops and fades out when
//     released; bit-crush quantises and rate reduction holds samples,
//   - switching off fades, then the engine is exact again,
//   - processor level: defaults, state round trip, state from before the
//     effect lands on defaults, presets, both placements, MIDI hold targets,
//     no latency, and a placement move without a click.
#include "GlitchFx.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <set>

namespace {

constexpr int kBlock = 256;

juce::AudioBuffer<float> stereoOf(const std::vector<float>& in) {
  juce::AudioBuffer<float> out(2, static_cast<int>(in.size()));
  for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, 0, in.data(), static_cast<int>(in.size()));
  return out;
}

std::vector<float> noise(int frames, float amplitude, std::uint32_t seed = 1) {
  std::vector<float> out(static_cast<size_t>(frames));
  for (auto& v : out) {
    seed = seed * 1664525u + 1013904223u;
    v = amplitude * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
  }
  return out;
}

void run(glitchfx::Engine& engine, juce::AudioBuffer<float>& buf, const glitchfx::Params& p, int block = kBlock,
         glitchfx::Transport t = {}, double fs = kFs) {
  for (int off = 0; off < buf.getNumSamples(); off += block) {
    const int n = std::min(block, buf.getNumSamples() - off);
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), off, n);
    engine.process(view, p, t);
    if (t.playing) t.ppq += n * t.bpm / 60.0 / fs;
  }
}

double rms(const float* x, int from, int to) {
  double acc = 0.0;
  for (int i = from; i < to; ++i) acc += static_cast<double>(x[i]) * x[i];
  return std::sqrt(acc / std::max(1, to - from));
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

bool sameAs(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
  for (int ch = 0; ch < a.getNumChannels(); ++ch)
    for (int i = 0; i < a.getNumSamples(); ++i)
      if (a.getSample(ch, i) != b.getSample(ch, i)) return false;
  return true;
}

glitchfx::Params everything() {
  glitchfx::Params p;
  p.on = true;
  p.stutter = true;
  p.rate = 4;
  p.length = 0.5f;
  p.bits = 3.0f;
  p.rateHz = 2000.0f;
  p.chance = 1.0f;
  p.intensity = 1.0f;
  p.seed = 3;
  p.freeze = true;
  p.freezeFadeMs = 20.0f;
  return p;
}

}  // namespace

TEST(GlitchFxTest, OffIsBitExactPassthrough) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  const auto in = stereoOf(noise(kBlock * 40, 0.5f));
  auto buf = in;
  run(engine, buf, {});
  EXPECT_TRUE(sameAs(buf, in));
}

TEST(GlitchFxTest, NeutralSettingsAreBitExactWhileOn) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  glitchfx::Params p;
  p.on = true;
  const auto in = stereoOf(noise(kBlock * 40, 0.5f));
  auto buf = in;
  run(engine, buf, p);
  EXPECT_TRUE(engine.running());
  EXPECT_TRUE(sameAs(buf, in));
}

TEST(GlitchFxTest, SilenceInGivesSilenceOutWithEverythingOn) {
  for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
    glitchfx::Engine engine;
    engine.prepare(fs, 512);
    juce::AudioBuffer<float> buf(2, static_cast<int>(fs));  // one second
    buf.clear();
    glitchfx::Transport t;
    t.playing = true;
    run(engine, buf, everything(), 512, t, fs);
    EXPECT_EQ(peak(buf), 0.0f) << fs;
  }
}

TEST(GlitchFxTest, StaysFiniteAndUnderTheCeilingAtEveryRateAndBlock) {
  for (const double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
    for (const int block : {32, 64, 128, 256, 512, 1024, 2048}) {
      glitchfx::Engine engine;
      engine.prepare(fs, block);
      const int n = static_cast<int>(fs * 0.75);
      auto buf = stereoOf(noise(n, 0.5f, static_cast<std::uint32_t>(block)));
      const float inPeak = peak(buf);
      auto p = everything();
      p.stutter = false;  // let the random glitches run
      glitchfx::Transport t;
      t.playing = true;
      t.bpm = 174.0;
      run(engine, buf, p, block, t, fs);
      ASSERT_TRUE(allFinite(buf)) << fs << " / " << block;
      EXPECT_LE(peak(buf), inPeak * 1.41f) << fs << " / " << block;
    }
  }
}

TEST(GlitchFxTest, NonFiniteInputDoesNotPoisonTheEngine) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  auto buf = stereoOf(makeSine(kBlock * 80, 220.0, 0.3f));
  buf.setSample(0, 1000, std::numeric_limits<float>::quiet_NaN());
  buf.setSample(1, 1001, std::numeric_limits<float>::infinity());
  auto p = everything();
  p.freeze = false;
  p.stutter = false;  // engaged at t = 0 it would loop silence
  run(engine, buf, p);
  ASSERT_TRUE(allFinite(buf));
  EXPECT_GT(rms(buf.getReadPointer(0), kBlock * 60, kBlock * 80), 0.01);
}

TEST(GlitchFxTest, StutterRepeatsTheSliceItCaught) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  glitchfx::Params p;
  p.on = true;
  p.rate = 2;  // 1/16 at 120 BPM: 6000 samples
  const int step = 6000;
  const auto in = noise(kBlock * 200, 0.4f);
  auto buf = stereoOf(in);
  const int engageBlock = 60;
  for (int b = 0; b < 200; ++b) {
    p.stutter = b >= engageBlock;
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, b * kBlock, kBlock);
    engine.process(view, p, {});
  }
  const int start = engageBlock * kBlock;
  const float* out = buf.getReadPointer(0);
  // Away from the edges, each repeat is the slice that preceded the engage.
  for (int k = 200; k < step - 200; k += 37) {
    EXPECT_FLOAT_EQ(out[start + step + k], in[static_cast<size_t>(start - step + k)]) << k;
    EXPECT_FLOAT_EQ(out[start + 2 * step + k], out[start + step + k]) << k;
  }
}

TEST(GlitchFxTest, ASeedReplaysTheSamePatternAndAnotherSeedDoesNot) {
  const auto render = [](int seed) {
    glitchfx::Engine engine;
    engine.prepare(kFs, kBlock);
    glitchfx::Params p;
    p.on = true;
    p.chance = 0.5f;
    p.intensity = 0.8f;
    p.seed = seed;
    p.rate = 3;
    glitchfx::Transport t;
    t.playing = true;
    t.bpm = 120.0;
    auto buf = stereoOf(noise(kBlock * 300, 0.4f));
    run(engine, buf, p, kBlock, t);
    return buf;
  };
  const auto a = render(7), b = render(7), c = render(8);
  EXPECT_TRUE(sameAs(a, b));
  EXPECT_FALSE(sameAs(a, c));
  // And something actually happened.
  EXPECT_FALSE(sameAs(a, stereoOf(noise(kBlock * 300, 0.4f))));
}

TEST(GlitchFxTest, FreezeHoldsAfterTheInputStopsThenFadesOut) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  glitchfx::Params p;
  p.on = true;
  p.freezeFadeMs = 50.0f;
  const auto sine = makeSine(kBlock * 100, 220.0, 0.4f);
  auto buf = stereoOf(sine);
  run(engine, buf, p);  // fill the ring
  p.freeze = true;
  juce::AudioBuffer<float> held(2, static_cast<int>(kFs * 2));
  held.clear();
  run(engine, held, p);
  EXPECT_GT(rms(held.getReadPointer(0), held.getNumSamples() / 2, held.getNumSamples()), 0.1);
  EXPECT_LE(peak(held), 0.4f * 1.41f);
  p.freeze = false;
  juce::AudioBuffer<float> after(2, static_cast<int>(kFs * 0.5));
  after.clear();
  run(engine, after, p);
  EXPECT_EQ(peak(after, static_cast<int>(kFs * 0.1)), 0.0f);
}

TEST(GlitchFxTest, BitCrushQuantisesAndRateReductionHolds) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  glitchfx::Params p;
  p.on = true;
  p.bits = 2.0f;  // steps of 0.5
  auto buf = stereoOf(makeSine(kBlock * 40, 220.0, 0.6f));
  run(engine, buf, p);
  std::set<float> levels;
  for (int i = kBlock * 20; i < buf.getNumSamples(); ++i) levels.insert(buf.getSample(0, i));
  EXPECT_LE(levels.size(), 5u);

  glitchfx::Engine down;
  down.prepare(kFs, kBlock);
  glitchfx::Params q;
  q.on = true;
  q.rateHz = 1000.0f;  // one new value every 48 samples
  auto held = stereoOf(makeSine(kBlock * 40, 220.0, 0.6f));
  run(down, held, q);
  int changes = 0;
  const float* x = held.getReadPointer(0);
  for (int i = kBlock * 20 + 1; i < held.getNumSamples(); ++i) changes += x[i] != x[i - 1] ? 1 : 0;
  const int span = held.getNumSamples() - kBlock * 20;
  EXPECT_NEAR(changes, span / 48, span / 48 / 10 + 2);
}

TEST(GlitchFxTest, SwitchingOffFadesThenGoesExact) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  auto p = everything();
  auto buf = stereoOf(noise(kBlock * 40, 0.4f));
  run(engine, buf, p);
  p.on = false;
  const auto in = stereoOf(noise(kBlock * 40, 0.4f, 9));
  auto off = in;
  run(engine, off, p);
  EXPECT_FALSE(engine.running());
  juce::AudioBuffer<float> tail(2, kBlock * 20);
  juce::AudioBuffer<float> inTail(2, kBlock * 20);
  for (int ch = 0; ch < 2; ++ch) {
    tail.copyFrom(ch, 0, off, ch, kBlock * 20, kBlock * 20);
    inTail.copyFrom(ch, 0, in, ch, kBlock * 20, kBlock * 20);
  }
  EXPECT_TRUE(sameAs(tail, inTail));
}

TEST(GlitchFxTest, MonoBufferWorks) {
  glitchfx::Engine engine;
  engine.prepare(kFs, kBlock);
  const auto in = noise(kBlock * 60, 0.4f);
  juce::AudioBuffer<float> buf(1, static_cast<int>(in.size()));
  buf.copyFrom(0, 0, in.data(), static_cast<int>(in.size()));
  run(engine, buf, everything());
  EXPECT_TRUE(allFinite(buf));
}

// Processor-level contracts.

namespace {

const char* const kGlitchIds[] = {"glitchEnabled",   "glitchMix",       "glitchOutput",     "glitchStutter",
                                  "glitchStutterRate", "glitchStutterLength", "glitchBits",  "glitchRate",
                                  "glitchChance",    "glitchIntensity", "glitchSeed",       "glitchFreeze",
                                  "glitchFreezeFade", "glitchFreezeMode", "glitchStutterMode", "glitchPlacement"};

float denorm(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

void setDenorm(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

}  // namespace

TEST(ProcessorTest, GlitchDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  for (const auto* id : kGlitchIds) ASSERT_NE(proc.parameters.getParameter(id), nullptr) << id;
  ASSERT_NE(proc.parameters.getParameter("standaloneBpm"), nullptr);
  const glitchfx::Params d;
  EXPECT_EQ(denorm(proc, "glitchEnabled"), 0.0f);
  EXPECT_EQ(denorm(proc, "glitchStutter"), 0.0f);
  EXPECT_EQ(denorm(proc, "glitchFreeze"), 0.0f);
  EXPECT_NEAR(denorm(proc, "glitchMix"), d.mix, 1e-4f);
  EXPECT_NEAR(denorm(proc, "glitchOutput"), d.outputDb, 1e-4f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "glitchStutterRate")), d.rate);
  EXPECT_NEAR(denorm(proc, "glitchStutterLength"), d.length, 1e-4f);
  EXPECT_NEAR(denorm(proc, "glitchBits"), d.bits, 1e-3f);
  EXPECT_NEAR(denorm(proc, "glitchRate"), d.rateHz, 1.0f);
  EXPECT_NEAR(denorm(proc, "glitchChance"), d.chance, 1e-4f);
  EXPECT_NEAR(denorm(proc, "glitchIntensity"), d.intensity, 1e-4f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "glitchSeed")), d.seed);
  EXPECT_NEAR(denorm(proc, "glitchFreezeFade"), d.freezeFadeMs, 0.5f);
  EXPECT_EQ(denorm(proc, "glitchPlacement"), 1.0f);  // post-amp
  EXPECT_NEAR(denorm(proc, "standaloneBpm"), 120.0f, 1e-3f);
}

TEST(ProcessorTest, GlitchSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    setDenorm(a, "glitchEnabled", 1.0f);
    setDenorm(a, "glitchStutterRate", 7.0f);
    setDenorm(a, "glitchBits", 4.5f);
    setDenorm(a, "glitchSeed", 42.0f);
    setDenorm(a, "glitchPlacement", 0.0f);
    setDenorm(a, "glitchFreezeMode", 1.0f);
    setDenorm(a, "standaloneBpm", 97.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(denorm(b, "glitchEnabled"), 1.0f);
  EXPECT_EQ(denorm(b, "glitchStutterRate"), 7.0f);
  EXPECT_NEAR(denorm(b, "glitchBits"), 4.5f, 0.01f);
  EXPECT_EQ(denorm(b, "glitchSeed"), 42.0f);
  EXPECT_EQ(denorm(b, "glitchPlacement"), 0.0f);
  EXPECT_EQ(denorm(b, "glitchFreezeMode"), 1.0f);
  EXPECT_NEAR(denorm(b, "standaloneBpm"), 97.0f, 0.1f);
}

TEST(ProcessorTest, StateFromBeforeGlitchLandsOnItsDefaults) {
  juce::MemoryBlock saved;
  {
    TONE3000Processor old;
    old.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(static_cast<const char*>(saved.getData()) + 4,
                                                       saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  juce::ValueTree params = tree.getChildWithName("PARAMETERS");
  for (const auto* id : kGlitchIds) {
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
  setDenorm(proc, "glitchEnabled", 1.0f);
  setDenorm(proc, "glitchChance", 0.9f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_EQ(denorm(proc, "glitchEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "glitchChance"), 0.0f, 1e-4f);
}

TEST(ProcessorTest, PresetsCarryGlitch) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-glitch-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    setDenorm(proc, "glitchEnabled", 1.0f);
    setDenorm(proc, "glitchChance", 0.7f);
    setDenorm(proc, "glitchPlacement", 0.0f);
    const juce::var saved = proc.savePreset("Broken");
    ASSERT_TRUE(saved.isObject());
    setDenorm(proc, "glitchEnabled", 0.0f);
    setDenorm(proc, "glitchChance", 0.1f);
    setDenorm(proc, "glitchPlacement", 1.0f);
    ASSERT_TRUE(proc.loadPreset(saved["id"].toString()));
    EXPECT_EQ(denorm(proc, "glitchEnabled"), 1.0f);
    EXPECT_NEAR(denorm(proc, "glitchChance"), 0.7f, 1e-3f);
    EXPECT_EQ(denorm(proc, "glitchPlacement"), 0.0f);
  }
  tmp.deleteRecursively();
}

TEST(ProcessorTest, GlitchAddsNoLatency) {
  TONE3000Processor proc;
  const int latency = proc.getLatencySamples();
  setDenorm(proc, "glitchEnabled", 1.0f);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  EXPECT_EQ(proc.getLatencySamples(), latency);
}

TEST(ProcessorTest, GlitchRunsInEitherPlacementAndMovesWithoutAClick) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenorm(proc, "glitchEnabled", 1.0f);
  setDenorm(proc, "glitchStutter", 1.0f);
  setDenorm(proc, "glitchStutterLength", 0.3f);  // gated: obviously not the dry signal
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto sine = makeSine(kBlock * 600, 110.0, 0.2f);
  float maxStep = 0.0f, prev = 0.0f;
  double gatedSilence = 0.0;
  for (int b = 0; b < 600; ++b) {
    if (b == 300) setDenorm(proc, "glitchPlacement", 0.0f);
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, sine.data() + b * kBlock, kBlock);
    proc.processBlock(buf, midi);
    const float* x = buf.getReadPointer(0);
    for (int i = 0; i < kBlock; ++i) {
      if (b > 20) maxStep = std::max(maxStep, std::abs(x[i] - prev));
      prev = x[i];
    }
    if (b > 100 && b < 300) gatedSilence += rms(x, 0, kBlock) < 1e-6 ? 1.0 : 0.0;
  }
  proc.releaseResources();
  ASSERT_TRUE(allFinite(buf));
  EXPECT_GT(gatedSilence, 10.0) << "the gated stutter's rests reach the output";
  // A 110 Hz sine at 0.2 moves at most ~0.003 per sample; the stutter's
  // edge fades and the placement dip keep every step well under a click.
  EXPECT_LT(maxStep, 0.05f);
}

TEST(MidiMapperTest, HoldTargetsAreMomentary) {
  TONE3000Processor proc;
  ASSERT_TRUE(proc.midiMapper.setCcMapping("glitchFreezeHold", 64));
  ASSERT_TRUE(proc.midiMapper.setCcMapping("glitchStutter", 65));
  const auto cc = [](int number, int value) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, number, value), 0);
    return midi;
  };
  const auto raw = [&](const char* id) { return proc.parameters.getRawParameterValue(id)->load(); };
  proc.midiMapper.processMidi(cc(64, 127));
  EXPECT_EQ(raw("glitchFreeze"), 1.0f);
  proc.midiMapper.processMidi(cc(64, 127));  // still held
  EXPECT_EQ(raw("glitchFreeze"), 1.0f);
  proc.midiMapper.processMidi(cc(64, 0));
  EXPECT_EQ(raw("glitchFreeze"), 0.0f);
  // The latch target toggles per press instead.
  proc.midiMapper.processMidi(cc(65, 127));
  proc.midiMapper.processMidi(cc(65, 0));
  EXPECT_EQ(raw("glitchStutter"), 1.0f);

  // Notes: on holds, off releases.
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  proc.midiMapper.startLearn("glitchStutterHold");
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  juce::MidiBuffer on, off;
  on.addEvent(juce::MidiMessage::noteOn(1, 40, static_cast<juce::uint8>(100)), 0);
  off.addEvent(juce::MidiMessage::noteOff(1, 40), 0);
  proc.midiMapper.processMidi(on);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  proc.parameters.getParameter("glitchStutter")->setValueNotifyingHost(0.0f);
  proc.midiMapper.processMidi(on);
  EXPECT_EQ(raw("glitchStutter"), 1.0f);
  proc.midiMapper.processMidi(off);
  EXPECT_EQ(raw("glitchStutter"), 0.0f);

  // Round trip through state keeps the hold target.
  const auto tree = proc.midiMapper.toValueTree();
  TONE3000Processor other;
  other.midiMapper.restoreFromValueTree(tree);
  const auto state = other.midiMapper.getState();
  bool found = false;
  for (const auto& m : *state["mappings"].getArray()) found = found || m["targetId"] == "glitchFreezeHold";
  EXPECT_TRUE(found);
}
