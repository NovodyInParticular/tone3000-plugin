// Dual cab IR panel: the cab block card's body while its 2×IR header button
// is open. IR A (the block's own tone) and IR B side by side, B loaded from
// TONE3000 or a local file, then Level A / Blend / Align / Level B knobs and
// a polarity flip per IR. The blend is baked into one kernel natively
// (plugin/include/DualIr.h), so every change here costs one rebuild and
// swaps in with the IR crossfade; the status line says so while it runs.
//
// Layout (800 × 275 body): the names row at the top, the knob row centred
// below, the status / hint line at the bottom.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

#include "model/ChainState.h"
#include "services/Services.h"
#include "widgets/ChromeTextButton.h"
#include "widgets/Knob.h"

namespace t3k::ui {

class DualIrView : public juce::Component {
public:
  DualIrView(Services& services, std::string blockId);

  // A fresh snapshot of the block (the card's resync).
  void setBlock(const ChainItem& block);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  static constexpr int kPad = 16;
  static constexpr int kNamesHeight = 56;
  static constexpr int kStatusHeight = 24;

  void send(const char* param, double value);

  Services& services_;
  std::string blockId_;
  juce::String aTitle_;
  DualIrState state_;

  ChromeTextButton load_{"LOAD", help::Key::dualIrLoad};
  ChromeTextButton file_{"FILE", help::Key::dualIrFile};
  ChromeTextButton remove_{"REMOVE", help::Key::dualIrRemove};
  ChromeTextButton invertA_{juce::String::fromUTF8("\xc3\x98 A"), help::Key::dualIrInvertA};
  ChromeTextButton invertB_{juce::String::fromUTF8("\xc3\x98 B"), help::Key::dualIrInvertB};
  Knob levelA_, blend_, align_, levelB_;
};

}  // namespace t3k::ui
