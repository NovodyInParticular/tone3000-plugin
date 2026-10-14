#include "ModelDiskCache.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr char kMagic[4] = {'T', '3', 'K', 'C'};
constexpr size_t kHeaderBytes = sizeof(kMagic) + sizeof(juce::uint64);

// FNV-1a: stable across platforms and sessions, no crypto module needed.
// Collisions only cost a miss: the checksum covers the payload, and the
// key hash is 64 bits over a URL.
juce::uint64 fnv1a64(const void* data, size_t size) {
  juce::uint64 hash = 1469598103934665603ULL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace

ModelDiskCache::ModelDiskCache(juce::File root, juce::int64 budgetBytes)
    : root_(std::move(root)), budget_(budgetBytes) {}

juce::File ModelDiskCache::defaultRoot() {
#if JUCE_LINUX || JUCE_BSD
  // XDG: an absolute $XDG_CACHE_HOME wins; relative or unset means ~/.cache.
  const auto xdg = juce::SystemStats::getEnvironmentVariable("XDG_CACHE_HOME", {});
  const juce::File base = juce::File::isAbsolutePath(xdg)
                              ? juce::File(xdg)
                              : juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".cache");
  return base.getChildFile("tone3000").getChildFile("models");
#elif JUCE_MAC
  return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
      .getChildFile("Library/Caches/TONE3000/models");
#elif JUCE_WINDOWS
  return juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
      .getChildFile("TONE3000")
      .getChildFile("Cache")
      .getChildFile("models");
#else
  return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("TONE3000-models");
#endif
}

juce::String ModelDiskCache::keyFor(const juce::String& modelUrl) {
  const auto trimmed = modelUrl.trim();
  if (!trimmed.startsWithIgnoreCase("https://") && !trimmed.startsWithIgnoreCase("http://"))
    return {};
  const auto withoutQuery = trimmed.upToFirstOccurrenceOf("?", false, false)
                                .upToFirstOccurrenceOf("#", false, false);
  const auto utf8 = withoutQuery.toStdString();
  return juce::String::toHexString(static_cast<juce::int64>(fnv1a64(utf8.data(), utf8.size())))
      .paddedLeft('0', 16);
}

juce::File ModelDiskCache::fileFor(const juce::String& key) const {
  return root_.getChildFile(key + ".t3kc");
}

bool ModelDiskCache::contains(const juce::String& modelUrl) const {
  const auto key = keyFor(modelUrl);
  return key.isNotEmpty() && fileFor(key).existsAsFile();
}

std::vector<uint8_t> ModelDiskCache::read(const juce::String& modelUrl) {
  const auto key = keyFor(modelUrl);
  if (key.isEmpty())
    return {};
  const auto file = fileFor(key);
  if (!file.existsAsFile() || file.getSize() > kMaxEntryBytes + static_cast<juce::int64>(kHeaderBytes))
    return {};

  juce::MemoryBlock data;
  if (!file.loadFileAsData(data) || data.getSize() <= kHeaderBytes ||
      std::memcmp(data.getData(), kMagic, sizeof(kMagic)) != 0) {
    file.deleteFile();
    return {};
  }
  juce::uint64 stored = 0;
  std::memcpy(&stored, static_cast<const char*>(data.getData()) + sizeof(kMagic), sizeof(stored));
  const auto* payload = static_cast<const uint8_t*>(data.getData()) + kHeaderBytes;
  const size_t payloadSize = data.getSize() - kHeaderBytes;
  if (juce::ByteOrder::swapIfBigEndian(stored) != fnv1a64(payload, payloadSize)) {
    file.deleteFile();  // corrupt: a miss, refetched and rewritten
    return {};
  }
  file.setLastModificationTime(juce::Time::getCurrentTime());  // LRU touch
  return std::vector<uint8_t>(payload, payload + payloadSize);
}

bool ModelDiskCache::write(const juce::String& modelUrl, const std::vector<uint8_t>& bytes) {
  const auto key = keyFor(modelUrl);
  if (key.isEmpty() || bytes.empty() || static_cast<juce::int64>(bytes.size()) > kMaxEntryBytes)
    return false;
  if (!root_.isDirectory() && !root_.createDirectory())
    return false;

  const auto target = fileFor(key);
  // Unique per writer, so two instances fetching the same model never
  // interleave into one temp file; the last rename wins with whole bytes.
  const auto temp = root_.getChildFile(key + "." + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) +
                                       ".tmp");
  {
    juce::FileOutputStream out(temp);
    if (!out.openedOk())
      return false;
    const auto checksum = juce::ByteOrder::swapIfBigEndian(fnv1a64(bytes.data(), bytes.size()));
    const bool ok = out.write(kMagic, sizeof(kMagic)) && out.write(&checksum, sizeof(checksum)) &&
                    out.write(bytes.data(), bytes.size());
    out.flush();
    if (!ok || out.getStatus().failed()) {
      temp.deleteFile();
      return false;
    }
  }
  if (!temp.moveFileTo(target)) {
    temp.deleteFile();
    return false;
  }
  evictToBudget();
  return true;
}

juce::int64 ModelDiskCache::sizeOnDisk() const {
  juce::int64 total = 0;
  for (const auto& entry : juce::RangedDirectoryIterator(root_, false, "*.t3kc", juce::File::findFiles))
    total += entry.getFileSize();
  return total;
}

void ModelDiskCache::clear() {
  for (const auto& entry : juce::RangedDirectoryIterator(root_, false, "*.t3kc;*.tmp", juce::File::findFiles))
    entry.getFile().deleteFile();
}

void ModelDiskCache::evictToBudget() {
  std::lock_guard<std::mutex> lock(evictMutex_);
  // Temp files a crash left behind mid-write (a live writer renames its own
  // within moments, so an hour-old one is certainly orphaned).
  const auto staleBefore = juce::Time::getCurrentTime() - juce::RelativeTime::hours(1);
  for (const auto& e : juce::RangedDirectoryIterator(root_, false, "*.tmp", juce::File::findFiles))
    if (e.getModificationTime() < staleBefore)
      e.getFile().deleteFile();
  struct Entry {
    juce::File file;
    juce::int64 size;
    juce::Time modified;
  };
  std::vector<Entry> entries;
  juce::int64 total = 0;
  for (const auto& e : juce::RangedDirectoryIterator(root_, false, "*.t3kc", juce::File::findFiles)) {
    entries.push_back({e.getFile(), e.getFileSize(), e.getModificationTime()});
    total += e.getFileSize();
  }
  if (total <= budget_)
    return;
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.modified < b.modified; });
  for (const auto& e : entries) {
    if (total <= budget_)
      break;
    if (e.file.deleteFile())
      total -= e.size;
  }
}
