// Input level calibration tests (InputCalibration.h). These pin:
//
//   - the measurement holds the loudest peak of the window and finishes
//     after kListenSeconds of signal (silence doesn't count, so a pause
//     before playing doesn't eat the window), or times out without one,
//   - the suggested gain lands the peak on the target and clamps,
//   - processor level: the parameters default off / 0 dB, a finished
//     measurement sets the gain and switches it on, the gain is applied
//     to the signal only while enabled, the result survives a session
//     round trip, a hostile stored value is ignored, and presets don't
//     carry it.
#include "InputCalibration.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

namespace {

constexpr int kBlock = 256;

void feed(InputCalibration& cal, float amplitude, double seconds) {
  const auto x = makeSine(kBlock, 440.0, amplitude);
  const int blocks = static_cast<int>(seconds * kFs / kBlock) + 1;
  for (int b = 0; b < blocks; ++b) cal.push(x.data(), x.data(), kBlock);
}

float denorm(TONE3000Processor& proc, const char* id) {
  auto* p = proc.parameters.getParameter(id);
  return p->convertFrom0to1(p->getValue());
}

}  // namespace

TEST(InputCalibrationTest, HoldsThePeakAndFinishesAfterTheWindow) {
  InputCalibration cal;
  EXPECT_EQ(cal.state(), InputCalibration::State::idle);
  cal.push(nullptr, nullptr, 0);  // idle: ignored
  cal.arm(kFs);
  EXPECT_EQ(cal.state(), InputCalibration::State::listening);
  feed(cal, 0.25f, 1.0);
  feed(cal, 0.5f, 0.1);  // the hardest hit
  feed(cal, 0.25f, 1.0);
  EXPECT_EQ(cal.state(), InputCalibration::State::listening);
  EXPECT_GT(cal.progress(), 0.4);
  feed(cal, 0.25f, 2.0);
  EXPECT_EQ(cal.state(), InputCalibration::State::measured);
  EXPECT_NEAR(cal.peakDb(), 20.0f * std::log10(0.5f), 0.05f);
}

TEST(InputCalibrationTest, SilenceDoesNotCountTowardTheWindow) {
  InputCalibration cal;
  cal.arm(kFs);
  feed(cal, 0.0f, 10.0);
  EXPECT_EQ(cal.state(), InputCalibration::State::listening);
  EXPECT_EQ(cal.progress(), 0.0);
  feed(cal, 0.0f, 11.0);
  EXPECT_EQ(cal.state(), InputCalibration::State::timedOut);
}

TEST(InputCalibrationTest, SuggestedGainHitsTheTargetAndClamps) {
  EXPECT_FLOAT_EQ(InputCalibration::suggestedGainDb(-18.0f), 12.0f);
  EXPECT_FLOAT_EQ(InputCalibration::suggestedGainDb(-6.0f), 0.0f);
  EXPECT_FLOAT_EQ(InputCalibration::suggestedGainDb(0.0f), -6.0f);
  EXPECT_FLOAT_EQ(InputCalibration::suggestedGainDb(-60.0f), InputCalibration::kMaxGainDb);
  EXPECT_FLOAT_EQ(InputCalibration::suggestedGainDb(std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(InputCalibrationTest, NonFiniteInputIsIgnored) {
  InputCalibration cal;
  cal.arm(kFs);
  std::vector<float> x(kBlock, std::numeric_limits<float>::infinity());
  cal.push(x.data(), nullptr, kBlock);
  EXPECT_EQ(cal.peakDb(), InputCalibration::kSilenceDb);
}

namespace {

// RMS of the left output for a steady sine through an empty chain.
double outputLevel(TONE3000Processor& proc) {
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto x = makeSine(kBlock, 440.0, 0.1f);
  double rms = 0.0;
  for (int b = 0; b < 60; ++b) {
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, x.data(), kBlock);
    proc.processBlock(buf, midi);
    if (b >= 50) {
      double acc = 0.0;
      for (int i = 0; i < kBlock; ++i) acc += buf.getSample(0, i) * buf.getSample(0, i);
      rms += std::sqrt(acc / kBlock) / 10.0;
    }
  }
  return rms;
}

}  // namespace

TEST(ProcessorTest, InputCalibrationMeasuresAndApplies) {
  TONE3000Processor proc;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  EXPECT_EQ(denorm(proc, "inputCalEnabled"), 0.0f);
  EXPECT_EQ(denorm(proc, "inputCalGain"), 0.0f);
  EXPECT_FALSE(proc.pollInputCalibration().hasProperty("measuredDb"));

  const double before = outputLevel(proc);
  proc.startInputCalibration();
  EXPECT_EQ(proc.pollInputCalibration()["state"].toString(), "listening");
  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto x = makeSine(kBlock, 440.0, 0.125f);  // -18 dBFS peak
  for (int b = 0; b < static_cast<int>(5.0 * kFs / kBlock); ++b) {
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, x.data(), kBlock);
    proc.processBlock(buf, midi);
  }
  const juce::var done = proc.pollInputCalibration();
  ASSERT_EQ(done["state"].toString(), "done");
  EXPECT_NEAR(static_cast<double>(done["peakDb"]), -18.06, 0.1);
  EXPECT_NEAR(static_cast<double>(done["gainDb"]), 12.06, 0.1);
  EXPECT_EQ(denorm(proc, "inputCalEnabled"), 1.0f);
  EXPECT_NEAR(denorm(proc, "inputCalGain"), 12.06f, 0.05f);

  // Applied: the (linear, empty) chain's output rises by the gain.
  const double after = outputLevel(proc);
  EXPECT_NEAR(20.0 * std::log10(after / before), 12.06, 0.3);
  // Off again: back to the original level.
  proc.parameters.getParameter("inputCalEnabled")->setValueNotifyingHost(0.0f);
  EXPECT_NEAR(20.0 * std::log10(outputLevel(proc) / before), 0.0, 0.1);
  proc.releaseResources();
}

TEST(ProcessorTest, InputCalibrationSurvivesStateButNotPresets) {
  juce::MemoryBlock state;
  {
    TONE3000Processor a;
    a.setPlayConfigDetails(2, 2, kFs, kBlock);
    a.prepareToPlay(kFs, kBlock);
    a.startInputCalibration();
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, kBlock);
    const auto x = makeSine(kBlock, 440.0, 0.25f);
    for (int b = 0; b < static_cast<int>(5.0 * kFs / kBlock); ++b) {
      for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, x.data(), kBlock);
      a.processBlock(buf, midi);
    }
    ASSERT_EQ(a.pollInputCalibration()["state"].toString(), "done");
    a.getStateInformation(state);
    a.releaseResources();
  }
  TONE3000Processor b;
  b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
  EXPECT_EQ(denorm(b, "inputCalEnabled"), 1.0f);
  EXPECT_NEAR(denorm(b, "inputCalGain"), 6.04f, 0.05f);
  const juce::var r = b.pollInputCalibration();
  ASSERT_TRUE(r.hasProperty("measuredDb"));
  EXPECT_NEAR(static_cast<double>(r["measuredDb"]), -12.04, 0.1);

  for (const auto& id : TONE3000Processor::presetParameterIdsForTesting())
    EXPECT_FALSE(id.startsWith("inputCal")) << "presets must not carry the calibration";
}

TEST(ProcessorTest, HostileStoredMeasurementIsIgnored) {
  juce::MemoryBlock saved;
  {
    TONE3000Processor p;
    p.getStateInformation(saved);
  }
  juce::ValueTree tree = juce::ValueTree::readFromData(static_cast<const char*>(saved.getData()) + 4,
                                                       saved.getSize() - 4);
  for (const juce::var bad : {juce::var("loud"), juce::var(1.0e9), juce::var(std::nan(""))}) {
    tree.setProperty("inputCalMeasuredDb", bad, nullptr);
    juce::MemoryBlock reframed;
    {
      juce::MemoryOutputStream out(reframed, false);
      out.write("T3KB", 4);
      tree.writeToStream(out);
    }
    TONE3000Processor proc;
    proc.setStateInformation(reframed.getData(), static_cast<int>(reframed.getSize()));
    EXPECT_FALSE(proc.pollInputCalibration().hasProperty("measuredDb"));
  }
}
