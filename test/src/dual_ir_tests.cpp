// Dual cab IR tests (DualIr.h): two IRs mixed into one kernel. These pin:
//
//   - the blend ends are the single IRs (energy-normalized), and inverting
//     flips the sign,
//   - an integer align offset places B exactly that many samples later
//     (and a negative one delays A instead), a fractional one keeps its
//     energy and lands between the two neighbouring samples,
//   - the same IR blended against its own inversion cancels to silence at
//     zero offset (the alignment is exact), but not when offset,
//   - real cab IR files combine (and resample when rates differ),
//   - garbage bytes and non-finite parameters can't produce a kernel or
//     crash: they yield nothing / clamp.
#include "DualIr.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

namespace {

std::vector<uint8_t> wavBytes(const juce::AudioBuffer<float>& buffer, double rate = 48000.0) {
  juce::MemoryBlock block;
  {
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(block, false);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate(rate)
                                                  .withNumChannels(buffer.getNumChannels())
                                                  .withBitsPerSample(32)
                                                  .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    EXPECT_NE(writer, nullptr);
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
  }
  const auto* p = static_cast<const uint8_t*>(block.getData());
  return {p, p + block.getSize()};
}

std::vector<uint8_t> impulse(int at, float amplitude = 1.0f, int length = 64) {
  juce::AudioBuffer<float> b(1, length);
  b.clear();
  b.setSample(0, at, amplitude);
  return wavBytes(b);
}

juce::AudioBuffer<float> decoded(const std::vector<uint8_t>& bytes) {
  juce::AudioBuffer<float> out;
  EXPECT_TRUE(dualir::decode(bytes, 48000.0, 2.0, out));
  return out;
}

std::vector<uint8_t> fileBytes(const char* name) {
  juce::MemoryBlock data;
  EXPECT_TRUE(testFile(name).loadFileAsData(data));
  const auto* p = static_cast<const uint8_t*>(data.getData());
  return {p, p + data.getSize()};
}

}  // namespace

TEST(DualIrTest, BlendEndsAreTheSingleIrs) {
  const auto a = impulse(3, 0.25f);  // normalized to unit energy: 1.0 at 3
  const auto b = impulse(7, 4.0f);
  dualir::Params p;
  p.blend = 0.0f;
  auto out = decoded(dualir::combine(a, b, p));
  EXPECT_NEAR(out.getSample(0, 3), 1.0f, 1e-5f);
  EXPECT_NEAR(out.getSample(0, 7), 0.0f, 1e-6f);
  p.blend = 1.0f;
  out = decoded(dualir::combine(a, b, p));
  EXPECT_NEAR(out.getSample(0, 7), 1.0f, 1e-5f);
  EXPECT_NEAR(out.getSample(0, 3), 0.0f, 1e-6f);
  p.blend = 0.0f;
  p.invertA = true;
  out = decoded(dualir::combine(a, b, p));
  EXPECT_NEAR(out.getSample(0, 3), -1.0f, 1e-5f);
}

TEST(DualIrTest, IntegerAlignShiftsExactly) {
  dualir::Params p;
  p.blend = 0.5f;
  p.alignMs = static_cast<float>(10.0 / 48.0);  // 10 samples at 48 kHz
  auto out = decoded(dualir::combine(impulse(0), impulse(0), p));
  EXPECT_NEAR(out.getSample(0, 0), 0.5f, 1e-4f);
  EXPECT_NEAR(out.getSample(0, 10), 0.5f, 1e-4f);
  EXPECT_NEAR(out.getSample(0, 5), 0.0f, 1e-4f);

  p.alignMs = -p.alignMs;  // negative: A moves later instead
  p.blend = 0.0f;
  out = decoded(dualir::combine(impulse(0), impulse(0), p));
  EXPECT_NEAR(out.getSample(0, 10), 1.0f, 1e-4f);
}

TEST(DualIrTest, FractionalAlignLandsBetweenSamples) {
  dualir::Params p;
  p.blend = 1.0f;                               // B alone, to read its shift
  p.alignMs = static_cast<float>(20.5 / 48.0);  // 20.5 samples
  const auto out = decoded(dualir::combine(impulse(0), impulse(0), p));
  // Symmetric between 20 and 21, and the shape keeps nearly all of the
  // energy of a unit impulse: a finite half-sample delay must roll off at
  // Nyquist, which costs a white impulse ~6% (all above ~20 kHz, where a cab
  // IR has nothing), hence the tolerance.
  EXPECT_NEAR(out.getSample(0, 20), out.getSample(0, 21), 1e-3f);
  EXPECT_GT(out.getSample(0, 20), 0.5f);
  double energy = 0.0;
  for (int i = 0; i < out.getNumSamples(); ++i)
    energy += out.getSample(0, i) * out.getSample(0, i);
  EXPECT_NEAR(energy, 1.0, 0.1);
}

TEST(DualIrTest, SelfAgainstInversionCancelsOnlyWhenAligned) {
  const auto cab = fileBytes("cab-ir-test.wav");
  dualir::Params p;
  p.blend = 0.5f;
  p.invertB = true;
  auto out = decoded(dualir::combine(cab, cab, p));
  EXPECT_LT(out.getMagnitude(0, out.getNumSamples()), 1e-5f);

  p.alignMs = 0.25f;  // 12 samples apart: no longer cancels
  out = decoded(dualir::combine(cab, cab, p));
  EXPECT_GT(out.getMagnitude(0, out.getNumSamples()), 1e-2f);
}

TEST(DualIrTest, RealCabsCombineAcrossRates) {
  const auto a = fileBytes("cab-ir-test.wav");
  juce::AudioBuffer<float> b48 = decoded(fileBytes("cab-ir-test-2.wav"));
  const auto b44 = wavBytes(b48, 44100.0);  // same samples, labelled 44.1k: resampled on decode
  dualir::Params p;
  const auto mixed = dualir::combine(a, b44, p);
  ASSERT_FALSE(mixed.empty());
  const auto out = decoded(mixed);
  EXPECT_GT(out.getNumSamples(), 0);
  EXPECT_GT(out.getMagnitude(0, out.getNumSamples()), 1e-3f);
  for (int ch = 0; ch < out.getNumChannels(); ++ch)
    for (int i = 0; i < out.getNumSamples(); ++i)
      ASSERT_TRUE(std::isfinite(out.getSample(ch, i)));
}

TEST(DualIrTest, GarbageInputsYieldNothingAndParamsClamp) {
  const auto good = impulse(0);
  const std::vector<uint8_t> junk{'R', 'I', 'F', 'F', 0, 1, 2, 3, 'n', 'o', 'p', 'e'};
  dualir::Params p;
  EXPECT_TRUE(dualir::combine(junk, good, p).empty());
  EXPECT_TRUE(dualir::combine(good, {}, p).empty());

  p.blend = std::numeric_limits<float>::quiet_NaN();
  p.levelADb = 1e9f;
  p.alignMs = -std::numeric_limits<float>::infinity();
  const auto s = p.sanitized();
  EXPECT_EQ(s.blend, 0.5f);
  EXPECT_EQ(s.levelADb, dualir::Params::kMaxLevelDb);
  EXPECT_EQ(s.alignMs, 0.0f);
  EXPECT_FALSE(dualir::combine(good, good, p).empty());
}

// ---------------------------------------------------------------------------
// Through the processor: a cab block with a second IR, audio running.
//
//   - B loads from a local file and the chain reports it,
//   - the same cab blended against its own inversion at zero offset nulls
//     the block's wet output (the whole path, not just the kernel math),
//   - blend 0 sounds like the single IR, and removing B returns to it,
//   - the blend persists through save / reopen with B's bytes embedded (no
//     file or network needed), and undo of a knob rebuilds the kernel.
#include "Processor.h"
#include "chain_test_helpers.h"

namespace {

constexpr int kStep = 256;

struct DualRig {
  ChainTestProcessor proc;
  std::vector<float> noise;
  juce::AudioBuffer<float> buffer{2, kStep};
  juce::MidiBuffer midi;
  int offset = 0;

  explicit DualRig(const juce::ValueTree& state = makeState()) {
    juce::Random rng(7);
    noise.resize(48000);
    for (auto& v : noise)
      v = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
    proc.setPlayConfigDetails(2, 2, kFs, kStep);
    proc.prepareToPlay(kFs, kStep);
    proc.restoreFromTree(state);
  }

  static juce::ValueTree makeState() {
    juce::ValueTree state("ChainSnapshot");
    juce::ValueTree lane("ChainBlocks");
    lane.appendChild(makeIrBlockTree("blk", 1, 100, "cab-ir-test.wav", "cab"), nullptr);
    state.appendChild(lane, nullptr);
    return state;
  }

  float rms() {
    buffer.copyFrom(0, 0, noise.data() + offset, kStep);
    buffer.copyFrom(1, 0, noise.data() + offset, kStep);
    offset = (offset + kStep) % (static_cast<int>(noise.size()) - kStep);
    proc.processBlock(buffer, midi);
    return buffer.getRMSLevel(0, 0, kStep);
  }

  juce::var dual() {
    const juce::var state = proc.getChainState(-1);
    for (const auto& item : *state["chain"].getArray())
      if (item["blockId"].toString() == "blk")
        return item["dualIr"];
    return {};
  }

  bool idle() {
    const juce::var state = proc.getChainState(-1);
    for (const auto& item : *state["chain"].getArray())
      if (item["blockId"].toString() == "blk")
        return !static_cast<bool>(item["modelLoading"]) && !static_cast<bool>(item["dualIr"]["building"]) &&
               !proc.hasRetiringModel("blk");
    return false;
  }

  // Keep audio running, paced near real time (restores and undos hold the
  // chain mute until their loads land, on wall-clock timers), until the
  // kernel is rebuilt and settled; then the mean RMS over a quarter second.
  float settle() {
    for (int i = 0, idleFor = 0; i < 4000 && idleFor < 40; ++i) {
      rms();
      idleFor = idle() ? idleFor + 1 : 0;
      juce::Thread::sleep(2);
    }
    for (int i = 0; i < 100; ++i)
      rms();  // smoothers, convolver tails
    double sum = 0.0;
    for (int i = 0; i < 48; ++i)
      sum += rms();
    return static_cast<float>(sum / 48.0);
  }
};

}  // namespace

TEST(DualIrProcessorTest, SelfInversionNullsAndBlendZeroIsTheSingleIr) {
  DualRig rig;
  ASSERT_TRUE(waitForChainLoaded(rig.proc));
  const float single = rig.settle();
  ASSERT_GT(single, 1e-4f);

  const auto result = rig.proc.setBlockSecondIrFile("blk", testFile("cab-ir-test.wav"));
  ASSERT_TRUE(result.hasProperty("blockId")) << result["error"].toString();
  ASSERT_TRUE(rig.dual()["b"].isObject());
  ASSERT_TRUE(rig.proc.setBlockDualIrParam("blk", "invertB", 1));
  const float nulled = rig.settle();
  EXPECT_LT(nulled, single * 0.01f) << "A blended against its own inversion should cancel";

  ASSERT_TRUE(rig.proc.setBlockDualIrParam("blk", "invertB", 0));
  ASSERT_TRUE(rig.proc.setBlockDualIrParam("blk", "blend", 0.0));
  EXPECT_NEAR(rig.settle(), single, single * 0.02f);

  ASSERT_TRUE(rig.proc.setBlockDualIrParam("blk", "blend", 0.5));
  ASSERT_TRUE(rig.proc.clearBlockSecondIr("blk"));
  EXPECT_FALSE(rig.dual()["b"].isObject());
  EXPECT_NEAR(rig.settle(), single, single * 0.02f);
}

TEST(DualIrProcessorTest, BlendPersistsWithEmbeddedBytesAndUndoRebuilds) {
  DualRig rig;
  ASSERT_TRUE(waitForChainLoaded(rig.proc));
  const float single = rig.settle();
  rig.proc.setBlockSecondIrFile("blk", testFile("cab-ir-test.wav"));
  rig.proc.setBlockDualIrParam("blk", "invertB", 1);
  ASSERT_LT(rig.settle(), single * 0.01f);

  // Save, then reopen in a fresh instance: B rides along in the state.
  juce::MemoryBlock saved;
  rig.proc.getStateInformation(saved);
  DualRig reopened(DualRig::makeState());
  reopened.proc.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  ASSERT_TRUE(waitForChainLoaded(reopened.proc));
  EXPECT_TRUE(reopened.dual()["b"].isObject());
  EXPECT_TRUE(static_cast<bool>(reopened.dual()["invertB"]));
  EXPECT_LT(reopened.settle(), single * 0.01f) << "reopened blend should still null";

  // Undo the invert: the kernel is rebuilt, the null goes away.
  ASSERT_TRUE(rig.proc.undoChain());
  EXPECT_GT(rig.settle(), single * 0.5f);
}

TEST(DualIrProcessorTest, RejectsNamBlocksAndBadInput) {
  ChainTestProcessor proc;
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  lane.appendChild(makeNamBlockTree("amp", 1, 100), nullptr);
  lane.appendChild(makeIrBlockTree("cab", 2, 200), nullptr);
  state.appendChild(lane, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));

  EXPECT_FALSE(proc.setBlockDualIrParam("amp", "blend", 0.3));
  EXPECT_TRUE(proc.setBlockSecondIrFile("amp", testFile("cab-ir-test.wav")).hasProperty("error"));
  EXPECT_FALSE(proc.setBlockSecondIr("cab", R"({"id":5,"format":"nam","models":[{"id":1,"model_url":"https://x/a.nam"}]})"));
  EXPECT_FALSE(proc.setBlockSecondIr("cab", "not json"));
  EXPECT_FALSE(proc.setBlockDualIrParam("cab", "blend", std::numeric_limits<double>::quiet_NaN()));
  EXPECT_FALSE(proc.setBlockDualIrParam("cab", "bogus", 1.0));
  EXPECT_TRUE(proc.setBlockSecondIrFile("cab", testFile("a2-amp-test.nam")).hasProperty("error"));

  // Out-of-range values clamp.
  ASSERT_TRUE(proc.setBlockDualIrParam("cab", "alignMs", 500.0));
  const juce::var chain = proc.getChainState(-1);
  for (const auto& item : *chain["chain"].getArray())
    if (item["blockId"].toString() == "cab")
      EXPECT_FLOAT_EQ(static_cast<float>(item["dualIr"]["alignMs"]), dualir::Params::kMaxAlignMs);
}

TEST(DualIrProcessorTest, HostileStateClampsAndIgnoresABrokenB) {
  ChainTestProcessor proc;
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree lane("ChainBlocks");
  auto block = makeIrBlockTree("blk", 1, 100);
  block.setProperty("irBModelId", 999, nullptr);
  block.setProperty("irBToneJson", "{broken", nullptr);
  block.setProperty("irBlend", 1e30, nullptr);
  block.setProperty("irAlignMs", -77.0, nullptr);
  lane.appendChild(block, nullptr);
  state.appendChild(lane, nullptr);
  proc.restoreFromTree(state);
  ASSERT_TRUE(waitForChainLoaded(proc));
  const juce::var chain = proc.getChainState(-1);
  const auto d = (*chain["chain"].getArray())[0]["dualIr"];
  ASSERT_TRUE(d.isObject());
  EXPECT_FALSE(d["b"].isObject());
  EXPECT_FLOAT_EQ(static_cast<float>(d["blend"]), 1.0f);
  EXPECT_FLOAT_EQ(static_cast<float>(d["alignMs"]), -dualir::Params::kMaxAlignMs);
}
