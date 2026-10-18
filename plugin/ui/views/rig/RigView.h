// The rig view (feature 12): the signal chain as a row of gear, pedals ->
// amps -> cab(s) -> the time-FX unit, each block one module styled by the
// active skin, with the output analyser underneath. Pedals are compact
// stompboxes, NAM amps a head with the capture's name, its In / Out / Mix
// knobs and a pulse trace that follows the block's output level, IR blocks
// a cab whose grille the skin picks (dots, hex). Click a module to open its
// detail card. Stereo rigs show one row per chain.
//
// Animation runs off the UI clock (30 Hz) and only while the skin allows
// it and the window shows; the audio thread is never involved. A new tone
// landing in a module flickers it briefly on skins that ask for it.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "services/Services.h"
#include "widgets/CpuMeter.h"
#include "views/rig/ToneEqOverlay.h"
#include "widgets/OutputSpectrum.h"

namespace t3k::ui {

class RigView : public juce::Component,
                private ChainStore::Listener,
                private UiClock::Listener,
                private UiPrefs::Listener {
public:
  explicit RigView(Services& services);
  ~RigView() override;

  std::function<void(const std::string& blockId)> onOpenBlock;

  void paint(juce::Graphics& g) override;
  void resized() override;

  // The gear in chain order, the Time FX unit last (tests drive these).
  std::vector<juce::Component*> modules() const;
  ToneEqOverlay& toneEq() { return toneEq_; }

  class Module;

private:
  void chainChanged(const ChainState& state) override;
  void tick() override;
  void prefChanged(const juce::String& key) override;
  void rebuild();

  // A cable between two neighbouring modules (or into the FX unit), and its
  // pulse: a dot travelling along it at a speed and brightness set by the
  // upstream module's output level. Cables touching a bypassed module stay
  // dim and still.
  struct Cable {
    Module* from = nullptr;
    Module* to = nullptr;
    float phase = 0.0f;  // 0..1 along the cable
    bool drawn = false;  // a pulse was painted last frame
  };
  void layoutCables();
  juce::Rectangle<int> cableArea(const Cable& c) const;
  juce::Rectangle<int> pulseArea(const Cable& c) const;  // the dot, with its glow
  bool cableLive(const Cable& c) const;
  float cableLevel(const Cable& c) const;

  Services& services_;
  std::vector<std::unique_ptr<Module>> modules_;
  std::unique_ptr<Module> fx_;
  OutputSpectrum spectrum_;
  CpuMeter cpu_;
  ToneEqOverlay toneEq_;  // over the spectrum's plot area
  std::vector<Cable> cables_;
  juce::uint32 lastTickMs_ = 0;
  float pulseDt_ = 0.0f;     // time gathered since the pulses last moved
  bool pulseFrame_ = false;  // pulses move every other tick
  bool stereo_ = false;
  int rowCount_ = 1;
};

}  // namespace t3k::ui
