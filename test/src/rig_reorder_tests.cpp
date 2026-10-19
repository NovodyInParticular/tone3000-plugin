// Reordering tone blocks the way the rig view's drag does (the tone blocks
// permuted, every empty insert slot left where it was): the chain takes the
// new order, it is saved with the session and comes back on reopen, and
// one undo restores the old order.
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

namespace {

juce::String irTone(int toneId) {
  return "{\"id\":" + juce::String(toneId) + ",\"title\":\"IR " + juce::String(toneId) +
         "\",\"format\":\"ir\",\"gear\":\"cab\",\"models\":[{\"id\":" + juce::String(toneId) +
         ",\"name\":\"cab\",\"model_url\":\"https://test.invalid/cab.wav\"}]}";
}

std::vector<std::string> laneIds(const juce::var& state) {
  std::vector<std::string> ids;
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane) ids.push_back(item["blockId"].toString().toStdString());
  return ids;
}

std::vector<int> toneIds(const juce::var& state) {
  std::vector<int> ids;
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane)
      if (item["kind"].toString() == "tone") ids.push_back(static_cast<int>(item["tone"]["id"]));
  return ids;
}

}  // namespace

TEST(RigReorderTest, TonesMoveInsertsStayAndTheOrderIsSaved) {
  ChainTestProcessor proc;
  for (const int id : {100, 101, 102}) ASSERT_FALSE(proc.loadTone(irTone(id), {}).empty());
  const auto before = laneIds(proc.getChainState(-1));
  ASSERT_EQ(toneIds(proc.getChainState(-1)), (std::vector<int>{100, 101, 102}));

  // The last tone dragged to the front; inserts keep their slots.
  std::vector<std::string> tones;
  std::vector<bool> isTone;
  const auto state = proc.getChainState(-1);
  if (const auto* lane = state["chain"].getArray())
    for (const auto& item : *lane) {
      const bool tone = item["kind"].toString() == "tone";
      isTone.push_back(tone);
      if (tone) tones.push_back(item["blockId"].toString().toStdString());
    }
  std::rotate(tones.begin(), tones.end() - 1, tones.end());
  std::vector<std::string> order;
  size_t next = 0;
  for (size_t i = 0; i < before.size(); ++i) order.push_back(isTone[i] ? tones[next++] : before[i]);

  ASSERT_TRUE(proc.reorderChainBlocks(order));
  EXPECT_EQ(laneIds(proc.getChainState(-1)), order);
  EXPECT_EQ(toneIds(proc.getChainState(-1)), (std::vector<int>{102, 100, 101}));

  juce::MemoryBlock saved;
  proc.getStateInformation(saved);
  ChainTestProcessor reopened;
  reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
  EXPECT_EQ(toneIds(reopened.getChainState(-1)), (std::vector<int>{102, 100, 101}));

  ASSERT_TRUE(proc.undoChain());
  EXPECT_EQ(toneIds(proc.getChainState(-1)), (std::vector<int>{100, 101, 102}));
}
