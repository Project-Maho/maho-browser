// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_model.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

CommandSuggestion MakeSuggestion(CommandSuggestionType type,
                                  const std::string& key) {
  CommandSuggestion s;
  s.type = type;
  s.key = key;
  s.title = key;
  return s;
}

class CapResultsTest : public ::testing::Test {};

TEST_F(CapResultsTest, EmptyInputProducesEmptyOutput) {
  std::vector<CommandSuggestion> results;
  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);
  EXPECT_TRUE(results.empty());
}

TEST_F(CapResultsTest, InputBelowLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(3u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("hist:0", results[1].key);
  EXPECT_EQ("bm:0", results[2].key);
}

TEST_F(CapResultsTest, InputAtExactLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 8; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(8u, results.size());
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ("hist:" + std::to_string(i), results[i].key);
  }
}

TEST_F(CapResultsTest, InputAboveLimitTruncatesAtEight) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 20; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  EXPECT_EQ(8u, results.size());
}

TEST_F(CapResultsTest, InputOrderIsPreservedAfterTruncation) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kNavigation, "nav:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kFolder, "folder:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kCalculator, "calc:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kArchivedTab, "arch:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kClosedTab, "closed:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kRecentSearch, "recent:0"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(8u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("hist:0", results[1].key);
  EXPECT_EQ("bm:0", results[2].key);
  EXPECT_EQ("action:0", results[3].key);
  EXPECT_EQ("nav:0", results[4].key);
  EXPECT_EQ("folder:0", results[5].key);
  EXPECT_EQ("calc:0", results[6].key);
  EXPECT_EQ("arch:0", results[7].key);
}

TEST_F(CapResultsTest, MixedTypesPreserveRustRankingOrder) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:rank1"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:rank2"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kBookmark, "bm:rank3"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:rank4"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:rank5"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(5u, results.size());
  EXPECT_EQ("hist:rank1", results[0].key);
  EXPECT_EQ("tab:rank2", results[1].key);
  EXPECT_EQ("bm:rank3", results[2].key);
  EXPECT_EQ("hist:rank4", results[3].key);
  EXPECT_EQ("tab:rank5", results[4].key);
}

TEST_F(CapResultsTest, ActionOnlyInputAboveLimitTruncatesToEight) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(8u, results.size());
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ("action:" + std::to_string(i), results[i].key);
  }
}

TEST_F(CapResultsTest, ActionOnlyInputAtExactLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 8; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(8u, results.size());
}

TEST_F(CapResultsTest, ActionOnlyInputBelowLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:1"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:2"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch);

  ASSERT_EQ(3u, results.size());
  EXPECT_EQ("action:0", results[0].key);
  EXPECT_EQ("action:1", results[1].key);
  EXPECT_EQ("action:2", results[2].key);
}

TEST_F(CapResultsTest, CommandsOnlyModeCapsAtTwenty) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 30; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kCommandsOnly);

  EXPECT_EQ(20u, results.size());
}

TEST_F(CapResultsTest, SiteSearchModeCapsAtTwenty) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 30; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSiteSearch);

  EXPECT_EQ(20u, results.size());
}

TEST_F(CapResultsTest, CurrentTabModeCapsAtEight) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kCurrentTab);

  EXPECT_EQ(8u, results.size());
}

TEST_F(CapResultsTest, NewTabModeCapsAtEight) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kNewTab);

  EXPECT_EQ(8u, results.size());
}

TEST_F(CapResultsTest, AIModeCapsAtEight) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kAI);

  EXPECT_EQ(8u, results.size());
}

class ParseTypeTest : public ::testing::Test {};

TEST_F(ParseTypeTest, KnownKindsMapToCorrectTypes) {
  EXPECT_EQ(CommandSuggestionType::kTab,
            MahoCommandModel::ParseType("tab"));
  EXPECT_EQ(CommandSuggestionType::kBookmark,
            MahoCommandModel::ParseType("bookmark"));
  EXPECT_EQ(CommandSuggestionType::kHistory,
            MahoCommandModel::ParseType("history"));
  EXPECT_EQ(CommandSuggestionType::kAction,
            MahoCommandModel::ParseType("action"));
  EXPECT_EQ(CommandSuggestionType::kNavigation,
            MahoCommandModel::ParseType("navigation"));
  EXPECT_EQ(CommandSuggestionType::kCalculator,
            MahoCommandModel::ParseType("calculator"));
  EXPECT_EQ(CommandSuggestionType::kUnitConversion,
            MahoCommandModel::ParseType("unitconversion"));
  EXPECT_EQ(CommandSuggestionType::kArchivedTab,
            MahoCommandModel::ParseType("archivedtab"));
  EXPECT_EQ(CommandSuggestionType::kClosedTab,
            MahoCommandModel::ParseType("closedtab"));
  EXPECT_EQ(CommandSuggestionType::kFolder,
            MahoCommandModel::ParseType("folder"));
  EXPECT_EQ(CommandSuggestionType::kRecentSearch,
            MahoCommandModel::ParseType("recentsearch"));
}

TEST_F(ParseTypeTest, UnknownKindFallsBackToHistory) {
  EXPECT_EQ(CommandSuggestionType::kHistory,
            MahoCommandModel::ParseType("unknown_kind"));
  EXPECT_EQ(CommandSuggestionType::kHistory,
            MahoCommandModel::ParseType(""));
  EXPECT_EQ(CommandSuggestionType::kHistory,
            MahoCommandModel::ParseType("TAB"));
}

class BuildTabEntryDictTest : public ::testing::Test {};

TEST_F(BuildTabEntryDictTest, ProducesExpectedFields) {
  base::Value entry = MahoCommandModel::BuildTabEntryDict(
      "42:7", "42", "7", "My Tab Title", "https://example.com/");

  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* id = dict->FindString("id");
  ASSERT_NE(nullptr, id);
  EXPECT_EQ("42:7", *id);

  const std::string* browser_session_id = dict->FindString("browser_session_id");
  ASSERT_NE(nullptr, browser_session_id);
  EXPECT_EQ("42", *browser_session_id);

  const std::string* tab_session_id = dict->FindString("tab_session_id");
  ASSERT_NE(nullptr, tab_session_id);
  EXPECT_EQ("7", *tab_session_id);

  const std::string* title = dict->FindString("title");
  ASSERT_NE(nullptr, title);
  EXPECT_EQ("My Tab Title", *title);

  const std::string* url = dict->FindString("url");
  ASSERT_NE(nullptr, url);
  EXPECT_EQ("https://example.com/", *url);
}

TEST_F(BuildTabEntryDictTest, NoIconFieldPresent) {
  base::Value entry = MahoCommandModel::BuildTabEntryDict(
      "1:2", "1", "2", "Title", "https://test.com/");

  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);
  EXPECT_EQ(nullptr, dict->Find("icon"));
}

TEST_F(BuildTabEntryDictTest, EmptyFieldsProduceEmptyStrings) {
  base::Value entry =
      MahoCommandModel::BuildTabEntryDict("", "", "", "", "");

  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* id = dict->FindString("id");
  ASSERT_NE(nullptr, id);
  EXPECT_EQ("", *id);
}

TEST_F(BuildTabEntryDictTest, SessionIdsMatchStableIdComponents) {
  base::Value entry = MahoCommandModel::BuildTabEntryDict(
      "99:42", "99", "42", "Session Tab", "https://session.test/");

  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* id = dict->FindString("id");
  const std::string* browser_session_id = dict->FindString("browser_session_id");
  const std::string* tab_session_id = dict->FindString("tab_session_id");
  ASSERT_NE(nullptr, id);
  ASSERT_NE(nullptr, browser_session_id);
  ASSERT_NE(nullptr, tab_session_id);

  EXPECT_EQ(*browser_session_id + ":" + *tab_session_id, *id);
}

// ---------------------------------------------------------------------------
// Session-id parsing / disposition behavior (non-UI regression coverage)
//
// The key invariant: browser_session_id and tab_session_id are decimal
// strings of numeric IDs.  FindTabIndexForStableId() (production code) calls
// base::StringToInt64() on tab_session_id — non-numeric values must produce
// kNoTab (-1) without crashing.  BuildTabEntryDict() is the canonical
// source of these fields, so we verify the format it emits.
// ---------------------------------------------------------------------------

class SessionIdParsingTest : public ::testing::Test {};

TEST_F(SessionIdParsingTest, NumericSessionIdsRoundTripAsDecimalStrings) {
  const std::string browser_sid = "12345";
  const std::string tab_sid = "67890";
  base::Value entry = MahoCommandModel::BuildTabEntryDict(
      browser_sid + ":" + tab_sid, browser_sid, tab_sid,
      "Title", "https://example.com/");

  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* bsid = dict->FindString("browser_session_id");
  const std::string* tsid = dict->FindString("tab_session_id");
  ASSERT_NE(nullptr, bsid);
  ASSERT_NE(nullptr, tsid);

  int64_t browser_val = 0;
  int64_t tab_val = 0;
  EXPECT_TRUE(base::StringToInt64(*bsid, &browser_val))
      << "browser_session_id must be parseable as int64";
  EXPECT_TRUE(base::StringToInt64(*tsid, &tab_val))
      << "tab_session_id must be parseable as int64";
  EXPECT_EQ(12345, browser_val);
  EXPECT_EQ(67890, tab_val);
}

TEST_F(SessionIdParsingTest, EmptySessionIdIsNotParsableAsInt64) {
  int64_t val = 0;
  EXPECT_FALSE(base::StringToInt64(std::string(), &val))
      << "Empty tab_session_id must fail StringToInt64, causing "
         "FindTabIndexForStableId to return kNoTab without crashing";
}

TEST_F(SessionIdParsingTest, NonNumericSessionIdIsNotParsableAsInt64) {
  int64_t val = 0;
  EXPECT_FALSE(base::StringToInt64("not-a-number", &val))
      << "Non-numeric tab_session_id must fail StringToInt64";
}

TEST_F(SessionIdParsingTest, ZeroSessionIdIsParsableButRepresentsInvalidId) {
  int64_t val = -1;
  EXPECT_TRUE(base::StringToInt64("0", &val));
  EXPECT_EQ(0, val)
      << "Session ID 0 parses correctly; production code must not confuse it "
         "with a valid Chromium session ID (which start at 1)";
}

TEST_F(SessionIdParsingTest, LargeNumericSessionIdRoundTrips) {
  const int64_t large_id = 999999999LL;
  const std::string large_str = base::NumberToString(large_id);
  int64_t parsed = 0;
  EXPECT_TRUE(base::StringToInt64(large_str, &parsed));
  EXPECT_EQ(large_id, parsed);
}

TEST_F(SessionIdParsingTest, StableIdFormatIsBrowserColonTab) {
  base::Value entry = MahoCommandModel::BuildTabEntryDict(
      "7:3", "7", "3", "T", "https://t.test/");
  const base::DictValue* dict = entry.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* id = dict->FindString("id");
  ASSERT_NE(nullptr, id);
  EXPECT_EQ("7:3", *id)
      << "Stable ID must be browser_session_id:tab_session_id";
  EXPECT_EQ(1u, std::count(id->begin(), id->end(), ':'))
      << "Stable ID must contain exactly one colon separator";
}

TEST_F(SessionIdParsingTest,
       BrowserSessionIdMismatchMeansTabNotFoundInThisBrowser) {
  const std::string browser_sid_a = "100";
  const std::string browser_sid_b = "200";
  const std::string tab_sid = "42";

  base::Value entry_a = MahoCommandModel::BuildTabEntryDict(
      browser_sid_a + ":" + tab_sid, browser_sid_a, tab_sid,
      "Tab A", "https://a.test/");

  const base::DictValue* dict = entry_a.GetIfDict();
  ASSERT_NE(nullptr, dict);

  const std::string* bsid = dict->FindString("browser_session_id");
  ASSERT_NE(nullptr, bsid);

  EXPECT_NE(*bsid, browser_sid_b)
      << "A tab entry built for browser 100 must not match browser 200; "
         "this guards the production-code cross-browser activation bypass "
         "in OpenCommandSuggestion";
}

// ---------------------------------------------------------------------------
// MahoCommandModel::Search — empty-query timing behavior
//
// These tests use a real MahoCommandModel with browser=nullptr.  The
// empty-query path in Search() only touches internal fields (query_, mode_,
// results_, pending_callback_, debounce_timer_) and never dereferences
// browser_, so nullptr is safe here.
//
// FireSearch() would crash on browser_->profile() if allowed to run, so the
// kNewTab tests must call model.Cancel() before the debounce timer fires.
// kDebounceDelay is 150 ms (private constant in maho_command_model.cc).
// ---------------------------------------------------------------------------

namespace {
constexpr base::TimeDelta kModelDebounceDelay = base::Milliseconds(150);
}  // namespace

class SearchEmptyQueryTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(SearchEmptyQueryTest,
       EmptyQueryInNewTabMode_CallbackNotFiredSynchronously) {
  MahoCommandModel model(/*browser=*/nullptr);

  int callback_count = 0;
  auto cb = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++(*count); },
      &callback_count);

  model.Search("", CommandOverlayMode::kNewTab, cb);

  EXPECT_EQ(0, callback_count)
      << "kNewTab empty-query must not fire the callback synchronously; "
         "it must schedule a debounced FireSearch via OneShotTimer";

  model.Cancel();
}

TEST_F(SearchEmptyQueryTest,
       EmptyQueryInNewTabMode_CallbackNotFiredBeforeDebounceDelay) {
  MahoCommandModel model(/*browser=*/nullptr);

  int callback_count = 0;
  auto cb = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++(*count); },
      &callback_count);

  model.Search("", CommandOverlayMode::kNewTab, cb);
  ASSERT_EQ(0, callback_count);

  task_env_.FastForwardBy(kModelDebounceDelay - base::Milliseconds(1));

  EXPECT_EQ(0, callback_count)
      << "kNewTab empty-query callback must not fire until the full "
      << kModelDebounceDelay.InMilliseconds() << " ms debounce delay elapses";

  model.Cancel();
}

TEST_F(SearchEmptyQueryTest,
       EmptyQueryInCurrentTabMode_CallbackFiredSynchronouslyWithEmptyResults) {
  MahoCommandModel model(/*browser=*/nullptr);

  int callback_count = 0;
  std::vector<CommandSuggestion> received_results;
  auto cb = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>* out,
         std::vector<CommandSuggestion> results) {
        ++(*count);
        *out = std::move(results);
      },
      &callback_count, &received_results);

  model.Search("", CommandOverlayMode::kCurrentTab, cb);

  EXPECT_EQ(1, callback_count)
      << "kCurrentTab empty-query must fire the callback synchronously, "
         "not via the debounce timer";
  EXPECT_TRUE(received_results.empty())
      << "kCurrentTab empty-query callback must deliver an empty result vector";
}

class QuickActionGateTest : public ::testing::Test {};

TEST_F(QuickActionGateTest, ShortQuerySuppressesActionInCurrentTabMode) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"));

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kCurrentTab, "ab");

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("hist:0", results[0].key);
}

TEST_F(QuickActionGateTest, ProtocolLikeInputSuppressesActionInNewTabMode) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kNavigation, "nav:0"));

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kNewTab, "https://example.com");

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("nav:0", results[0].key);
}

TEST_F(QuickActionGateTest, CommandsOnlyModeDoesNotSuppressActionForShortOrProtocolInput) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:1"));

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kCommandsOnly, "ab");
  ASSERT_EQ(2u, results.size());

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kCommandsOnly, "https://example.com");
  ASSERT_EQ(2u, results.size());
}

TEST_F(QuickActionGateTest, TypedTextFallbackPreserved) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kNavigation, "nav:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kSearch, "search:0"));

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kCurrentTab, "ab");

  ASSERT_EQ(2u, results.size());
  EXPECT_EQ("nav:0", results[0].key);
  EXPECT_EQ("search:0", results[1].key);
}

TEST_F(QuickActionGateTest, CalculatorAndUnitConversionPreserved) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kCalculator, "calc:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kUnitConversion, "unit:0"));

  MahoCommandModel::ApplyQuickActionGate(
      results, CommandOverlayMode::kCurrentTab, "ab");

  ASSERT_EQ(2u, results.size());
  EXPECT_EQ("calc:0", results[0].key);
  EXPECT_EQ("unit:0", results[1].key);
}

}  // namespace
}  // namespace maho

