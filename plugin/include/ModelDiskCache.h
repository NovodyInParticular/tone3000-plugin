#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <mutex>
#include <vector>

/**
 * On-disk cache of downloaded TONE3000 model files (NAM captures and IRs),
 * so a tone revisited in a later session, another plugin instance or the
 * standalone loads without the network, and the previous / next prefetch
 * has somewhere to put what it fetched ahead.
 *
 * Location (the platform's cache directory, never a hardcoded path):
 *   Linux    $XDG_CACHE_HOME/tone3000/models (default ~/.cache/tone3000/models)
 *   macOS    ~/Library/Caches/TONE3000/models
 *   Windows  %LOCALAPPDATA%\TONE3000\Cache\models
 *
 * Keys: the model URL without its query string (signed / expiring query
 * parameters must not split one model into many entries), hashed. Entries
 * are written to a temp file and renamed into place, and carry a checksum
 * of their payload, so a crash mid-write or a corrupted file reads as a
 * miss (and is removed) rather than as bad model bytes.
 *
 * Size: least-recently-used entries (by mtime; a hit touches it) are
 * evicted once the folder passes the budget.
 *
 * Threading: loader / prefetch threads only, never the audio thread. Every
 * method is safe to call concurrently; whole-file writes plus rename keep
 * readers from ever seeing a partial entry.
 */
class ModelDiskCache {
public:
  static constexpr juce::int64 kDefaultBudgetBytes = 1024LL * 1024 * 1024;  // 1 GB
  // Larger than any published model or the IR loader's own cap allows.
  static constexpr juce::int64 kMaxEntryBytes = 256LL * 1024 * 1024;

  explicit ModelDiskCache(juce::File root, juce::int64 budgetBytes = kDefaultBudgetBytes);

  /** The platform cache folder above. */
  static juce::File defaultRoot();

  /** The canonical cache identity of a model URL ("" for local files and
      anything that isn't http(s)). */
  static juce::String keyFor(const juce::String& modelUrl);

  /** True when `modelUrl` is cacheable and present (no checksum read). */
  bool contains(const juce::String& modelUrl) const;

  /** The cached bytes, or empty on a miss (absent, unreadable, corrupt). */
  std::vector<uint8_t> read(const juce::String& modelUrl);

  /** Store `bytes` for `modelUrl` (no-op for uncacheable URLs or empty /
      oversized payloads), then evict down to the budget. */
  bool write(const juce::String& modelUrl, const std::vector<uint8_t>& bytes);

  /** Total bytes of entries currently on disk. */
  juce::int64 sizeOnDisk() const;

  /** Remove every entry. */
  void clear();

  const juce::File& root() const { return root_; }

private:
  juce::File fileFor(const juce::String& key) const;
  void evictToBudget();

  juce::File root_;
  juce::int64 budget_;
  // Serializes eviction scans; reads and writes are independent files.
  std::mutex evictMutex_;
};
