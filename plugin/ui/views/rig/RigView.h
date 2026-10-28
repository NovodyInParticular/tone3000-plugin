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
#include "views/rig/RigTuner.h"
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
  void paintOverChildren(juce::Graphics& g) override;
  void resized() override;
  void visibilityChanged() override;

  // The tuner in the analyser's place (RigTuner). Only while the analyser
  // shows; `onClose` runs when it closes from here (its ✕, or this view
  // going away), and hideTuner() is the way back for the owner.
  bool canShowTuner() const;
  void showTuner(std::function<void()> onClose);
  void hideTuner();
  RigTuner* tuner() { return tuner_.get(); }

  // The gear in chain order with the Time FX unit last, and the insert
  // cards (Abyss Pitch, Glitch Engine) in signal order; inserts sit before
  // or after the chain (tests drive these).
  std::vector<juce::Component*> modules() const;
  std::vector<juce::Component*> inserts() const;
  ToneEqOverlay& toneEq() { return toneEq_; }
  // The module being dragged to a new place, if any (tests).
  bool reordering() const { return dragged_ != nullptr; }

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

  // Drag to reorder: a pedal, amp or cab dragged along its row, a drop bar
  // where it would land, one reorder on release (empty insert slots keep
  // their places in the lane).
  void reorderDrag(Module& m, juce::Point<int> pos);
  void reorderEnd(Module& m, juce::Point<int> pos, bool commit);
  std::vector<Module*> rowModules(int row) const;
  std::vector<Module*> row0Sequence() const;  // with the inserts and the FX unit
  bool insertDropPre(int x) const;
  int insertLayoutKey() const;
  int dropIndex(const Module& dragged, int x) const;  // among the row's other modules
  int dropBarX(const Module& dragged, int index) const;
  Module* dragged_ = nullptr;
  int dragGrabX_ = 0, dropIndex_ = -1;
  juce::Rectangle<int> cableArea(const Cable& c) const;
  juce::Rectangle<int> pulseArea(const Cable& c) const;  // the dot, with its glow
  bool cableLive(const Cable& c) const;
  float cableLevel(const Cable& c) const;

  Services& services_;
  std::vector<std::unique_ptr<Module>> modules_;
  std::unique_ptr<Module> fx_;
  std::unique_ptr<Module> glitch_;  // the Glitch Engine card
  std::unique_ptr<Module> abyss_;   // the Abyss Pitch card
  int insertLayoutShown_ = -1;      // the insertLayoutKey the layout reflects
  OutputSpectrum spectrum_;
  CpuMeter cpu_;
  ToneEqOverlay toneEq_;  // over the spectrum's plot area
  std::unique_ptr<RigTuner> tuner_;
  std::function<void()> onTunerClose_;
  std::vector<Cable> cables_;
  juce::uint32 lastTickMs_ = 0;
  float pulseDt_ = 0.0f;     // time gathered since the pulses last moved
  bool pulseFrame_ = false;  // pulses move every other tick
  bool stereo_ = false;
  int rowCount_ = 1;
};

}  // namespace t3k::ui
