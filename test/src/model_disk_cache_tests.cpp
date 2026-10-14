// Model disk cache tests
//
// ModelDiskCache keeps downloaded model files across sessions and plugin
// instances, and is where the previous / next prefetch lands. These pin:
//
//   - a write reads back byte-identical, under the URL with any query
//     string (signed URLs must not split one model into many entries),
//   - local files and non-http URLs are never cached,
//   - a corrupted or truncated entry reads as a miss and is removed,
//   - least-recently-used entries are evicted past the budget, and a read
//     counts as a use,
//   - on Linux the folder follows $XDG_CACHE_HOME (absolute values only),
//   - the processor loads a catalog model from the disk cache when the
//     network can't serve it (the test URLs never resolve).
#include "ModelDiskCache.h"
#include "Processor.h"
#include "chain_test_helpers.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace {

struct ScratchDir {
  juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("t3k-cache-test-" + juce::Uuid().toString());
  ScratchDir() { dir.createDirectory(); }
  ~ScratchDir() { dir.deleteRecursively(); }
};

std::vector<uint8_t> bytesOf(int n, uint8_t seed) {
  std::vector<uint8_t> v(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
    v[static_cast<size_t>(i)] = static_cast<uint8_t>(seed + i * 7);
  return v;
}

juce::File onlyEntry(const juce::File& dir) {
  const auto files = dir.findChildFiles(juce::File::findFiles, false, "*.t3kc");
  return files.size() == 1 ? files[0] : juce::File();
}

}  // namespace

TEST(ModelDiskCacheTest, RoundTripsAndIgnoresTheQueryString) {
  ScratchDir scratch;
  ModelDiskCache cache(scratch.dir);
  const auto data = bytesOf(5000, 3);
  ASSERT_TRUE(cache.write("https://cdn.example/models/42.nam?sig=abc&exp=1", data));
  EXPECT_TRUE(cache.contains("https://cdn.example/models/42.nam?sig=zzz&exp=2"));
  EXPECT_EQ(cache.read("https://cdn.example/models/42.nam"), data);
  EXPECT_FALSE(cache.contains("https://cdn.example/models/43.nam"));
  EXPECT_TRUE(cache.read("https://cdn.example/models/43.nam").empty());
}

TEST(ModelDiskCacheTest, NeverCachesLocalOrOddUrls) {
  ScratchDir scratch;
  ModelDiskCache cache(scratch.dir);
  const auto data = bytesOf(100, 1);
  EXPECT_FALSE(cache.write("file:///home/user/amp.nam", data));
  EXPECT_FALSE(cache.write("/home/user/amp.nam", data));
  EXPECT_FALSE(cache.write("ftp://x/amp.nam", data));
  EXPECT_FALSE(cache.write("https://x/empty.nam", {}));
  EXPECT_EQ(ModelDiskCache::keyFor("file:///a"), juce::String());
  EXPECT_EQ(cache.sizeOnDisk(), 0);
}

TEST(ModelDiskCacheTest, CorruptEntriesAreMissesAndGetRemoved) {
  ScratchDir scratch;
  ModelDiskCache cache(scratch.dir);
  const juce::String url = "https://x/models/1.nam";

  ASSERT_TRUE(cache.write(url, bytesOf(2048, 9)));
  auto entry = onlyEntry(scratch.dir);
  ASSERT_TRUE(entry.existsAsFile());
  {  // flip a payload byte
    juce::MemoryBlock data;
    entry.loadFileAsData(data);
    static_cast<char*>(data.getData())[100] ^= 0x5a;
    entry.replaceWithData(data.getData(), data.getSize());
  }
  EXPECT_TRUE(cache.read(url).empty());
  EXPECT_FALSE(entry.existsAsFile());

  ASSERT_TRUE(cache.write(url, bytesOf(2048, 9)));
  entry = onlyEntry(scratch.dir);
  entry.replaceWithText("T3K");  // truncated inside the header
  EXPECT_TRUE(cache.read(url).empty());
  EXPECT_FALSE(entry.existsAsFile());
}

TEST(ModelDiskCacheTest, EvictsLeastRecentlyUsedPastTheBudget) {
  ScratchDir scratch;
  // Room for two 10 KB entries (plus headers), not three.
  ModelDiskCache cache(scratch.dir, 25 * 1024);
  const auto data = bytesOf(10 * 1024, 4);
  ASSERT_TRUE(cache.write("https://x/a.nam", data));
  ASSERT_TRUE(cache.write("https://x/b.nam", data));
  // Make the ages unambiguous whatever the filesystem's mtime resolution:
  // a is old, b is older; then reading a makes it the most recent.
  const auto now = juce::Time::getCurrentTime();
  for (const auto& f : scratch.dir.findChildFiles(juce::File::findFiles, false, "*.t3kc"))
    f.setLastModificationTime(now - juce::RelativeTime::hours(2));
  ASSERT_FALSE(cache.read("https://x/a.nam").empty());  // touch a

  ASSERT_TRUE(cache.write("https://x/c.nam", data));
  EXPECT_TRUE(cache.contains("https://x/a.nam"));
  EXPECT_FALSE(cache.contains("https://x/b.nam"));
  EXPECT_TRUE(cache.contains("https://x/c.nam"));
  EXPECT_LE(cache.sizeOnDisk(), 25 * 1024);
}

#if JUCE_LINUX || JUCE_BSD
TEST(ModelDiskCacheTest, LinuxRootFollowsXdgCacheHome) {
  const char* saved = std::getenv("XDG_CACHE_HOME");
  const std::string restore = saved != nullptr ? saved : "";

  setenv("XDG_CACHE_HOME", "/tmp/t3k-xdg-test", 1);
  EXPECT_EQ(ModelDiskCache::defaultRoot().getFullPathName(), juce::String("/tmp/t3k-xdg-test/tone3000/models"));

  // The spec ignores relative values; so do we.
  setenv("XDG_CACHE_HOME", "relative/cache", 1);
  const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  EXPECT_EQ(ModelDiskCache::defaultRoot(), home.getChildFile(".cache/tone3000/models"));

  unsetenv("XDG_CACHE_HOME");
  EXPECT_EQ(ModelDiskCache::defaultRoot(), home.getChildFile(".cache/tone3000/models"));

  if (saved != nullptr)
    setenv("XDG_CACHE_HOME", restore.c_str(), 1);
}
#endif

TEST(ModelDiskCacheTest, ProcessorLoadsFromTheDiskCacheWhenTheNetworkCant) {
  ScratchDir scratch;
  ChainTestProcessor proc;
  proc.setModelDiskCacheForTesting(std::make_unique<ModelDiskCache>(scratch.dir));

  // The amp the helper names (https://test.invalid/amp.nam never resolves),
  // seeded on disk the way an earlier session's download would have left it.
  juce::MemoryBlock amp;
  ASSERT_TRUE(testFile("a2-amp-test.nam").loadFileAsData(amp));
  const auto* raw = static_cast<const uint8_t*>(amp.getData());
  ASSERT_TRUE(proc.diskCache().write("https://test.invalid/amp.nam?token=old",
                                     std::vector<uint8_t>(raw, raw + amp.getSize())));

  // A block with no embedded bytes: only the disk cache can serve it.
  auto block = makeNamBlockTree("blk", 1, 100);
  block.removeChild(block.getChildWithName("ModelCache"), nullptr);
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  left.appendChild(block, nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);

  ASSERT_TRUE(waitForChainLoaded(proc));
  const auto chain = proc.getChainState(-1);
  bool loaded = false;
  for (const auto& item : *chain["chain"].getArray())
    if (item["kind"].toString() == "tone")
      loaded = static_cast<bool>(item["loaded"]) && !static_cast<bool>(item["loadFailed"]);
  EXPECT_TRUE(loaded);
}

TEST(ModelDiskCacheTest, PrefetchIsANoOpForCachedAndLocalModels) {
  ScratchDir scratch;
  ChainTestProcessor proc;
  proc.setModelDiskCacheForTesting(std::make_unique<ModelDiskCache>(scratch.dir));
  ASSERT_TRUE(proc.diskCache().write("https://test.invalid/cached.nam", bytesOf(64, 2)));
  // Neither queues anything (nothing to wait for, nothing written).
  proc.prefetchModel("https://test.invalid/cached.nam");
  proc.prefetchModel("file:///tmp/local.nam");
  EXPECT_EQ(scratch.dir.findChildFiles(juce::File::findFiles, false, "*.t3kc").size(), 1);
}

// Control for the test above: the same block with an empty cache fails, so
// that test's load really came from the disk cache.
TEST(ModelDiskCacheTest, WithoutTheCacheTheSameBlockFails) {
  ScratchDir scratch;
  ChainTestProcessor proc;
  proc.setModelDiskCacheForTesting(std::make_unique<ModelDiskCache>(scratch.dir));
  auto block = makeNamBlockTree("blk", 1, 100);
  block.removeChild(block.getChildWithName("ModelCache"), nullptr);
  juce::ValueTree state("ChainSnapshot");
  juce::ValueTree left("ChainBlocks");
  left.appendChild(block, nullptr);
  state.appendChild(left, nullptr);
  proc.restoreFromTree(state);

  bool failed = false;
  for (int i = 0; i < 300 && !failed; ++i) {
    juce::Thread::sleep(100);
    const auto chain = proc.getChainState(-1);
    for (const auto& item : *chain["chain"].getArray())
      if (item["kind"].toString() == "tone" && static_cast<bool>(item["loadFailed"]))
        failed = true;
  }
  EXPECT_TRUE(failed);
}
