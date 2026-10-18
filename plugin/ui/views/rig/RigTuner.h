// The rig view's tuner: when the tuner is on in the rig view, it takes the
// output analyser's place with a large readout in the skin's style, so the
// rig stays in sight. A cents scale across the panel (-50..+50, a tick
// every 10) with a needle, the note big in the middle and the frequency
// under it; in tune (within pitch::kInTuneCents) the needle and note light
// in the accent with the skin's glow. Owning the TunerFeed switches the
// native pitch detector on for as long as it shows.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "services/Services.h"
#include "services/TunerFeed.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class RigTuner : public juce::Component {
public:
  explicit RigTuner(Services& services);

  // The ✕ (the header's tuner button also closes).
  std::function<void()> onClose;

  const TunerFeed::State& state() const { return feed_.state(); }

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  TunerFeed feed_;
  IconButton close_{Icon::X, 24, 16};
};

}  // namespace t3k::ui
