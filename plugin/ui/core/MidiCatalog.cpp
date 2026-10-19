#include "MidiCatalog.h"

namespace t3k::ui::midi {

const std::vector<MappableTarget>& mappableTargets() {
  using K = TargetKind;
  static const std::vector<MappableTarget> table = [] {
    std::vector<MappableTarget> t = {
        // Virtual actions (native resolves the ids itself): step through the
        // preset list in browser order, wrapping at the ends, for footswitches
        // programmed with CC / note buttons instead of program changes.
        {"presetPrevious", "Previous Preset", "Presets", K::trigger},
        {"presetNext", "Next Preset", "Presets", K::trigger},
        {"inputLevel", "Input Gain", "Global", K::continuous},
        {"outputLevel", "Output Level", "Global", K::continuous},
        {"outputBalance", "Output Balance", "Global", K::continuous},
        {"gateEnabled", "Gate Power", "Noise Gate", K::toggle},
        {"gateThreshold", "Gate Threshold", "Noise Gate", K::continuous},
        {"gateRelease", "Gate Release", "Noise Gate", K::continuous},
        {"gateHold", "Gate Hold", "Noise Gate", K::continuous},
        {"gateRange", "Gate Range", "Noise Gate", K::continuous},
        // The window stays out: like the oversampling factor it changes the
        // reported latency, a setting rather than a performance control.
        {"pitchEnabled", "Pitch Power", "Pitch Shift", K::toggle},
        {"pitchSemitones", "Pitch", "Pitch Shift", K::continuous},
        {"pitchStep", "Pitch Step", "Pitch Shift", K::toggle},
        {"pitchTonality", "Pitch Tonality", "Pitch Shift", K::continuous},
        {"fxModEnabled", "Mod Power", "Time FX", K::toggle},
        {"fxModRate", "Mod Rate", "Time FX", K::continuous},
        {"fxModDepth", "Mod Depth", "Time FX", K::continuous},
        {"fxModMix", "Mod Mix", "Time FX", K::continuous},
        {"fxDelayEnabled", "Delay Power", "Time FX", K::toggle},
        {"fxDelayTime", "Delay Time", "Time FX", K::continuous},
        {"fxDelayFeedback", "Delay Feedback", "Time FX", K::continuous},
        {"fxDelayMix", "Delay Mix", "Time FX", K::continuous},
        {"fxReverbEnabled", "Reverb Power", "Time FX", K::toggle},
        {"fxReverbDecay", "Reverb Decay", "Time FX", K::continuous},
        {"fxReverbMix", "Reverb Mix", "Time FX", K::continuous},
        {"toneEqEnabled", "Tone Stack Power", "Tone Stack", K::toggle},
        {"toneBass", "Bass", "Tone Stack", K::continuous},
        {"toneMid", "Mid", "Tone Stack", K::continuous},
        {"toneTreble", "Treble", "Tone Stack", K::continuous},
        {"toneBassFreq", "Bass Frequency", "Tone Stack", K::continuous},
        {"toneMidFreq", "Mid Frequency", "Tone Stack", K::continuous},
        {"toneTrebleFreq", "Treble Frequency", "Tone Stack", K::continuous},
        {"spreadEnabled", "Spread Power", "Spread", K::toggle},
        {"spreadOffset", "Spread Offset", "Spread", K::continuous},
        {"spreadWobble", "Spread Wobble", "Spread", K::continuous},
        {"spreadWobbleEnabled", "Spread Wobble Power", "Spread", K::toggle},
        {"spreadCrossover", "Spread Crossover", "Spread", K::continuous},
        {"spreadCrossoverEnabled", "Spread Crossover Power", "Spread", K::toggle},
        {"spreadDiffuseEnabled", "Spread Diffuse Power", "Spread", K::toggle},
        // Virtual target like block powers: stereo on/off is chain state, not
        // an APVTS parameter (the native mapper resolves the id itself).
        {"stereoEnabled", "Stereo Mode", "Stereo", K::toggle},
        {"alignEnabled", "Align Power", "Align", K::toggle},
        {"alignOffset", "Align Offset", "Align", K::continuous},
        {"alignWobble", "Align Wobble", "Align", K::continuous},
        {"alignWobbleEnabled", "Align Wobble Power", "Align", K::toggle},
        {"alignCrossover", "Align Crossover", "Align", K::continuous},
        {"alignCrossoverEnabled", "Align Crossover Power", "Align", K::toggle},
        {"alignDiffuseEnabled", "Align Diffuse Power", "Align", K::toggle},
        {"chainPanLeft", "Pan L", "Stereo", K::continuous},
        {"chainPanRight", "Pan R", "Stereo", K::continuous},
        {"chainSoloLeft", "Solo L", "Stereo", K::toggle},
        {"chainSoloRight", "Solo R", "Stereo", K::toggle},
        {"chainInvertLeft", "Invert L", "Stereo", K::toggle},
        {"chainInvertRight", "Invert R", "Stereo", K::toggle},
    };
    for (int i = 1; i <= kBlockPowerTargets; ++i)
      t.push_back({"block" + juce::String(i) + "Power", "Block " + juce::String(i) + " Power", "Chain", K::toggle});
    for (int i = 1; i <= kBlockPowerTargets; ++i)
      t.push_back({"rightBlock" + juce::String(i) + "Power", "Block " + juce::String(i) + " Power", "Chain R",
                   K::toggle});
    // Previous / next through the search a block's tone came from.
    for (const char* lane : {"block", "rightBlock"})
      for (int i = 1; i <= kBlockPowerTargets; ++i) {
        const juce::String group = juce::String(lane) == "block" ? "Chain" : "Chain R";
        t.push_back({lane + juce::String(i) + "Prev", "Block " + juce::String(i) + " Previous Tone", group, K::trigger});
        t.push_back({lane + juce::String(i) + "Next", "Block " + juce::String(i) + " Next Tone", group, K::trigger});
      }
    return t;
  }();
  return table;
}

const MappableTarget* targetById(const juce::String& id) {
  for (const auto& t : mappableTargets())
    if (t.id == id) return &t;
  return nullptr;
}

std::optional<BlockPowerTarget> blockPowerTarget(const juce::String& targetId) {
  // ^(right)?[bB]lock(\d+)(Power|Next|Prev)$: every positional block target
  const bool right = targetId.startsWith("right");
  auto rest = right ? targetId.substring(5) : targetId;
  const int suffix = rest.endsWith("Power") ? 5 : rest.endsWith("Next") || rest.endsWith("Prev") ? 4 : 0;
  if (!(rest.startsWith("block") || rest.startsWith("Block")) || suffix == 0) return std::nullopt;
  const auto digits = rest.substring(5, rest.length() - suffix);
  if (digits.isEmpty() || !digits.containsOnly("0123456789")) return std::nullopt;
  return BlockPowerTarget{digits.getIntValue() - 1, right};
}

juce::String noteName(int note) {
  static const char* const kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return juce::String(kNames[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

juce::String sourceLabel(const MidiMapping& mapping) {
  return mapping.source == MidiSource::cc ? "CC " + juce::String(mapping.number)
                                          : "Note " + noteName(mapping.number);
}

juce::String behaviorLabel(const MidiMapping& mapping) {
  const auto* target = targetById(mapping.targetId);
  const auto kind = target != nullptr ? std::optional(target->kind) : std::nullopt;
  if (kind == TargetKind::trigger) return "Trigger";
  if (kind == TargetKind::toggle || mapping.source == MidiSource::note) return "Toggle";
  return "Absolute";
}

}  // namespace t3k::ui::midi
