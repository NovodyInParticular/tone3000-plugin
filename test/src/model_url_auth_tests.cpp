// Model URL auth tests
//
// fetchModelFromUrl attaches the user's TONE3000 access token to model
// downloads. The URL comes from stored tone JSON, which a preset or a host
// project can set to anything, so the token may only go to TONE3000 itself.
// These pin:
//
//   - https tone3000.com and its subdomains take the token,
//   - the build's API origin takes it on the same scheme and host,
//   - any other host, plain http, and lookalike or parsing-trick hosts
//     (suffixes, userinfo, '#', backslashes) don't.
#include "Processor.h"

#include <gtest/gtest.h>

namespace {

bool takes(const char* url, const char* origin = "https://www.tone3000.com") {
  return TONE3000Processor::modelUrlTakesToken(url, origin);
}

}  // namespace

TEST(ModelUrlAuth, Tone3000HostsTakeTheToken) {
  EXPECT_TRUE(takes("https://www.tone3000.com/api/v1/models/1101/download"));
  EXPECT_TRUE(takes("https://tone3000.com/api/v1/models/1101/download"));
  EXPECT_TRUE(takes("https://cdn.tone3000.com/models/a.nam?sig=abc"));
  EXPECT_TRUE(takes("HTTPS://WWW.TONE3000.COM/api/v1/models/1"));
  EXPECT_TRUE(takes("https://www.tone3000.com:443/api/v1/models/1"));
  EXPECT_TRUE(takes("https://www.tone3000.com/api/v1/models/1", ""));
}

TEST(ModelUrlAuth, TheBuildsApiOriginTakesTheToken) {
  EXPECT_TRUE(takes("http://localhost:3000/api/v1/models/1", "http://localhost:3000"));
  EXPECT_TRUE(takes("https://staging.example.dev/m/1", "https://staging.example.dev/"));
  EXPECT_FALSE(takes("https://localhost:3000/api/v1/models/1", "http://localhost:3000"));
  EXPECT_FALSE(takes("http://localhost.evil.com/m", "http://localhost:3000"));
}

TEST(ModelUrlAuth, OtherHostsDont) {
  EXPECT_FALSE(takes("https://evil.example/model.nam"));
  EXPECT_FALSE(takes("http://www.tone3000.com/api/v1/models/1"));
  EXPECT_FALSE(takes("https://tone3000.com.evil.example/m"));
  EXPECT_FALSE(takes("https://eviltone3000.com/m"));
  EXPECT_FALSE(takes("https://www.tone3000.com@evil.example/m"));
  EXPECT_FALSE(takes("https://evil.example#@www.tone3000.com/m"));
  EXPECT_FALSE(takes("https://evil.example\\.tone3000.com/m"));
  EXPECT_FALSE(takes("https://evil.example%2f.tone3000.com/m"));
  EXPECT_FALSE(takes("https://evil.example/?u=https://www.tone3000.com"));
  EXPECT_FALSE(takes("file:///home/user/model.nam"));
  EXPECT_FALSE(takes(""));
  EXPECT_FALSE(takes("not a url"));
}
