// The rig view's output analyser (BlockSpectrum behind the "output" id):
// off by default (the audio thread does no work for it), and once enabled
// it reports the output's spectrum: a 1 kHz tone peaks in the bin around
// 1 kHz, well above the bins far away.
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

TEST(ProcessorTest, OutputSpectrumFollowsTheOutput) {
  TONE3000Processor proc;
  constexpr int kBlock = 256;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);
  ASSERT_TRUE(proc.setBlockSpectrumEnabled(TONE3000Processor::kOutputSpectrumId, true));

  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto tone = makeSine(kBlock * 64, 1000.0, 0.3f);
  for (int b = 0; b < 64; ++b) {
    for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, tone.data() + b * kBlock, kBlock);
    proc.processBlock(buf, midi);
  }
  juce::var bins;
  for (int i = 0; i < 20; ++i) {  // the analysis is throttled; give it a few polls
    bins = proc.getBlockSpectrum(TONE3000Processor::kOutputSpectrumId);
    juce::Thread::sleep(35);
  }
  ASSERT_TRUE(bins.isArray());
  ASSERT_EQ(bins.size(), BlockSpectrum::kNumBins);
  // Bin centre frequencies are log-spaced 20 Hz..20 kHz.
  const auto binFor = [](double hz) {
    return static_cast<int>(std::lround(std::log(hz / 20.0) / std::log(1000.0) * (BlockSpectrum::kNumBins - 1)));
  };
  const double at1k = static_cast<double>(bins[binFor(1000.0)]);
  EXPECT_GT(at1k, static_cast<double>(bins[binFor(60.0)]) + 20.0);
  EXPECT_GT(at1k, static_cast<double>(bins[binFor(15000.0)]) + 20.0);
  EXPECT_TRUE(proc.setBlockSpectrumEnabled(TONE3000Processor::kOutputSpectrumId, false));
  proc.releaseResources();
}

// The dry-input tap behind the "input" id (the analyser's ghost trace):
// nothing reaches it until it is enabled, then it reports the input's
// spectrum as the chain receives it.
TEST(ProcessorTest, InputSpectrumFollowsTheInputOnlyWhileEnabled) {
  TONE3000Processor proc;
  constexpr int kBlock = 256;
  proc.setPlayConfigDetails(2, 2, kFs, kBlock);
  proc.prepareToPlay(kFs, kBlock);

  juce::MidiBuffer midi;
  juce::AudioBuffer<float> buf(2, kBlock);
  const auto tone = makeSine(kBlock * 64, 1000.0, 0.3f);
  const auto play = [&] {
    for (int b = 0; b < 64; ++b) {
      for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, 0, tone.data() + b * kBlock, kBlock);
      proc.processBlock(buf, midi);
    }
  };
  const auto poll = [&] {
    juce::var bins;
    for (int i = 0; i < 20; ++i) {  // the analysis is throttled; give it a few polls
      bins = proc.getBlockSpectrum(TONE3000Processor::kInputSpectrumId);
      juce::Thread::sleep(35);
    }
    return bins;
  };
  const auto binFor = [](double hz) {
    return static_cast<int>(std::lround(std::log(hz / 20.0) / std::log(1000.0) * (BlockSpectrum::kNumBins - 1)));
  };

  play();
  auto bins = poll();
  ASSERT_TRUE(bins.isArray());
  EXPECT_LE(static_cast<double>(bins[binFor(1000.0)]), BlockSpectrum::kMinDb + 1.0) << "the tap ran while off";

  ASSERT_TRUE(proc.setBlockSpectrumEnabled(TONE3000Processor::kInputSpectrumId, true));
  play();
  bins = poll();
  ASSERT_EQ(bins.size(), BlockSpectrum::kNumBins);
  const double at1k = static_cast<double>(bins[binFor(1000.0)]);
  EXPECT_GT(at1k, static_cast<double>(bins[binFor(60.0)]) + 20.0);
  EXPECT_GT(at1k, static_cast<double>(bins[binFor(15000.0)]) + 20.0);
  EXPECT_TRUE(proc.setBlockSpectrumEnabled(TONE3000Processor::kInputSpectrumId, false));
  proc.releaseResources();
}
