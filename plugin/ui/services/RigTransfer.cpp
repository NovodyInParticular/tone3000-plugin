#include "RigTransfer.h"

#include <map>

namespace t3k::ui {

namespace {
constexpr const char* kExtension = ".t3krig";
}  // namespace

RigTransfer::RigTransfer(Backend& backend, ToneSession& session, ChainStore& chain, UiPrefs& prefs)
    : backend_(backend), session_(session), chain_(chain), prefs_(prefs) {}

RigTransfer::~RigTransfer() { *alive_ = false; }

juce::String RigTransfer::rigName() const {
  const auto& preset = chain_.state().preset;
  return preset && preset->name.isNotEmpty() ? preset->name : juce::String("My Rig");
}

juce::String RigTransfer::missingSentence(const juce::var& names, const juce::String& verb) {
  const auto* arr = names.getArray();
  if (arr == nullptr || arr->isEmpty()) return {};
  juce::StringArray list;
  for (const auto& n : *arr) list.add(n.toString());
  return juce::String(list.size()) + (list.size() == 1 ? " item " : " items ") + verb + ": " +
         list.joinIntoString(", ") + ".";
}

RigTransfer::Outcome RigTransfer::copyCode() {
  const juce::var r = backend_.exportRig(rigName(), prefs_.get(UiPrefs::kSkin), true);
  const juce::String code = r["text"].toString();
  if (code.isEmpty()) return {false, "Couldn't export the rig."};
  juce::SystemClipboard::copyTextToClipboard(code);
  juce::String message = "Rig code copied to the clipboard.";
  if (const auto missing = missingSentence(r["missing"], "can't travel in a code (local files; use a rig file)");
      missing.isNotEmpty())
    message << " " << missing;
  return {true, message};
}

void RigTransfer::saveFile(Done done) {
  if (chooser_ != nullptr) return;
  const juce::String name = juce::File::createLegalFileName(rigName());
  chooser_ = std::make_unique<juce::FileChooser>(
      "Save Rig", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name + kExtension),
      juce::String("*") + kExtension);
  auto alive = alive_;
  chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, alive, done](const juce::FileChooser& chooser) {
                          if (!*alive) return;
                          juce::MessageManager::callAsync([this, alive] {
                            if (*alive) chooser_.reset();
                          });
                          auto file = chooser.getResult();
                          if (file == juce::File()) return;
                          if (!file.hasFileExtension(kExtension)) file = file.withFileExtension(kExtension);
                          const juce::var r = backend_.exportRig(rigName(), prefs_.get(UiPrefs::kSkin), false);
                          if (!file.replaceWithText(r["text"].toString())) {
                            if (done) done({false, "Couldn't write " + file.getFileName() + "."});
                            return;
                          }
                          juce::String message = "Saved " + file.getFileName() + ".";
                          if (const auto e = missingSentence(r["embedded"], "embedded in the file"); e.isNotEmpty())
                            message << " " << e;
                          if (const auto m = missingSentence(r["missing"], "too large to embed, left out");
                              m.isNotEmpty())
                            message << " " << m;
                          if (done) done({true, message});
                        });
}

void RigTransfer::importFile(Done done) {
  if (chooser_ != nullptr) return;
  chooser_ = std::make_unique<juce::FileChooser>("Open Rig", juce::File{}, juce::String("*") + kExtension);
  auto alive = alive_;
  chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                        [this, alive, done](const juce::FileChooser& chooser) {
                          if (!*alive) return;
                          juce::MessageManager::callAsync([this, alive] {
                            if (*alive) chooser_.reset();
                          });
                          const auto file = chooser.getResult();
                          if (file == juce::File()) return;
                          if (file.getSize() > static_cast<juce::int64>(24 * 1024 * 1024)) {
                            if (done) done({false, "That file is too large to be a rig."});
                            return;
                          }
                          importText(file.loadFileAsString(), done);
                        });
}

void RigTransfer::importText(const juce::String& text, Done done) {
  if (busy_) return;
  const juce::var info = backend_.inspectRig(text);
  if (!static_cast<bool>(info["ok"])) {
    if (done) done({false, info["error"].toString().isNotEmpty() ? info["error"].toString() : "Not a rig."});
    return;
  }
  std::vector<int> ids;
  if (const auto* arr = info["toneIds"].getArray())
    for (const auto& v : *arr) ids.push_back(static_cast<int>(v));

  // Shared across the parallel lookups; the last one to land applies.
  struct Pending {
    juce::String text;
    std::map<int, juce::String> resolved;
    int outstanding = 0;
  };
  auto pending = std::make_shared<Pending>();
  pending->text = text;
  pending->outstanding = static_cast<int>(ids.size());
  auto alive = alive_;

  auto finish = [this, alive, pending, done] {
    if (!*alive) return;
    busy_ = false;
    juce::DynamicObject::Ptr tones = new juce::DynamicObject();
    for (const auto& [id, json] : pending->resolved) tones->setProperty(juce::String(id), json);
    const juce::var r = backend_.applyRig(pending->text, tones.get());
    if (!static_cast<bool>(r["ok"])) {
      if (done) done({false, r["error"].toString()});
      return;
    }
    chain_.refresh(true);
    // The rig's look comes along when this machine has that skin.
    if (const auto skin = r["skin"].toString(); skin.isNotEmpty()) prefs_.set(UiPrefs::kSkin, skin);
    juce::String message = "Loaded rig \"" + r["name"].toString() + "\".";
    if (const auto m = missingSentence(r["missing"], "missing (local files or tones that couldn't be fetched)");
        m.isNotEmpty())
      message << " " << m;
    if (done) done({true, message});
  };

  if (ids.empty()) {
    finish();
    return;
  }
  if (!session_.authenticated()) {
    if (done) done({false, "Sign in to TONE3000 to load this rig's tones."});
    return;
  }
  busy_ = true;
  // The fresh token reaches native before any download starts.
  session_.ensureNativeAuth([](const juce::String&) {});
  for (const int id : ids) {
    session_.getTone(id, [this, alive, pending, finish, id](Result<Tone> tone) {
      if (!*alive) return;
      if (!tone) {
        if (--pending->outstanding == 0) finish();
        return;
      }
      session_.listToneModels(id, tone->format, [alive, pending, finish, id, t = *tone](Result<std::vector<Model>> models) {
        if (!*alive) return;
        if (models && !models->empty()) pending->resolved[id] = t.withModels(*models).toJson();
        if (--pending->outstanding == 0) finish();
      });
    });
  }
}

}  // namespace t3k::ui
