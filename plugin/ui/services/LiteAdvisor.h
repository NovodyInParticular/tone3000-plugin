// Suggests A2-Lite when the DSP load stays high (never switches on its
// own). While the CPU readout sits at or above kHighPercent for kSustainMs
// and some NAM block runs at Full size, the advisor offers to switch those
// blocks to Lite (one engine rebuild each, from the in-memory model: A2
// files carry both sizes, so nothing downloads). After switching it offers
// Back to Full for exactly the blocks it switched. "Not now" snoozes the
// offer for kSnoozeMs. The switched set is also what the gallery tiles
// badge as LITE, so it's clear which blocks are running Lite.
#pragma once

#include <juce_events/juce_events.h>

#include <set>
#include <string>

#include "ChainStore.h"
#include "MeterStore.h"

namespace t3k::ui {

class LiteAdvisor : private juce::Timer, private ChainStore::Listener {
public:
  static constexpr float kHighPercent = 75.0f;
  static constexpr juce::int64 kSustainMs = 6000;
  static constexpr juce::int64 kSnoozeMs = 10 * 60 * 1000;

  enum class Mode { hidden, suggest, switched };

  struct Listener {
    virtual ~Listener() = default;
    virtual void liteAdvisorChanged() = 0;
  };

  // `meters` null: no polling (tests drive tick() themselves).
  LiteAdvisor(ChainStore& chain, MeterStore* meters);
  ~LiteAdvisor() override;

  Mode mode() const { return mode_; }
  // Full-size NAM blocks the offer would switch.
  int eligibleCount() const;
  const std::set<std::string>& switchedBlocks() const { return switched_; }
  bool isSwitched(const std::string& blockId) const { return switched_.count(blockId) > 0; }

  void switchToLite();
  void backToFull();
  // Not now (suggest) / OK (switched): hide; a suggestion snoozes.
  void dismiss();

  // One poll: the current CPU percent at `nowMs`.
  void tick(float cpuPercent, juce::int64 nowMs);

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

private:
  void timerCallback() override;
  void chainChanged(const ChainState&) override;
  void setMode(Mode mode);
  std::vector<std::string> fullNamBlocks() const;

  ChainStore& chain_;
  MeterStore* meters_;
  Mode mode_ = Mode::hidden;
  juce::int64 highSince_ = -1;
  juce::int64 snoozedUntil_ = 0;
  juce::int64 lastNow_ = 0;
  std::set<std::string> switched_;
  juce::ListenerList<Listener> listeners_;
};

}  // namespace t3k::ui
