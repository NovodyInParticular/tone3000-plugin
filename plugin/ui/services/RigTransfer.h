// Rig sharing, UI half (plugin/include/RigShare.h has the format). Export
// asks native for a rig file or a text code; import inspects the rig,
// resolves its TONE3000 tones through the signed-in session (getTone +
// the full model list, so download URLs come from the API, never from the
// rig), hands both to native, and applies the rig's skin. Messages are
// ready to show: they name local captures / IRs that couldn't travel.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "ChainStore.h"
#include "ToneSession.h"
#include "UiPrefs.h"
#include "backend/Backend.h"

namespace t3k::ui {

class RigTransfer {
public:
  struct Outcome {
    bool ok = false;
    juce::String message;
  };
  using Done = std::function<void(Outcome)>;

  RigTransfer(Backend& backend, ToneSession& session, ChainStore& chain, UiPrefs& prefs);
  ~RigTransfer();

  // Code to the clipboard.
  Outcome copyCode();
  // File via a save dialog.
  void saveFile(Done done);
  // Import pasted text (code or file contents) / a file via an open dialog.
  void importText(const juce::String& text, Done done);
  void importFile(Done done);
  bool busy() const { return busy_; }

private:
  juce::String rigName() const;
  static juce::String missingSentence(const juce::var& names, const juce::String& verb);

  Backend& backend_;
  ToneSession& session_;
  ChainStore& chain_;
  UiPrefs& prefs_;
  std::unique_ptr<juce::FileChooser> chooser_;
  bool busy_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace t3k::ui
