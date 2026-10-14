// Tone step tests (MIDI next / previous per block)
//
// "block2Next" / "rightBlock1Prev" step a block through the search its tone
// came from (BrowseContext.h). These pin:
//
//   - the positional target ids parse like the block powers, and junk ids
//     are rejected (so a hostile session can't map them),
//   - presses deliver per lane and position on the message thread, rapid
//     ones coalesced into one signed step, and mappings survive state,
//   - stepBlock routes to the editor's stepper when one is registered, and
//     declines blocks with no search behind them or past the chain's end,
//   - the window-closed path's pure helpers: the cursor skips NAM tones
//     with no A2 model and stops at the stored page's ends, and a tone plus
//     a model listing make the tone JSON a swap takes (garbage makes none).
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

namespace {

void pumpMessages(int ms = 20) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

juce::MidiBuffer ccEvent(int number, int value) {
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::controllerEvent(1, number, value), 0);
  return midi;
}

void learn(TONE3000Processor& proc, const juce::String& targetId, int cc) {
  proc.midiMapper.startLearn(targetId);
  pumpMessages();
  proc.midiMapper.processMidi(ccEvent(cc, 127));
  pumpMessages();
}

juce::var context(int index, const std::vector<std::pair<const char*, bool>>& tones) {
  juce::Array<juce::var> rows;
  int id = 100;
  for (const auto& [format, a2] : tones) {
    juce::DynamicObject::Ptr row = new juce::DynamicObject();
    row->setProperty("id", id++);
    row->setProperty("format", format);
    row->setProperty("a2", a2);
    rows.add(juce::var(row.get()));
  }
  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("index", index);
  o->setProperty("page", 1);
  o->setProperty("totalPages", 1);
  o->setProperty("tones", rows);
  return juce::var(o.get());
}

juce::String irTone(int toneId) {
  return "{\"id\":" + juce::String(toneId) + ",\"title\":\"IR\",\"format\":\"ir\",\"models\":[{\"id\":1,"
         "\"name\":\"cab\",\"model_url\":\"https://test.invalid/cab.wav\"}]}";
}

}  // namespace

TEST(ToneStepTest, TargetIdsParse) {
  using M = MidiMapper;
  auto t = M::blockStepTargetFor("block3Next");
  EXPECT_EQ(t.index, 2);
  EXPECT_FALSE(t.right);
  EXPECT_EQ(t.delta, 1);
  t = M::blockStepTargetFor("rightBlock1Prev");
  EXPECT_EQ(t.index, 0);
  EXPECT_TRUE(t.right);
  EXPECT_EQ(t.delta, -1);
  for (const char* bad : {"block0Next", "block65Next", "blockXNext", "block3Nexts", "block3Power", "rightBlockNext",
                          "Block3Next", "block-1Prev", ""})
    EXPECT_EQ(M::blockStepTargetFor(bad).index, -1) << bad;
}

TEST(ToneStepTest, PressesDeliverPerLaneCoalescedAndSurviveState) {
  TONE3000Processor proc;
  learn(proc, "block2Next", 30);
  learn(proc, "rightBlock1Prev", 31);

  struct Step {
    int index;
    bool right;
    int delta;
  };
  std::vector<Step> steps;
  proc.midiMapper.onBlockStep = [&steps](int index, bool right, int delta) { steps.push_back({index, right, delta}); };

  proc.midiMapper.processMidi(ccEvent(30, 127));
  proc.midiMapper.processMidi(ccEvent(30, 0));  // momentary release: no second press
  pumpMessages();
  ASSERT_EQ(steps.size(), 1u);
  EXPECT_EQ(steps[0].index, 1);
  EXPECT_FALSE(steps[0].right);
  EXPECT_EQ(steps[0].delta, 1);

  // Rapid presses before the hop: one signed step per lane and position.
  steps.clear();
  proc.midiMapper.processMidi(ccEvent(31, 127));
  proc.midiMapper.processMidi(ccEvent(31, 127));
  proc.midiMapper.processMidi(ccEvent(30, 127));
  pumpMessages();
  ASSERT_EQ(steps.size(), 2u);
  EXPECT_EQ(steps[0].index, 1);  // left lane first
  EXPECT_EQ(steps[0].delta, 1);
  EXPECT_TRUE(steps[1].right);
  EXPECT_EQ(steps[1].delta, -2);

  // The mappings travel with the session.
  TONE3000Processor reopened;
  reopened.midiMapper.restoreFromValueTree(proc.midiMapper.toValueTree());
  const auto state = reopened.midiMapper.getState();
  int found = 0;
  for (const auto& m : *state["mappings"].getArray())
    if (m["targetId"] == juce::var("block2Next") || m["targetId"] == juce::var("rightBlock1Prev"))
      ++found;
  EXPECT_EQ(found, 2);
}

TEST(ToneStepTest, StepBlockRoutesToTheEditorsStepper) {
  ChainTestProcessor proc;
  const auto stepped = proc.loadTone(irTone(100), {}, context(0, {{"ir", true}, {"ir", true}}));
  ASSERT_FALSE(stepped.empty());
  proc.loadTone(irTone(200));  // block 2: no search behind it

  std::string gotId;
  int gotDelta = 0;
  proc.blockStepHandler = [&](const std::string& id, int delta) {
    gotId = id;
    gotDelta = delta;
    return true;
  };
  EXPECT_TRUE(proc.stepBlock(0, false, +1));
  EXPECT_EQ(gotId, stepped);
  EXPECT_EQ(gotDelta, 1);

  gotId.clear();
  EXPECT_FALSE(proc.stepBlock(1, false, +1));  // no browse context
  EXPECT_FALSE(proc.stepBlock(7, false, +1));  // past the chain's end
  EXPECT_FALSE(proc.stepBlock(0, true, +1));   // right lane outside stereo
  EXPECT_FALSE(proc.stepBlock(0, false, 0));
  EXPECT_TRUE(gotId.empty());
}

TEST(ToneStepTest, HeadlessCursorSkipsUnloadableAndStopsAtThePage) {
  const auto ctx = context(0, {{"nam", true}, {"nam", false}, {"ir", false}, {"nam", true}});
  EXPECT_EQ(TONE3000Processor::browseStepIndex(ctx, +1), 2);  // NAM without A2 skipped; IR a2 flag ignored
  EXPECT_EQ(TONE3000Processor::browseStepIndex(ctx, +2), 3);
  EXPECT_EQ(TONE3000Processor::browseStepIndex(ctx, +3), -1);  // off the page
  EXPECT_EQ(TONE3000Processor::browseStepIndex(ctx, -1), -1);
  EXPECT_EQ(TONE3000Processor::browseStepIndex(context(3, {{"nam", true}, {"nam", false}, {"nam", false}, {"ir", true}}), -1), 0);
  EXPECT_EQ(TONE3000Processor::browseStepIndex({}, 1), -1);
}

TEST(ToneStepTest, HeadlessToneJsonComesFromToneAndFirstModel) {
  const auto tone = juce::JSON::parse(R"({"id":42,"title":"Plexi","format":"nam","models":[{"id":9}]})");
  const auto models = juce::JSON::parse(
      R"({"data":[{"id":7,"name":"a","model_url":"https://x/7.nam"},{"id":8,"name":"b","model_url":"https://x/8.nam"}]})");
  const auto merged = juce::JSON::parse(TONE3000Processor::toneJsonWithFirstModel(tone, models));
  EXPECT_EQ(static_cast<int>(merged["id"]), 42);
  ASSERT_EQ(merged["models"].size(), 1);
  EXPECT_EQ(static_cast<int>(merged["models"][0]["id"]), 7);
  // The caller's payload is untouched (the merge works on a copy).
  EXPECT_EQ(static_cast<int>(tone["models"][0]["id"]), 9);

  EXPECT_TRUE(TONE3000Processor::toneJsonWithFirstModel(tone, juce::JSON::parse(R"({"data":[]})")).isEmpty());
  EXPECT_TRUE(TONE3000Processor::toneJsonWithFirstModel(tone, juce::JSON::parse(R"({"data":[{"id":1}]})")).isEmpty());
  EXPECT_TRUE(TONE3000Processor::toneJsonWithFirstModel(juce::JSON::parse(R"({"id":0})"), models).isEmpty());
  EXPECT_TRUE(TONE3000Processor::toneJsonWithFirstModel(juce::var("x"), models).isEmpty());
}
