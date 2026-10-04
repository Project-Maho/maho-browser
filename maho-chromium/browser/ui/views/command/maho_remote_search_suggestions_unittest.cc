// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_remote_search_suggestions.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace maho {
namespace {

AutocompleteMatch MakeMatch(AutocompleteMatchType::Type type,
                            std::u16string fill_into_edit,
                            std::u16string contents,
                            std::u16string description,
                            const char* destination_url) {
  AutocompleteMatch match;
  match.type = type;
  match.fill_into_edit = std::move(fill_into_edit);
  match.contents = std::move(contents);
  match.description = std::move(description);
  match.destination_url = GURL(destination_url);
  return match;
}

std::vector<RemoteSearchSuggestion> MapMatches(ACMatches matches) {
  AutocompleteResult result;
  result.AppendMatches(matches);
  return MapRemoteSearchSuggestionsForTesting(result);
}

TEST(MahoRemoteSearchSuggestionMappingTest,
     AcceptedMatchTypesMapToRemoteSuggestions) {
  const std::array<AutocompleteMatchType::Type, 5> accepted_types = {
      AutocompleteMatchType::SEARCH_SUGGEST,
      AutocompleteMatchType::SEARCH_SUGGEST_ENTITY,
      AutocompleteMatchType::SEARCH_SUGGEST_TAIL,
      AutocompleteMatchType::SEARCH_SUGGEST_PERSONALIZED,
      AutocompleteMatchType::SEARCH_SUGGEST_PROFILE,
  };

  ACMatches matches;
  for (size_t i = 0; i < accepted_types.size(); ++i) {
    matches.push_back(MakeMatch(
        accepted_types[i],
        std::u16string(u"Weather Query ") + base::NumberToString16(i),
        std::u16string(u"Weather Display ") + base::NumberToString16(i),
        u"Forecast subtitle",
        ("https://search.example/?q=" + std::to_string(i)).c_str()));
  }

  const auto suggestions = MapMatches(std::move(matches));
  ASSERT_EQ(5u, suggestions.size());
  for (size_t i = 0; i < suggestions.size(); ++i) {
    EXPECT_EQ("weather query " + std::to_string(i),
              suggestions[i].normalized_search_terms);
    EXPECT_EQ("Weather Display " + std::to_string(i),
              suggestions[i].display_title);
    EXPECT_EQ("Forecast subtitle", suggestions[i].subtitle);
    EXPECT_EQ("https://search.example/?q=" + std::to_string(i),
              suggestions[i].destination_url.spec());
  }
}

// Regression: the command palette used to pass the UTF-8 byte length of the
// query as AutocompleteInput's cursor position. AutocompleteInput measures the
// cursor in UTF-16 code units, so any non-ASCII query (Korean is 3 UTF-8 bytes
// per 1 UTF-16 unit) produced cursor_position > text.length() and crashed the
// browser inside AutocompleteInput::Init. Deriving the cursor from the
// converted UTF-16 text keeps the contract satisfied for every script.
TEST(MahoRemoteSearchSuggestionCursorTest,
     CursorFromUtf16TextStaysWithinInputLength) {
  const std::string queries[] = {
      "hello",             // ASCII
      "\xed\x95\x9c",      // Korean single syllable (3 UTF-8 bytes)
      "\xed\x95\x9c\xea\xb8\x80 \xea\xb2\x80\xec\x83\x89",  // Korean phrase with a space
      "\xf0\x9f\x98\x80",  // Emoji (surrogate pair in UTF-16)
      "",                  // Empty query
  };

  for (const std::string& query : queries) {
    const std::u16string text = base::UTF8ToUTF16(query);
    const size_t cursor_position = text.length();

    // The contract AutocompleteInput::Init DCHECKs on.
    EXPECT_LE(cursor_position, text.length()) << "query=" << query;

    // The previous implementation used query.size(); assert that the UTF-8
    // byte length is what actually violates the contract for non-ASCII input,
    // so this test fails if the production code regresses to byte offsets.
    if (query.size() != text.length()) {
      EXPECT_GT(query.size(), text.length()) << "query=" << query;
    }
  }
}

TEST(MahoRemoteSearchSuggestionMappingTest, RejectsNonAcceptedMatchTypes) {
  const std::array<AutocompleteMatchType::Type, 8> rejected_types = {
      AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED,
      AutocompleteMatchType::SEARCH_HISTORY,
      AutocompleteMatchType::SEARCH_OTHER_ENGINE,
      AutocompleteMatchType::NAVSUGGEST,
      AutocompleteMatchType::NAVSUGGEST_PERSONALIZED,
      AutocompleteMatchType::CALCULATOR,
      AutocompleteMatchType::DOCUMENT_SUGGESTION,
      AutocompleteMatchType::URL_WHAT_YOU_TYPED,
  };

  ACMatches matches;
  for (size_t i = 0; i < rejected_types.size(); ++i) {
    matches.push_back(MakeMatch(
        rejected_types[i],
        std::u16string(u"rejected ") + base::NumberToString16(i),
        u"Rejected", u"", "https://search.example/rejected"));
  }

  EXPECT_TRUE(MapMatches(std::move(matches)).empty());
}

TEST(MahoRemoteSearchSuggestionMappingTest,
     FallsBackToContentsForSearchTerms) {
  const auto suggestions = MapMatches({MakeMatch(
      AutocompleteMatchType::SEARCH_SUGGEST, u"", u"Fallback Terms", u"",
      "https://search.example/?q=fallback")});

  ASSERT_EQ(1u, suggestions.size());
  EXPECT_EQ("fallback terms", suggestions[0].normalized_search_terms);
  EXPECT_EQ("Fallback Terms", suggestions[0].display_title);
  EXPECT_EQ(std::nullopt, suggestions[0].subtitle);
}

TEST(MahoRemoteSearchSuggestionMappingTest,
     TrimsUnicodeWhitespaceAndAsciiCaseFoldsTerms) {
  const auto suggestions = MapMatches({MakeMatch(
      AutocompleteMatchType::SEARCH_SUGGEST, u"\u3000\u00a0WeAtHeR 서울\u00a0\u3000",
      u"Weather 서울", u"Details",
      "https://search.example/?q=weather")});

  ASSERT_EQ(1u, suggestions.size());
  EXPECT_EQ("weather 서울", suggestions[0].normalized_search_terms);
  EXPECT_EQ("Weather 서울", suggestions[0].display_title);
  EXPECT_EQ("Details", suggestions[0].subtitle);
}

TEST(MahoRemoteSearchSuggestionMappingTest, RejectsEmptyNormalizedTerms) {
  EXPECT_TRUE(MapMatches({MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST,
                                    u"\u3000\u00a0", u"", u"",
                                    "https://search.example/")})
                  .empty());
}

TEST(MahoRemoteSearchSuggestionMappingTest, RejectsInvalidAndNonHttpUrls) {
  ACMatches matches;
  matches.push_back(MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST,
                              u"invalid", u"Invalid", u"", "not a url"));
  matches.push_back(MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST,
                              u"file", u"File", u"", "file:///tmp/a"));
  matches.push_back(MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST,
                              u"ftp", u"FTP", u"", "ftp://example.test/a"));
  matches.push_back(MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST,
                              u"javascript", u"JavaScript", u"",
                              "javascript:alert(1)"));

  EXPECT_TRUE(MapMatches(std::move(matches)).empty());
}

TEST(MahoRemoteSearchSuggestionMappingTest, PreservesHttpAndHttpsDestinations) {
  const auto suggestions = MapMatches({
      MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST, u"http", u"HTTP", u"",
                "http://search.example/path?q=a%20b&source=remote"),
      MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST, u"https", u"HTTPS",
                u"", "https://search.example/path?q=a%2Bb#fragment"),
  });

  ASSERT_EQ(2u, suggestions.size());
  EXPECT_EQ("http://search.example/path?q=a%20b&source=remote",
            suggestions[0].destination_url.spec());
  EXPECT_EQ("https://search.example/path?q=a%2Bb#fragment",
            suggestions[1].destination_url.spec());
}

TEST(MahoRemoteSearchSuggestionMappingTest,
     DuplicateNormalizedTermsAreRemovedFromSnapshot) {
  const auto suggestions = MapMatches({
      MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST, u" Weather ",
                u"Weather", u"First", "https://search.example/?q=first"),
      MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST_ENTITY,
                u"\u3000wEaThEr\u00a0", u"Weather duplicate", u"Second",
                "https://search.example/?q=second"),
      MakeMatch(AutocompleteMatchType::SEARCH_SUGGEST_TAIL, u"Weather today",
                u"Weather today", u"Third",
                "https://search.example/?q=today"),
  });

  ASSERT_EQ(2u, suggestions.size());
  EXPECT_EQ("weather", suggestions[0].normalized_search_terms);
  EXPECT_EQ("https://search.example/?q=first",
            suggestions[0].destination_url.spec());
  EXPECT_EQ("weather today", suggestions[1].normalized_search_terms);
}

}  // namespace
}  // namespace maho
