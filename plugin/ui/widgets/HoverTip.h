// A small floating label for a hovered control: the full text of something
// shown cut short (a long tone name on a rig module). Lives in the overlay
// layer like the knob readout, so it can spill past its anchor's edges; it
// takes no clicks. No overlay host above the anchor (a bare test window):
// nothing shows.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace t3k::ui {

class HoverTip {
public:
  HoverTip();
  ~HoverTip();

  // Shows `text` just below `area` (in `anchor`'s coordinates), centred on
  // it and kept inside the overlay.
  void show(juce::Component& anchor, juce::Rectangle<float> area, const juce::String& text);
  void hide();
  bool showing() const { return bubble_ != nullptr; }

  // The bubble's component id (tests find it by this).
  static constexpr const char* kComponentId = "hoverTip";

  class Bubble;

private:
  std::unique_ptr<Bubble> bubble_;
};

}  // namespace t3k::ui
