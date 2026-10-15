#include "ChainScreen.h"

#include <algorithm>

namespace t3k::ui {

ChainScreen::ChainScreen(Services& services) : services_(services), gallery_(services) {
  gallery_.onOpenBlock = [this](const std::string& id) { openDetail(id); };
  gallery_.onSelectTone = [this](ChainSide side, const std::string& id) {
    if (onSelectTone) onSelectTone(side, id);
  };
  addAndMakeVisible(gallery_);
  addAndMakeVisible(galleryButton_);
  addAndMakeVisible(rigButton_);
  galleryButton_.onClick = [this] { setRigView(false); };
  rigButton_.onClick = [this] { setRigView(true); };
  setRigView(services_.prefs.get(UiPrefs::kChainView) == "rig");

  // Reopen the card a swap / OAuth round trip left behind, unless its block
  // is gone (the chain changed underneath the round trip).
  const auto saved = services_.prefs.session.find(UiPrefs::kDetailBlockId);
  if (saved != services_.prefs.session.end() && saved->second.isNotEmpty()) {
    const auto id = saved->second.toStdString();
    if (services_.chain.state().findBlock(id) != nullptr)
      openDetail(id);
    else
      services_.prefs.session.erase(UiPrefs::kDetailBlockId);
  }
}

ChainScreen::~ChainScreen() = default;

void ChainScreen::setRigView(bool rig) {
  if (services_.prefs.get(UiPrefs::kChainView) != (rig ? "rig" : "gallery"))
    services_.prefs.set(UiPrefs::kChainView, rig ? "rig" : "gallery");
  galleryButton_.setArmed(!rig);
  rigButton_.setArmed(rig);
  if (rig && rig_ == nullptr) {
    rig_ = std::make_unique<RigView>(services_);
    rig_->onOpenBlock = [this](const std::string& id) { openDetail(id); };
    addAndMakeVisible(*rig_);
  } else if (!rig) {
    rig_.reset();
  }
  gallery_.setVisible(!rig && detail_ == nullptr);
  if (rig_) rig_->setVisible(detail_ == nullptr);
  resized();
}

void ChainScreen::openDetail(const std::string& blockId) {
  services_.prefs.session[UiPrefs::kDetailBlockId] = juce::String(blockId);
  detail_ = std::make_unique<BlockDetail>(services_, blockId);
  // The card may ask to close from inside a chain listener callback (its
  // block vanished); tear it down once that callback has unwound.
  detail_->onBack = [this] {
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ChainScreen>(this)] {
      if (safe != nullptr) safe->closeDetail();
    });
  };
  detail_->onSwap = [this](const std::string& id) {
    // The swap keeps the block id; the detail id stays saved for the return.
    const auto& state = services_.chain.state();
    const bool right = state.chainRight && std::any_of(state.chainRight->begin(), state.chainRight->end(),
                                                       [&](const ChainItem& b) { return b.blockId == id; });
    if (onSelectTone) onSelectTone(right ? ChainSide::right : ChainSide::left, id);
  };
  addAndMakeVisible(*detail_);
  gallery_.setVisible(false);
  if (rig_) rig_->setVisible(false);
  galleryButton_.setVisible(false);
  rigButton_.setVisible(false);
  resized();
}

void ChainScreen::closeDetail() {
  services_.prefs.session.erase(UiPrefs::kDetailBlockId);
  if (!detail_) return;
  detail_.reset();
  gallery_.setVisible(rig_ == nullptr);
  if (rig_) rig_->setVisible(true);
  galleryButton_.setVisible(true);
  rigButton_.setVisible(true);
  resized();
}

void ChainScreen::returnToGallery() {
  closeDetail();
  gallery_.returnToGallery();
}

void ChainScreen::resized() {
  gallery_.setBounds(getLocalBounds());
  if (rig_) rig_->setBounds(getLocalBounds().withTrimmedTop(44));
  // The view switch sits top-right, level with the SIGNAL CHAIN title.
  const int y = 20;
  rigButton_.setTopLeftPosition(getWidth() - 24 - rigButton_.getWidth(), y);
  galleryButton_.setTopLeftPosition(rigButton_.getX() - 4 - galleryButton_.getWidth(), y);
  galleryButton_.toFront(false);
  rigButton_.toFront(false);
  if (detail_) detail_->setBounds(getLocalBounds());
}

}  // namespace t3k::ui
