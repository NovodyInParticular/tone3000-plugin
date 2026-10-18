// The rig view's amp waveform tap (BlockScope): nothing is copied while it
// is off, a steady tone comes back as a full-scale, zero-crossing-aligned
// window, silence reads flat, and the processor routes it per block.
#include "BlockScope.h"
#include "Processor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

namespace {
std::vector<float> read(const BlockScope& scope) {
  std::vector<float> out;
  scope.readWaveform(out);
  return out;
}
}  // namespace

TEST(BlockScopeTest, SilentWhileOffAndFlatOnSilence) {
  BlockScope scope;
  scope.prepare(kFs);
  EXPECT_FALSE(scope.isEnabled());
  const auto points = read(scope);
  ASSERT_EQ(points.size(), static_cast<size_t>(BlockScope::kPoints));
  for (const float v : points) EXPECT_EQ(v, 0.0f);
}

TEST(BlockScopeTest, SteadyToneIsTriggeredOnARisingZeroCrossing) {
  BlockScope scope;
  scope.prepare(kFs);
  scope.setEnabled(true);
  // 220 Hz at 0.5, pushed in host-sized blocks with an arbitrary phase.
  const auto tone = makeSine(48000 / 4, 220.0, 0.5f);
  for (size_t i = 37; i + 256 <= tone.size(); i += 256) scope.pushSamples(tone.data() + i, nullptr, 256);
  const auto points = read(scope);
  ASSERT_EQ(points.size(), static_cast<size_t>(BlockScope::kPoints));
  float peak = 0.0f;
  for (const float v : points) peak = std::max(peak, std::abs(v));
  EXPECT_NEAR(peak, 0.5f, 0.02f);
  // The window starts at the crossing: near zero (each point is the largest
  // sample of its ~10-sample slice, up to ~0.14 here), then rising.
  EXPECT_NEAR(points[0], 0.0f, 0.2f);
  EXPECT_GT(points[3], points[0]);
  // 20 ms of 220 Hz is 4.4 cycles: the next rising crossings repeat it.
  int rising = 0;
  for (size_t i = 1; i < points.size(); ++i)
    if (points[i - 1] < 0.0f && points[i] >= 0.0f) ++rising;
  EXPECT_GE(rising, 3);
  EXPECT_LE(rising, 5);
}

TEST(ProcessorTest, BlockScopeIsAddressedPerBlock) {
  TONE3000Processor proc;
  proc.prepareToPlay(kFs, 256);
  EXPECT_FALSE(proc.setBlockScopeEnabled("no-such-block", true));
  EXPECT_TRUE(proc.getBlockScope("no-such-block").isVoid());
  proc.releaseResources();
}
