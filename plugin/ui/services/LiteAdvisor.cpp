#include "LiteAdvisor.h"

namespace t3k::ui {

namespace {
constexpr int kPollMs = 500;

template <typename F>
void forEachBlock(const ChainState& state, F&& f) {
  for (const auto& item : state.chain) f(item);
  if (state.chainRight)
    for (const auto& item : *state.chainRight) f(item);
}
}  // namespace

LiteAdvisor::LiteAdvisor(ChainStore& chain, MeterStore* meters) : chain_(chain), meters_(meters) {
  chain_.addListener(this);
  if (meters_ != nullptr) startTimer(kPollMs);
}

LiteAdvisor::~LiteAdvisor() {
  stopTimer();
  chain_.removeListener(this);
}

std::vector<std::string> LiteAdvisor::fullNamBlocks() const {
  std::vector<std::string> ids;
  forEachBlock(chain_.state(), [&](const ChainItem& item) {
    if (!item.isInsert && item.tone.isNam() && item.loaded && isSlimSizeFull(item.params.slimSize))
      ids.push_back(item.blockId);
  });
  return ids;
}

int LiteAdvisor::eligibleCount() const { return static_cast<int>(fullNamBlocks().size()); }

void LiteAdvisor::timerCallback() {
  tick(meters_->cpuPercent(), static_cast<juce::int64>(juce::Time::getMillisecondCounter()));
}

void LiteAdvisor::tick(float cpuPercent, juce::int64 nowMs) {
  lastNow_ = nowMs;
  if (cpuPercent < kHighPercent) {
    highSince_ = -1;
    return;
  }
  if (highSince_ < 0) highSince_ = nowMs;
  if (mode_ != Mode::hidden || nowMs < snoozedUntil_) return;
  if (nowMs - highSince_ >= kSustainMs && eligibleCount() > 0) setMode(Mode::suggest);
}

void LiteAdvisor::switchToLite() {
  for (const auto& id : fullNamBlocks()) {
    if (chain_.setBlockSlimSize(id, kSlimSizeLite)) switched_.insert(id);
  }
  highSince_ = -1;
  setMode(switched_.empty() ? Mode::hidden : Mode::switched);
}

void LiteAdvisor::backToFull() {
  const auto ids = switched_;
  switched_.clear();
  for (const auto& id : ids) chain_.setBlockSlimSize(id, kSlimSizeFull);
  // Don't re-suggest straight away for the load the user just chose.
  snoozedUntil_ = lastNow_ + kSnoozeMs;
  highSince_ = -1;
  setMode(Mode::hidden);
}

void LiteAdvisor::dismiss() {
  if (mode_ == Mode::suggest) snoozedUntil_ = lastNow_ + kSnoozeMs;
  highSince_ = -1;
  setMode(Mode::hidden);
}

void LiteAdvisor::chainChanged(const ChainState& state) {
  // Forget blocks that left the chain or were set back to Full by hand.
  std::set<std::string> still;
  forEachBlock(state, [&](const ChainItem& item) {
    if (switched_.count(item.blockId) > 0 && !isSlimSizeFull(item.params.slimSize)) still.insert(item.blockId);
  });
  const bool changed = still != switched_;
  switched_ = std::move(still);
  if (mode_ == Mode::switched && switched_.empty()) setMode(Mode::hidden);
  else if (mode_ == Mode::suggest && eligibleCount() == 0) setMode(Mode::hidden);
  else if (changed) listeners_.call([](Listener& l) { l.liteAdvisorChanged(); });
}

void LiteAdvisor::setMode(Mode mode) {
  if (mode_ == mode) {
    listeners_.call([](Listener& l) { l.liteAdvisorChanged(); });
    return;
  }
  mode_ = mode;
  listeners_.call([](Listener& l) { l.liteAdvisorChanged(); });
}

}  // namespace t3k::ui
