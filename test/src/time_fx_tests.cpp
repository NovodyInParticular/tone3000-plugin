// Time effects rack tests (TimeFx.h). These pin:
//
//   - with every effect off the rack is bit-exact passthrough,
//   - the delay's first repeat lands exactly one delay time after the
//     input at the mix level, and each later one is `feedback` quieter;
//     tempo sync follows the host BPM (120 without one); ping-pong puts
//     the first repeat left and the second right,
//   - switching an effect off lets its tail ring out (spillover) while new
//     input stays dry, and once the tail has gone the rack is exact again,
//   - the reverb decays at its RT60 and stays bounded at the longest decay
//     and largest room,
//   - modulation switches on without a click and decorrelates L/R,
//   - a change of order dips and recovers, and non-finite parameters can't
//     produce non-finite output,
//   - processor level: every new parameter defaults off, survives a state
//     round trip, lands on its default from a state saved before it
//     existed, rides presets, and the reverb shows up in the host tail.
#include "Processor.h"
#include "TimeFx.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

constexpr int kBlock = 256;

// Runs `in` (stereo, same signal both sides) through `rack` block by block.
juce::AudioBuffer<float> run(timefx::Rack& rack, const std::vector<float>& in, const timefx::Params& p,
                             double bpm = 0.0) {
  juce::AudioBuffer<float> out(2, static_cast<int>(in.size()));
  for (int ch = 0; ch < 2; ++ch)
    out.copyFrom(ch, 0, in.data(), static_cast<int>(in.size()));
  for (int off = 0; off < out.getNumSamples(); off += kBlock) {
    const int n = std::min(kBlock, out.getNumSamples() - off);
    juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, off, n);
    rack.process(view, p, bpm);
  }
  return out;
}

std::vector<float> impulseAt(int at, int length, float amplitude = 1.0f) {
  std::vector<float> x(static_cast<size_t>(length), 0.0f);
  x[static_cast<size_t>(at)] = amplitude;
  return x;
}

double rms(const float* x, int from, int to) {
  double acc = 0.0;
  for (int i = from; i < to; ++i)
    acc += static_cast<double>(x[i]) * x[i];
  return std::sqrt(acc / std::max(1, to - from));
}

int samples(double seconds) { return static_cast<int>(std::lround(seconds * kFs)); }

timefx::Params cleanDelay(float timeMs, float feedback, float mix) {
  timefx::Params p;
  p.delay.on = true;
  p.delay.timeMs = timeMs;
  p.delay.feedback = feedback;
  p.delay.mix = mix;
  p.delay.damping = 0.0f;
  return p;
}

}  // namespace

TEST(TimeFxTest, AllOffIsBitExactPassthrough) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const auto in = makeNoise(samples(0.5), 7, 0.5f);
  const auto out = run(rack, in, timefx::Params{});
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < out.getNumSamples(); ++i)
      ASSERT_EQ(out.getSample(ch, i), in[static_cast<size_t>(i)]) << "ch " << ch << " at " << i;
}

TEST(TimeFxTest, DelayRepeatsLandOnTimeAndDecayByFeedback) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const int d = samples(0.1);
  const auto in = impulseAt(0, d * 4);
  const auto out = run(rack, in, cleanDelay(100.0f, 0.5f, 0.4f));

  EXPECT_FLOAT_EQ(out.getSample(0, 0), 1.0f) << "dry stays at unity";
  EXPECT_NEAR(out.getSample(0, d), 0.4f, 1e-4f) << "first repeat at the mix level";
  EXPECT_NEAR(out.getSample(0, 2 * d), 0.2f, 1e-4f) << "second repeat one feedback step quieter";
  EXPECT_NEAR(out.getSample(0, 3 * d), 0.1f, 1e-4f);
  EXPECT_NEAR(out.getSample(0, d - 1), 0.0f, 1e-6f);
  EXPECT_NEAR(out.getSample(0, d + 1), 0.0f, 1e-6f);
}

TEST(TimeFxTest, TempoSyncFollowsTheHostBpm) {
  timefx::DelayParams p;
  p.sync = true;
  p.division = 2;  // 1/4
  EXPECT_NEAR(timefx::Delay::delaySeconds(p, 120.0), 0.5, 1e-9);
  EXPECT_NEAR(timefx::Delay::delaySeconds(p, 0.0), 0.5, 1e-9) << "no tempo: 120 BPM";
  p.division = 6;  // dotted 1/8
  EXPECT_NEAR(timefx::Delay::delaySeconds(p, 100.0), 0.45, 1e-9);
  p.division = 0;  // whole note at 20 BPM would be 12 s: clamped
  EXPECT_NEAR(timefx::Delay::delaySeconds(p, 20.0), timefx::kMaxDelaySeconds, 1e-9);
  p.division = 999;
  EXPECT_TRUE(std::isfinite(timefx::Delay::delaySeconds(p, std::numeric_limits<double>::quiet_NaN())));

  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  auto params = cleanDelay(0.0f, 0.0f, 1.0f);
  params.delay.sync = true;
  params.delay.division = 8;  // 1/16 at 150 BPM = 100 ms
  const auto out = run(rack, impulseAt(0, samples(0.2)), params, 150.0);
  EXPECT_NEAR(out.getSample(0, samples(0.1)), 1.0f, 1e-4f);
}

TEST(TimeFxTest, PingPongAlternatesSides) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  auto p = cleanDelay(50.0f, 0.5f, 1.0f);
  p.delay.pingPong = true;
  const int d = samples(0.05);
  const auto out = run(rack, impulseAt(0, d * 3), p);
  EXPECT_NEAR(out.getSample(0, d), 1.0f, 1e-4f) << "first repeat left";
  EXPECT_NEAR(out.getSample(1, d), 0.0f, 1e-6f);
  EXPECT_NEAR(out.getSample(0, 2 * d), 0.0f, 1e-6f);
  EXPECT_NEAR(out.getSample(1, 2 * d), 0.5f, 1e-4f) << "second repeat right";
}

TEST(TimeFxTest, SwitchingOffLetsTheTailRingOutThenGoesExact) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const int d = samples(0.1);
  auto p = cleanDelay(100.0f, 0.5f, 0.5f);
  // Impulse while on, then switch off well before the first repeat.
  juce::AudioBuffer<float> buf(2, kBlock);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  buf.setSample(1, 0, 1.0f);
  rack.process(buf, p, 0.0);
  p.delay.on = false;

  std::vector<float> left;
  const auto late = impulseAt(d / 2, samples(3.0));  // new input after the switch
  for (int off = 0; off + kBlock <= static_cast<int>(late.size()); off += kBlock) {
    for (int ch = 0; ch < 2; ++ch)
      buf.copyFrom(ch, 0, late.data() + off, kBlock);
    rack.process(buf, p, 0.0);
    left.insert(left.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
  }
  // Positions in `left` are relative to the second block.
  EXPECT_NEAR(left[static_cast<size_t>(d - kBlock)], 0.5f, 1e-3f) << "the tail rings out";
  EXPECT_NEAR(left[static_cast<size_t>(d / 2 + d - kBlock)], 0.0f, 1e-3f)
      << "input after switching off is not delayed";

  // Long after the tail has died away the rack is exact passthrough again.
  const auto in = makeNoise(kBlock * 4, 3, 0.5f);
  const auto out = run(rack, in, p);
  for (int i = 0; i < out.getNumSamples(); ++i)
    ASSERT_EQ(out.getSample(0, i), in[static_cast<size_t>(i)]);
}

TEST(TimeFxTest, ReverbDecaysAtItsRt60) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  timefx::Params p;
  p.reverb.on = true;
  p.reverb.decayS = 2.0f;
  p.reverb.damping = 0.0f;
  p.reverb.mix = 1.0f;
  const auto in = makeNoise(samples(0.05), 11, 0.5f);
  std::vector<float> x(in);
  x.resize(static_cast<size_t>(samples(2.0)), 0.0f);
  const auto out = run(rack, x, p);

  // 2 s RT60: 30 dB down over one second of tail.
  const double early = rms(out.getReadPointer(0), samples(0.3), samples(0.4));
  const double late = rms(out.getReadPointer(0), samples(1.3), samples(1.4));
  ASSERT_GT(early, 0.0);
  const double dropDb = 20.0 * std::log10(early / std::max(late, 1e-12));
  EXPECT_NEAR(dropDb, 30.0, 6.0);
  EXPECT_GT(rms(out.getReadPointer(1), samples(0.3), samples(0.4)), 0.0) << "stereo tail";
}

TEST(TimeFxTest, ReverbStaysBoundedAtTheExtremes) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  timefx::Params p;
  p.reverb.on = true;
  p.reverb.decayS = 10.0f;
  p.reverb.size = 1.0f;
  p.reverb.damping = 0.0f;
  p.reverb.mix = 1.0f;
  const auto out = run(rack, makeNoise(samples(4.0), 5, 0.5f), p);
  float peak = 0.0f;
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < out.getNumSamples(); ++i) {
      ASSERT_TRUE(std::isfinite(out.getSample(ch, i)));
      peak = std::max(peak, std::abs(out.getSample(ch, i)));
    }
  EXPECT_LT(peak, 8.0f);
}

TEST(TimeFxTest, ModulationSwitchesOnWithoutAClick) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const auto in = makeSine(samples(0.5), 220.0, 0.5f);
  timefx::Params p;
  p.mod.on = true;
  p.mod.mix = 1.0f;
  p.mod.depth = 1.0f;
  const auto out = run(rack, in, p);

  // Largest sample-to-sample step stays near the sine's own.
  float maxStep = 0.0f;
  for (int i = 1; i < out.getNumSamples(); ++i)
    maxStep = std::max(maxStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
  const float sineStep = static_cast<float>(0.5 * 2.0 * kPi * 220.0 / kFs);
  EXPECT_LT(maxStep, sineStep * 1.5f);

  double diff = 0.0;
  for (int i = samples(0.2); i < samples(0.5); ++i)
    diff += std::abs(out.getSample(0, i) - out.getSample(1, i));
  EXPECT_GT(diff, 1.0) << "the two sides sweep apart";
}

TEST(TimeFxTest, ReorderDipsAndRecovers) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  timefx::Params p;
  const auto in = makeSine(samples(0.1), 440.0, 0.5f);
  run(rack, in, p);
  p.order = 5;
  const auto out = run(rack, in, p);
  // Dipped near the start, back at the input level by the end.
  EXPECT_LT(rms(out.getReadPointer(0), 0, samples(0.008)), 0.3);
  EXPECT_NEAR(rms(out.getReadPointer(0), samples(0.05), samples(0.1)), 0.5 / std::sqrt(2.0), 0.01);
}

TEST(TimeFxTest, HostileParamsStayFinite) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  timefx::Params p;
  p.delay = {true, nan, false, -4, inf, nan, true, nan};
  p.reverb = {true, inf, nan, -inf, nan};
  p.mod = {true, true, nan, inf, nan};
  p.order = 1000;
  const auto out = run(rack, makeNoise(samples(0.5), 9, 0.5f), p, std::numeric_limits<double>::infinity());
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < out.getNumSamples(); ++i)
      ASSERT_TRUE(std::isfinite(out.getSample(ch, i)));
  EXPECT_TRUE(std::isfinite(timefx::Rack::tailSeconds(p, 0.0)));
}

TEST(TimeFxTest, MonoBufferWorks) {
  timefx::Rack rack;
  rack.prepare(kFs, kBlock);
  const int d = samples(0.01);
  juce::AudioBuffer<float> buf(1, kBlock * 2);
  buf.clear();
  buf.setSample(0, 0, 1.0f);
  auto p = cleanDelay(10.0f, 0.0f, 1.0f);
  for (int off = 0; off < buf.getNumSamples(); off += kBlock) {
    juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 1, off, kBlock);
    rack.process(view, p, 0.0);
  }
  EXPECT_NEAR(buf.getSample(0, d), 1.0f, 1e-4f);
}

// Processor-level contracts.

namespace {

const char* const kFxIds[] = {
    "fxDelayEnabled", "fxDelayTime",     "fxDelaySync",  "fxDelayDivision", "fxDelayFeedback",
    "fxDelayMix",     "fxDelayPingPong", "fxDelayDamping", "fxReverbEnabled", "fxReverbSize",
    "fxReverbDecay",  "fxReverbDamping", "fxReverbMix",  "fxModEnabled",    "fxModType",
    "fxModRate",      "fxModDepth",      "fxModMix",     "fxOrder"};

float denorm(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

void setDenorm(TONE3000Processor& proc, const char* id, float value) {
  auto* p = proc.parameters.getParameter(id);
  p->setValueNotifyingHost(p->convertTo0to1(value));
}

}  // namespace

TEST(ProcessorTest, TimeFxDefaultsAreOffAndMatchTheDsp) {
  TONE3000Processor proc;
  for (const auto* id : kFxIds)
    ASSERT_NE(proc.parameters.getParameter(id), nullptr) << id;
  const timefx::Params d;
  EXPECT_EQ(denorm(proc, "fxDelayEnabled"), 0.0f);
  EXPECT_EQ(denorm(proc, "fxReverbEnabled"), 0.0f);
  EXPECT_EQ(denorm(proc, "fxModEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "fxDelayTime"), d.delay.timeMs, 0.01f);
  EXPECT_NEAR(denorm(proc, "fxDelayFeedback") * 0.95f, d.delay.feedback, 1e-3f);
  EXPECT_NEAR(denorm(proc, "fxReverbDecay"), d.reverb.decayS, 0.01f);
  EXPECT_NEAR(denorm(proc, "fxModRate"), d.mod.rateHz, 0.01f);
  EXPECT_EQ(static_cast<int>(denorm(proc, "fxDelayDivision")), d.delay.division);
  EXPECT_EQ(proc.getLatencySamples(), 0);
}

TEST(ProcessorTest, TimeFxSurvivesStateRoundTrip) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    setDenorm(a, "fxDelayEnabled", 1.0f);
    setDenorm(a, "fxDelayTime", 250.0f);
    setDenorm(a, "fxReverbDecay", 4.5f);
    setDenorm(a, "fxModType", 1.0f);
    setDenorm(a, "fxOrder", 3.0f);
    a.getStateInformation(state);
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(denorm(b, "fxDelayEnabled"), 1.0f);
  EXPECT_NEAR(denorm(b, "fxDelayTime"), 250.0f, 0.1f);
  EXPECT_NEAR(denorm(b, "fxReverbDecay"), 4.5f, 0.01f);
  EXPECT_EQ(denorm(b, "fxModType"), 1.0f);
  EXPECT_EQ(denorm(b, "fxOrder"), 3.0f);
}

TEST(ProcessorTest, StateFromBeforeTheFxRackLandsOnItsDefaults) {
  juce::MemoryBlock saved;
  {
    TONE3000Processor old;
    old.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(static_cast<const char*>(saved.getData()) + 4,
                                                       saved.getSize() - 4);
  ASSERT_TRUE(tree.isValid());
  juce::ValueTree params = tree.getChildWithName("PARAMETERS");
  for (const auto* id : kFxIds) {
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
  setDenorm(proc, "fxReverbEnabled", 1.0f);
  setDenorm(proc, "fxDelayTime", 900.0f);
  proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
  EXPECT_EQ(denorm(proc, "fxReverbEnabled"), 0.0f);
  EXPECT_NEAR(denorm(proc, "fxDelayTime"), 380.0f, 0.1f);
}

TEST(ProcessorTest, PresetsCarryTheFxRack) {
  const juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("t3k-fx-tests-" + juce::Uuid().toString());
  tmp.createDirectory();
  {
    TONE3000Processor proc;
    proc.setPresetStoreForTesting(tmp);
    setDenorm(proc, "fxReverbEnabled", 1.0f);
    setDenorm(proc, "fxReverbMix", 0.6f);
    const juce::var saved = proc.savePreset("Wet");
    ASSERT_TRUE(saved.isObject());
    setDenorm(proc, "fxReverbEnabled", 0.0f);
    setDenorm(proc, "fxReverbMix", 0.1f);
    ASSERT_TRUE(proc.loadPreset(saved["id"].toString()));
    EXPECT_EQ(denorm(proc, "fxReverbEnabled"), 1.0f);
    EXPECT_NEAR(denorm(proc, "fxReverbMix"), 0.6f, 1e-3f);
  }
  tmp.deleteRecursively();
}

TEST(ProcessorTest, ReverbExtendsTheHostTail) {
  TONE3000Processor proc;
  const double base = proc.getTailLengthSeconds();
  setDenorm(proc, "fxReverbEnabled", 1.0f);
  setDenorm(proc, "fxReverbDecay", 8.0f);
  EXPECT_GE(proc.getTailLengthSeconds(), 8.0);
  setDenorm(proc, "fxReverbEnabled", 0.0f);
  EXPECT_DOUBLE_EQ(proc.getTailLengthSeconds(), base);
}

namespace {

// Feeds one 256-sample burst through a fresh processor (after the empty
// chain's start-up fades settle) and returns the left channel's RMS per
// block afterwards.
std::vector<double> burstResponse(bool delayOn) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  setDenorm(proc, "fxDelayEnabled", delayOn ? 1.0f : 0.0f);
  setDenorm(proc, "fxDelayTime", 20.0f);
  setDenorm(proc, "fxDelayMix", 1.0f);
  setDenorm(proc, "fxDelayFeedback", 0.0f);
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  for (int i = 0; i < 40; ++i) {
    buf.clear();
    proc.processBlock(buf, midi);
  }
  const auto burst = makeSine(kBlock, 1000.0, 0.5f);
  std::vector<double> levels;
  for (int b = 0; b < 8; ++b) {
    buf.clear();
    if (b == 0)
      for (int ch = 0; ch < 2; ++ch)
        buf.copyFrom(ch, 0, burst.data(), kBlock);
    proc.processBlock(buf, midi);
    levels.push_back(rms(buf.getReadPointer(0), 0, kBlock));
  }
  proc.releaseResources();
  return levels;
}

}  // namespace

TEST(ProcessorTest, DelayRunsInProcessBlock) {
  const auto dry = burstResponse(false);
  const auto wet = burstResponse(true);
  ASSERT_GT(dry[0], 0.01) << "the empty chain passes the burst";
  // 20 ms = 960 samples: the repeat spans blocks 3 and 4.
  // The dry run only carries the gate's release residue after the burst.
  EXPECT_LT(dry[4], 0.01 * dry[0]);
  EXPECT_GT(wet[4], 0.1 * dry[0]);
  EXPECT_GT(wet[4], 20.0 * dry[4]);
  EXPECT_LT(wet[6], 0.02 * dry[0]) << "no feedback: one repeat only";
}
