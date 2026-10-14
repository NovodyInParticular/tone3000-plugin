// Previous / next for a tone block: steps the block through the result list
// its tone was picked from (the block's browse context, BrowseContext.h),
// without opening the browser. Each block has its own cursor, so one block
// can stay put while another cycles.
//
// A step resolves the neighbouring tone (its first loadable model, a fresh
// token for native) and swaps it into the block in place, carrying the
// moved cursor along in the same undoable edit. Off either end of the
// stored page it fetches the adjacent page of the same query first. The
// ends of the whole result list stop rather than wrap: when playing, a
// sudden jump back to result 1 is more confusing than a "last result" note.
//
// Rapid presses: every press moves the shown cursor at once and starts its
// own resolve; replies older than the block's latest press are dropped, so
// the last press always wins and nothing lands out of order.
//
// Instant switching: once a block lands, its next three and previous one
// results are resolved in the background (memoized here, so stepping to
// one skips the API round trips) and their model files fetched into
// native's disk cache (so the load skips the download too).
#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <set>
#include <vector>
#include <optional>
#include <string>

#include "ChainStore.h"
#include "ConnectionGate.h"
#include "Toast.h"
#include "ToneSession.h"
#include "core/AsyncScope.h"

namespace t3k::ui {

class ToneStepper {
public:
  // Where a block's cursor stands, for the ‹ › controls and their label.
  struct Cursor {
    int position = 0;  // 1-based across the whole result list
    int total = 0;     // results in the whole list (an estimate past page 1)
    bool canPrev = false, canNext = false;
    bool busy = false;  // a step is resolving
  };

  struct Listener {
    virtual ~Listener() = default;
    virtual void stepperChanged() = 0;
  };

  ToneStepper(Backend& backend, ChainStore& chain, ToneSession& session, ConnectionGate& connection, Toast& toast);

  // Neighbours prefetched around a landed cursor: this many ahead, and one
  // behind (stepping back is rarer, and the previous tone is often the one
  // just left, already in the cache).
  static constexpr int kPrefetchAhead = 3, kPrefetchBehind = 1;
  static constexpr size_t kMemoSize = 64;
  // A resolved tone's model URL may be signed with an expiry; past this a
  // step resolves afresh instead of trusting the memo.
  static constexpr juce::int64 kMemoMaxAgeMs = 10 * 60 * 1000;
  ~ToneStepper();

  void addListener(Listener* l) { listeners_.add(l); }
  void removeListener(Listener* l) { listeners_.remove(l); }

  // The block's cursor, or nothing when it has no browse context.
  std::optional<Cursor> cursor(const std::string& blockId) const;
  // Step by `delta` results (±1 from the buttons and keys).
  void step(const std::string& blockId, int delta);

  // The block arrow keys step: the last one stepped, picked or opened.
  const std::string& focusBlock() const { return focus_; }
  void setFocusBlock(const std::string& blockId);

  // Warm the neighbours of the block's cursor (after any landing: a step,
  // or a pick from the browser).
  void prefetchAround(const std::string& blockId);

  // The keyboard's way in (← / → step the focused block, Shift+← / → move
  // the focus between blocks that have a cursor). False when there is
  // nothing to act on, so the key can go elsewhere.
  bool stepFocused(int delta);
  bool moveFocus(int direction);

  // The browse context for a pick from the browser: `page` of `query`
  // (its results in order) with the cursor on `index`.
  static juce::var makeContext(const juce::var& query, const TonePage& page, int pageSize, int index);

private:
  struct Pending {
    int generation = 0;
    int index = 0;  // the shown cursor while resolving (within `context`)
    juce::var context;
  };

  void land(const std::string& blockId, juce::var context, int index);
  void resolve(const std::string& blockId, juce::var context, int index);
  void fetchPage(const std::string& blockId, const juce::var& context, int page, bool fromEnd);
  juce::var contextOf(const std::string& blockId) const;
  void remember(const Tone& tone);
  const Tone* memo(int toneId) const;
  void prefetchTone(int toneId);
  void notify();

  Backend& backend_;
  ChainStore& chain_;
  ToneSession& session_;
  ConnectionGate& connection_;
  Toast& toast_;
  std::map<std::string, Pending> pending_;
  std::string focus_;
  std::map<int, Tone> memo_;      // resolved tones by id (first model embedded)
  std::vector<int> memoOrder_;    // oldest first, for the size cap
  std::map<int, juce::int64> memoTime_;  // when each was resolved
  std::set<int> resolving_;       // prefetch resolves in flight
  juce::ListenerList<Listener> listeners_;
  AsyncScope scope_;
};

}  // namespace t3k::ui
