// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_mail_command_catalog.h"
#include "maho/browser/ui/views/command/maho_remote_search_suggestions.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
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
  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");
  EXPECT_TRUE(results.empty());
}

TEST_F(CapResultsTest, InputBelowLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(3u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("hist:0", results[1].key);
  EXPECT_EQ("bm:0", results[2].key);
}

TEST_F(CapResultsTest, InputAtExactLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 5; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(5u, results.size());
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ("hist:" + std::to_string(i), results[i].key);
  }
}

TEST_F(CapResultsTest, InputAboveLimitTruncatesAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 20; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  EXPECT_EQ(5u, results.size());
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

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(5u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("hist:0", results[1].key);
  EXPECT_EQ("bm:0", results[2].key);
  EXPECT_EQ("action:0", results[3].key);
  EXPECT_EQ("nav:0", results[4].key);
}

TEST_F(CapResultsTest, MixedTypesPreserveRustRankingOrder) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:rank1"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:rank2"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kBookmark, "bm:rank3"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kHistory, "hist:rank4"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kTab, "tab:rank5"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(5u, results.size());
  EXPECT_EQ("hist:rank1", results[0].key);
  EXPECT_EQ("tab:rank2", results[1].key);
  EXPECT_EQ("bm:rank3", results[2].key);
  EXPECT_EQ("hist:rank4", results[3].key);
  EXPECT_EQ("tab:rank5", results[4].key);
}

TEST_F(CapResultsTest, ActionOnlyInputAboveLimitTruncatesToFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(5u, results.size());
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ("action:" + std::to_string(i), results[i].key);
  }
}

TEST_F(CapResultsTest, ActionOnlyInputAtExactLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 5; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(5u, results.size());
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ("action:" + std::to_string(i), results[i].key);
  }
}

TEST_F(CapResultsTest, ActionOnlyInputBelowLimitIsUnchanged) {
  std::vector<CommandSuggestion> results;
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:0"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:1"));
  results.push_back(MakeSuggestion(CommandSuggestionType::kAction, "action:2"));

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  ASSERT_EQ(3u, results.size());
  EXPECT_EQ("action:0", results[0].key);
  EXPECT_EQ("action:1", results[1].key);
  EXPECT_EQ("action:2", results[2].key);
}

TEST_F(CapResultsTest, CommandsOnlyModeCapsAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 30; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kCommandsOnly,
                               "query");

  EXPECT_EQ(5u, results.size());
}

TEST_F(CapResultsTest, SiteSearchModeCapsAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 30; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSiteSearch,
                               "query");

  EXPECT_EQ(5u, results.size());
}

TEST_F(CapResultsTest, CurrentTabModeCapsAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kCurrentTab,
                               "query");

  EXPECT_EQ(5u, results.size());
}

TEST_F(CapResultsTest, NewTabModeCapsAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kNewTab, "query");

  EXPECT_EQ(5u, results.size());
}

TEST_F(CapResultsTest, SearchModeCapsAtFive) {
  std::vector<CommandSuggestion> results;
  for (int i = 0; i < 15; ++i) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kHistory,
                                     "hist:" + std::to_string(i)));
  }

  MahoCommandModel::CapResults(results, CommandOverlayMode::kSearch, "query");

  EXPECT_EQ(5u, results.size());
}

TEST_F(CapResultsTest, EmptyQueryBucketsNewTabResults) {
  std::vector<CommandSuggestion> results = {
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:1"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:1"),
      MakeSuggestion(CommandSuggestionType::kAction, "action:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:2"),
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:2"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bm:1"),
  };

  MahoCommandModel::CapResults(results, CommandOverlayMode::kNewTab, "");

  ASSERT_EQ(5u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("tab:1", results[1].key);
  EXPECT_EQ("hist:0", results[2].key);
  EXPECT_EQ("hist:1", results[3].key);
  EXPECT_EQ("bm:0", results[4].key);
}

TEST_F(CapResultsTest, EmptyQueryBucketsCurrentTabResults) {
  std::vector<CommandSuggestion> results = {
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:1"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:1"),
      MakeSuggestion(CommandSuggestionType::kAction, "action:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:2"),
      MakeSuggestion(CommandSuggestionType::kHistory, "hist:2"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bm:1"),
  };

  MahoCommandModel::CapResults(results, CommandOverlayMode::kCurrentTab, "");

  ASSERT_EQ(5u, results.size());
  EXPECT_EQ("tab:0", results[0].key);
  EXPECT_EQ("tab:1", results[1].key);
  EXPECT_EQ("hist:0", results[2].key);
  EXPECT_EQ("hist:1", results[3].key);
  EXPECT_EQ("bm:0", results[4].key);
}

TEST_F(CapResultsTest, NonEmptyQueriesDoNotEngageTabModeBucketing) {
  for (CommandOverlayMode mode : {CommandOverlayMode::kNewTab,
                                  CommandOverlayMode::kCurrentTab}) {
    std::vector<CommandSuggestion> results = {
        MakeSuggestion(CommandSuggestionType::kHistory, "hist:0"),
        MakeSuggestion(CommandSuggestionType::kBookmark, "bm:0"),
        MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
        MakeSuggestion(CommandSuggestionType::kHistory, "hist:1"),
        MakeSuggestion(CommandSuggestionType::kTab, "tab:1"),
        MakeSuggestion(CommandSuggestionType::kAction, "action:0"),
    };

    MahoCommandModel::CapResults(results, mode, "query");

    ASSERT_EQ(5u, results.size());
    EXPECT_EQ("hist:0", results[0].key);
    EXPECT_EQ("bm:0", results[1].key);
    EXPECT_EQ("tab:0", results[2].key);
    EXPECT_EQ("hist:1", results[3].key);
    EXPECT_EQ("tab:1", results[4].key);
  }
}

class ParseTypeTest : public ::testing::Test {};

TEST(MailCommandAvailabilityTest, RemovesAllMailActionsWhenUnavailable) {
  std::vector<CommandSuggestion> results;
  for (const MahoMailCommand& command : kMahoMailCommands) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::string(command.action_id)));
  }
  results.push_back(
      MakeSuggestion(CommandSuggestionType::kAction, "action:settings"));

  MahoCommandModel::ApplyMailAvailabilityGate(results, false);

  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("action:settings", results[0].key);
}

TEST(MailCommandAvailabilityTest, PreservesMailActionsWhenAvailable) {
  std::vector<CommandSuggestion> results;
  for (const MahoMailCommand& command : kMahoMailCommands) {
    results.push_back(MakeSuggestion(CommandSuggestionType::kAction,
                                     "action:" + std::string(command.action_id)));
  }

  MahoCommandModel::ApplyMailAvailabilityGate(results, true);

  ASSERT_EQ(kMahoMailCommands.size(), results.size());
}

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
       EmptyQueryInCurrentTabMode_CallbackNotFiredSynchronously) {
  MahoCommandModel model(/*browser=*/nullptr);

  int callback_count = 0;
  auto cb = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++(*count); },
      &callback_count);

  model.Search("", CommandOverlayMode::kCurrentTab, cb);

  // Ctrl+L opens with the URL selected; clearing it must show the same
  // debounced default suggestions as a new tab, not an empty list.
  EXPECT_EQ(0, callback_count)
      << "kCurrentTab empty-query must schedule a debounced FireSearch like "
         "kNewTab instead of firing an empty result synchronously";

  model.Cancel();
}

class MahoCommandModelCoreReadyTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

// Regression: the startup palette auto-opens (kNewTab mode) while maho-core
// is still initializing asynchronously (storage dir + vault key derivation +
// LoadState). Its initial search observed GetCore() == null and published an
// empty snapshot that stayed on screen until the user typed. The model must
// subscribe to the core-ready notification and re-issue the pending query.
TEST_F(MahoCommandModelCoreReadyTest, CoreReadyRefetchesCurrentQuery) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Phase 1: initial search races ahead of the core and completes empty.
  MahoCommandModel model(/*browser=*/nullptr);
  int callback_count = 0;
  auto cb = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++(*count); },
      &callback_count);
  model.Search("", CommandOverlayMode::kNewTab, cb);
  task_env_.FastForwardBy(kModelDebounceDelay);
  ASSERT_EQ(1, callback_count);

  // Phase 2: install a real core. SetCore() synchronously notifies
  // core-ready subscribers (BrowserThread::UI is not initialized in this
  // fixture, so the notify path runs inline).
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "0123456789abcdef"
      "0123456789abcdef"
      "0123456789abcdef"
      "0123456789abcdef"));
  MahoCore* core = maho_core_new_with_storage(
      temp_dir.GetPath().AppendASCII("core-ready.sqlite").AsUTF8Unsafe().c_str());
  ASSERT_NE(core, nullptr);
  MahoCore* saved_core = maho::GetCore();

  const uint32_t generation_before = model.search_generation_for_testing();
  maho::SetCore(core);

  // The core-ready notification must have re-issued the pending query...
  EXPECT_EQ(generation_before + 1, model.search_generation_for_testing());

  // ...and the refetched search must run to completion and publish again.
  task_env_.FastForwardBy(kModelDebounceDelay);
  EXPECT_EQ(2, callback_count);

  maho::SetCore(saved_core);
  maho_core_free(core);
}

// Palettes whose search starts after the core is already up must not be
// disturbed by later core installs: the notify fires only when a search is
// live, and RefetchCurrentQuery is a no-op without a pending callback.
TEST_F(MahoCommandModelCoreReadyTest, CoreReadyWithoutPendingSearchIsNoop) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  MahoCommandModel model(/*browser=*/nullptr);
  // No Search() call: no pending callback, so nothing to refetch.
  const uint32_t generation_before = model.search_generation_for_testing();

  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "0123456789abcdef"
      "0123456789abcdef"
      "0123456789abcdef"
      "0123456789abcdef"));
  MahoCore* core = maho_core_new_with_storage(
      temp_dir.GetPath().AppendASCII("core-ready-noop.sqlite").AsUTF8Unsafe()
          .c_str());
  ASSERT_NE(core, nullptr);
  MahoCore* saved_core = maho::GetCore();

  maho::SetCore(core);
  EXPECT_EQ(generation_before, model.search_generation_for_testing());

  maho::SetCore(saved_core);
  maho_core_free(core);
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

class MahoCommandModelTask4Test : public ::testing::Test {};

CommandSuggestion MakeTask4Local(int index) {
  CommandSuggestion suggestion = MakeSuggestion(
      CommandSuggestionType::kHistory, "local:" + std::to_string(index));
  suggestion.subtitle = "Local subtitle " + std::to_string(index);
  suggestion.execution_payload =
      "https://local.example/" + std::to_string(index);
  return suggestion;
}

CommandSuggestion MakeTask4LocalSearch(const std::string& terms,
                                       const std::string& destination) {
  CommandSuggestion suggestion = MakeSuggestion(
      CommandSuggestionType::kSearch, "search:" + terms);
  suggestion.title = terms + " — Search with Local";
  suggestion.execution_payload = destination;
  return suggestion;
}

RemoteSearchSuggestion MakeTask4Remote(
    const std::string& terms,
    const std::string& destination,
    const std::string& title = std::string(),
    std::optional<std::string> subtitle = std::nullopt) {
  return RemoteSearchSuggestion(terms, title.empty() ? terms : title,
                                std::move(subtitle), GURL(destination));
}

std::vector<CommandSuggestion> MakeTask4Locals(size_t count) {
  std::vector<CommandSuggestion> locals;
  for (size_t i = 0; i < count; ++i) {
    locals.push_back(MakeTask4Local(static_cast<int>(i)));
  }
  return locals;
}

std::vector<CommandSuggestion> MakeTask4RemoteRows(size_t count) {
  std::vector<RemoteSearchSuggestion> remotes;
  for (size_t i = 0; i < count; ++i) {
    remotes.push_back(MakeTask4Remote(
        "remote " + std::to_string(i),
        "https://search.example/?q=remote" + std::to_string(i)));
  }
  return MahoCommandModel::NormalizeRemoteSuggestions(
      "current query", {}, remotes);
}

TEST_F(MahoCommandModelTask4Test, PrepareLocalCandidatesPreservesPolicyWithoutCap) {
  std::vector<CommandSuggestion> actions_only = MakeTask4Locals(6);
  actions_only.push_back(
      MakeSuggestion(CommandSuggestionType::kAction, "action:keep"));
  actions_only = MahoCommandModel::PrepareLocalCandidates(
      std::move(actions_only), /*actions_only=*/true,
      CommandOverlayMode::kSearch, "query");
  ASSERT_EQ(1u, actions_only.size());
  EXPECT_EQ("action:keep", actions_only[0].key);

  std::vector<CommandSuggestion> navigation = MakeTask4Locals(6);
  navigation.push_back(MakeTask4LocalSearch(
      "example.com/path", "https://search.example/?q=example"));
  navigation = MahoCommandModel::PrepareLocalCandidates(
      std::move(navigation), /*actions_only=*/false,
      CommandOverlayMode::kSearch, "example.com/path");
  ASSERT_EQ(8u, navigation.size());
  EXPECT_EQ(CommandSuggestionType::kNavigation, navigation[0].type);
  // Scheme-less input canonicalizes to http:// like the omnibox; HTTPS-Upgrades
  // upgrades it at navigation time with an http fallback.
  EXPECT_EQ("http://example.com/path", navigation[0].execution_payload);

  navigation = MahoCommandModel::PrepareLocalCandidates(
      std::move(navigation), /*actions_only=*/false,
      CommandOverlayMode::kSearch, "example.com/path");
  ASSERT_EQ(8u, navigation.size());
  EXPECT_EQ(1, std::count_if(
                   navigation.begin(), navigation.end(),
                   [](const CommandSuggestion& suggestion) {
                     return suggestion.type ==
                            CommandSuggestionType::kNavigation;
                   }));
}

TEST_F(MahoCommandModelTask4Test, FullAllocationMatrixIsCappedAndRemoteBottomContiguous) {
  for (size_t local_count = 0; local_count <= 6; ++local_count) {
    for (size_t remote_input_count = 0; remote_input_count <= 3;
         ++remote_input_count) {
      std::vector<CommandSuggestion> locals = MakeTask4Locals(local_count);
      std::vector<CommandSuggestion> remote_rows =
          MakeTask4RemoteRows(remote_input_count);
      std::vector<CommandSuggestion> actual =
          MahoCommandModel::AllocateFinalResults(
              locals, remote_rows, CommandOverlayMode::kSearch, "query");

      const size_t expected_remote_count =
          std::min<size_t>(2, remote_input_count);
      const size_t expected_local_count =
          remote_input_count == 0
              ? std::min<size_t>(MahoCommandModel::kMaxVisibleRows,
                                 local_count)
              : std::min<size_t>(MahoCommandModel::kMaxVisibleRows -
                                     expected_remote_count,
                                 local_count);
      SCOPED_TRACE(testing::Message()
                   << "locals=" << local_count
                   << " remotes=" << remote_input_count);
      ASSERT_EQ(expected_local_count + expected_remote_count, actual.size());
      EXPECT_LE(actual.size(),
                static_cast<size_t>(MahoCommandModel::kMaxVisibleRows));
      for (size_t i = 0; i < expected_local_count; ++i) {
        EXPECT_EQ("local:" + std::to_string(i), actual[i].key);
      }
      for (size_t i = 0; i < expected_remote_count; ++i) {
        EXPECT_EQ("remote-search:remote " + std::to_string(i),
                  actual[expected_local_count + i].key);
      }
    }
  }
}

TEST_F(MahoCommandModelTask4Test, ApprovedAllocationExamplesHaveExactKeys) {
  struct Case {
    size_t locals;
    size_t remotes;
    std::vector<std::string> expected_keys;
  };
  const std::vector<Case> cases = {
      {5, 2, {"local:0", "local:1", "local:2",
              "remote-search:remote 0", "remote-search:remote 1"}},
      {4, 1, {"local:0", "local:1", "local:2", "local:3",
              "remote-search:remote 0"}},
      {3, 2, {"local:0", "local:1", "local:2",
              "remote-search:remote 0", "remote-search:remote 1"}},
      {2, 2, {"local:0", "local:1", "remote-search:remote 0",
              "remote-search:remote 1"}},
  };

  for (const Case& test_case : cases) {
    std::vector<CommandSuggestion> actual =
        MahoCommandModel::AllocateFinalResults(
            MakeTask4Locals(test_case.locals),
            MakeTask4RemoteRows(test_case.remotes),
            CommandOverlayMode::kSearch, "query");
    std::vector<std::string> actual_keys;
    for (const auto& suggestion : actual) {
      actual_keys.push_back(suggestion.key);
    }
    EXPECT_EQ(test_case.expected_keys, actual_keys);
  }
}

TEST_F(MahoCommandModelTask4Test, NoRemoteUsesDirectCapResultsIncludingEmptyBuckets) {
  std::vector<CommandSuggestion> ranked = MakeTask4Locals(7);
  std::vector<CommandSuggestion> expected_ranked = ranked;
  MahoCommandModel::CapResults(expected_ranked, CommandOverlayMode::kSearch,
                               "query");
  EXPECT_TRUE(MahoCommandModel::FinalSnapshotsEqual(
      expected_ranked, MahoCommandModel::AllocateFinalResults(
                           ranked, {}, CommandOverlayMode::kSearch, "query")));

  std::vector<CommandSuggestion> bucketed = {
      MakeSuggestion(CommandSuggestionType::kHistory, "history:0"),
      MakeSuggestion(CommandSuggestionType::kHistory, "history:1"),
      MakeSuggestion(CommandSuggestionType::kHistory, "history:2"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:1"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bookmark:0"),
  };
  std::vector<CommandSuggestion> expected_bucketed = bucketed;
  MahoCommandModel::CapResults(expected_bucketed,
                               CommandOverlayMode::kNewTab, "");
  std::vector<CommandSuggestion> actual_bucketed =
      MahoCommandModel::AllocateFinalResults(
          bucketed, {}, CommandOverlayMode::kNewTab, "");
  ASSERT_TRUE(MahoCommandModel::FinalSnapshotsEqual(expected_bucketed,
                                                     actual_bucketed));
  ASSERT_EQ(5u, actual_bucketed.size());
  EXPECT_EQ("tab:0", actual_bucketed[0].key);
  EXPECT_EQ("tab:1", actual_bucketed[1].key);
  EXPECT_EQ("history:0", actual_bucketed[2].key);
  EXPECT_EQ("history:1", actual_bucketed[3].key);
  EXPECT_EQ("bookmark:0", actual_bucketed[4].key);
}

TEST_F(MahoCommandModelTask4Test, NormalizesMapsAndDeduplicatesRemoteCandidates) {
  std::vector<CommandSuggestion> locals = {
      MakeTask4LocalSearch("Local Search", "https://local.example/search"),
      MakeTask4Local(0),
  };
  std::vector<RemoteSearchSuggestion> remotes = {
      MakeTask4Remote("  CURRENT QUERY  ",
                      "https://search.example/current"),
      MakeTask4Remote(" LOCAL SEARCH ", "https://search.example/local"),
      MakeTask4Remote("Accepted", "https://search.example/accepted",
                      "Provider title", "Provider description"),
      MakeTask4Remote(" accepted ", "https://search.example/other"),
      MakeTask4Remote("Different text", "https://search.example/accepted"),
      MakeTask4Remote("Fallback subtitle", "https://search.example/fallback",
                      "Fallback title", "   "),
  };

  std::vector<CommandSuggestion> actual =
      MahoCommandModel::NormalizeRemoteSuggestions(
          " Current Query ", locals, remotes);

  ASSERT_EQ(2u, actual.size());
  EXPECT_EQ(CommandSuggestionType::kSearch, actual[0].type);
  EXPECT_EQ("remote-search:accepted", actual[0].key);
  EXPECT_EQ("Provider title", actual[0].title);
  EXPECT_EQ("Provider description", actual[0].subtitle);
  EXPECT_EQ("https://search.example/accepted", actual[0].execution_payload);
  EXPECT_EQ("remote-search:fallback subtitle", actual[1].key);
  EXPECT_EQ("Fallback title", actual[1].title);
  EXPECT_EQ("search.example", actual[1].subtitle);
}

TEST_F(MahoCommandModelTask4Test, RejectsLocalAndRemoteTextOrCanonicalUrlDuplicates) {
  std::vector<CommandSuggestion> locals = {
      MakeTask4LocalSearch("Local terms", "https://example.test/path"),
  };
  std::vector<RemoteSearchSuggestion> remotes = {
      MakeTask4Remote("local terms", "https://different.test/search"),
      MakeTask4Remote("different terms", "https://example.test/path"),
      MakeTask4Remote("accepted", "https://example.test:443/accepted"),
      MakeTask4Remote("same url different text",
                      "https://example.test/accepted"),
      MakeTask4Remote("ACCEPTED", "https://another.test/search"),
      MakeTask4Remote("same text different url",
                      "https://different.test/accepted"),
  };

  std::vector<CommandSuggestion> actual =
      MahoCommandModel::NormalizeRemoteSuggestions("query", locals, remotes);

  ASSERT_EQ(2u, actual.size());
  EXPECT_EQ("remote-search:accepted", actual[0].key);
  EXPECT_EQ("remote-search:same text different url", actual[1].key);
}

TEST_F(MahoCommandModelTask4Test, FinalSnapshotEqualityUsesOnlyFourPayloadFields) {
  CommandSuggestion original = MakeTask4Local(0);
  original.title = "Title";
  original.subtitle = "Subtitle";
  original.execution_payload = "https://example.test/original";
  std::vector<CommandSuggestion> baseline = {original};

  CommandSuggestion icon_only = original;
  ImageData icon;
  icon.format = "test";
  icon_only.icon = std::move(icon);
  EXPECT_TRUE(MahoCommandModel::FinalSnapshotsEqual(baseline, {icon_only}));

  CommandSuggestion changed = original;
  changed.key = "changed-key";
  EXPECT_FALSE(MahoCommandModel::FinalSnapshotsEqual(baseline, {changed}));
  changed = original;
  changed.title = "Changed title";
  EXPECT_FALSE(MahoCommandModel::FinalSnapshotsEqual(baseline, {changed}));
  changed = original;
  changed.subtitle = "Changed subtitle";
  EXPECT_FALSE(MahoCommandModel::FinalSnapshotsEqual(baseline, {changed}));
  changed = original;
  changed.execution_payload = "https://example.test/changed";
  EXPECT_FALSE(MahoCommandModel::FinalSnapshotsEqual(baseline, {changed}));
  EXPECT_FALSE(MahoCommandModel::FinalSnapshotsEqual(baseline, {}));
}

// ---------------------------------------------------------------------------
// Private-context (exact-primary-Incognito) contract — task-10-command-unit
// ---------------------------------------------------------------------------

class MahoCommandModelTest : public ::testing::Test {};

class MahoRemoteSearchSuggestionsTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

class LateNotifyingRemoteSearchController final
    : public MahoRemoteSearchSuggestions::ControllerForTesting {
 public:
  LateNotifyingRemoteSearchController() = default;
  ~LateNotifyingRemoteSearchController() override = default;

  void SetResultChangedCallback(ResultChangedCallback callback) override {
    if (callback) {
      late_result_changed_callback_ = callback;
    }
    result_changed_callback_ = std::move(callback);
  }

  void Start(const std::string& query) override {
    last_query_ = query;
    ++start_count_;
  }

  void Stop() override { ++stop_count_; }

  void EmitResultChanged() {
    if (result_changed_callback_) {
      result_changed_callback_.Run();
    }
  }

  void EmitLateResultChanged() {
    late_result_changed_attempted_ = true;
    if (late_result_changed_callback_) {
      late_result_changed_callback_.Run();
    }
  }

  int start_count() const { return start_count_; }
  int stop_count() const { return stop_count_; }
  bool has_result_changed_callback() const {
    return static_cast<bool>(result_changed_callback_);
  }
  bool late_result_changed_attempted() const {
    return late_result_changed_attempted_;
  }

 private:
  std::string last_query_;
  ResultChangedCallback result_changed_callback_;
  ResultChangedCallback late_result_changed_callback_;
  int start_count_ = 0;
  int stop_count_ = 0;
  bool late_result_changed_attempted_ = false;
};

TEST_F(MahoRemoteSearchSuggestionsTest,
       ProductionAdapterUsesSearchOnlyControllerConfig) {
  EXPECT_EQ(AutocompleteProvider::TYPE_SEARCH,
            MahoRemoteSearchSuggestions::ProviderTypesForTesting());
}

TEST_F(MahoRemoteSearchSuggestionsTest,
       ProductionAdapterSuppressesLateControllerNotificationAfterCancel) {
  auto controller = std::make_unique<LateNotifyingRemoteSearchController>();
  LateNotifyingRemoteSearchController* controller_driver = controller.get();
  std::unique_ptr<MahoRemoteSearchSuggestions> source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));

  int callback_count = 0;
  source->Start("weather", base::BindRepeating(
                               [](int* count,
                                  std::vector<RemoteSearchSuggestion>) {
                                 ++*count;
                               },
                               &callback_count));
  EXPECT_EQ(1, controller_driver->start_count());

  source->Cancel();
  EXPECT_EQ(1, controller_driver->stop_count());
  EXPECT_FALSE(controller_driver->has_result_changed_callback());
  controller_driver->EmitLateResultChanged();

  EXPECT_TRUE(controller_driver->late_result_changed_attempted());
  EXPECT_EQ(0, callback_count);
}

TEST_F(MahoRemoteSearchSuggestionsTest,
       ProductionAdapterRestartsAfterCancel) {
  auto controller = std::make_unique<LateNotifyingRemoteSearchController>();
  LateNotifyingRemoteSearchController* controller_driver = controller.get();
  std::unique_ptr<MahoRemoteSearchSuggestions> source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));

  int callback_count = 0;
  auto callback = base::BindRepeating(
      [](int* count, std::vector<RemoteSearchSuggestion>) { ++*count; },
      &callback_count);
  source->Start("weather", callback);
  source->Cancel();
  source->Start("news", callback);
  controller_driver->EmitResultChanged();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(2, controller_driver->start_count());
  EXPECT_TRUE(controller_driver->has_result_changed_callback());
  EXPECT_EQ(1, callback_count);
}

struct ReentrantControllerState {
  int destroy_count = 0;
  int start_return_count = 0;
  int stream_return_count = 0;
  std::string terms = "weather today";
};

class ReentrantNotificationController final
    : public MahoRemoteSearchSuggestions::ControllerForTesting {
 public:
  explicit ReentrantNotificationController(ReentrantControllerState* state)
      : state_(state) {}
  ~ReentrantNotificationController() override { ++state_->destroy_count; }

  void SetResultChangedCallback(ResultChangedCallback callback) override {
    callback_ = std::move(callback);
  }

  void Start(const std::string& query) override {
    ReentrantControllerState* state = state_;
    if (callback_) {
      callback_.Run();
    }
    ++state->start_return_count;
  }

  void Stop() override {}

  std::vector<RemoteSearchSuggestion> GetSuggestions() const override {
    return {RemoteSearchSuggestion(
        state_->terms, state_->terms, std::nullopt,
        GURL("https://search.example/?q=" + state_->terms))};
  }

  void EmitStreamedSnapshot(std::string terms) {
    state_->terms = std::move(terms);
    ReentrantControllerState* state = state_;
    if (callback_) {
      callback_.Run();
    }
    ++state->stream_return_count;
  }

 private:
  const raw_ptr<ReentrantControllerState> state_;
  ResultChangedCallback callback_;
};

TEST_F(MahoRemoteSearchSuggestionsTest,
       ConsumerDestructionIsDeferredPastSynchronousStartNotification) {
  ReentrantControllerState state;
  auto controller = std::make_unique<ReentrantNotificationController>(&state);
  std::unique_ptr<MahoRemoteSearchSuggestions> source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));
  int callback_count = 0;

  source->Start(
      "weather",
      base::BindRepeating(
          [](std::unique_ptr<MahoRemoteSearchSuggestions>* source,
             int* callback_count,
             std::vector<RemoteSearchSuggestion>) {
            ++*callback_count;
            source->reset();
          },
          base::Unretained(&source), base::Unretained(&callback_count)));

  EXPECT_TRUE(source);
  EXPECT_EQ(1, state.start_return_count);
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, state.destroy_count);

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(source);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(1, state.destroy_count);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
}

TEST_F(MahoRemoteSearchSuggestionsTest,
       ConsumerDestructionIsDeferredPastStreamedNotification) {
  ReentrantControllerState state;
  auto controller = std::make_unique<ReentrantNotificationController>(&state);
  ReentrantNotificationController* controller_driver = controller.get();
  std::unique_ptr<MahoRemoteSearchSuggestions> source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));
  int callback_count = 0;

  source->Start(
      "weather",
      base::BindRepeating(
          [](std::unique_ptr<MahoRemoteSearchSuggestions>* source,
             int* callback_count,
             std::vector<RemoteSearchSuggestion>) {
            ++*callback_count;
            if (*callback_count == 2) {
              source->reset();
            }
          },
          base::Unretained(&source), base::Unretained(&callback_count)));
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(source);
  ASSERT_EQ(1, callback_count);

  controller_driver->EmitStreamedSnapshot("weather tomorrow");
  EXPECT_TRUE(source);
  EXPECT_EQ(1, state.stream_return_count);
  EXPECT_EQ(1, callback_count);

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(source);
  EXPECT_EQ(2, callback_count);
  EXPECT_EQ(1, state.destroy_count);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(2, callback_count);
}

struct FakeRemoteSourceState {
  int start_count = 0;
  int cancel_count = 0;
  int destroy_count = 0;
};

class LifecycleFakeRemoteSource final
    : public MahoRemoteSearchSuggestionSource {
 public:
  explicit LifecycleFakeRemoteSource(FakeRemoteSourceState* state)
      : state_(state) {}
  ~LifecycleFakeRemoteSource() override { ++state_->destroy_count; }

  void Start(const std::string& query, SnapshotCallback callback) override {
    ++state_->start_count;
    callback_ = std::move(callback);
    if (!synchronous_snapshot_.empty()) {
      callback_.Run(synchronous_snapshot_);
    }
  }

  void Cancel() override {
    ++state_->cancel_count;
    if (emit_during_cancel_ && callback_) {
      callback_.Run(cancel_snapshot_);
    }
  }

  void Emit(std::vector<RemoteSearchSuggestion> snapshot) {
    if (callback_) {
      callback_.Run(std::move(snapshot));
    }
  }

  void set_synchronous_snapshot(
      std::vector<RemoteSearchSuggestion> snapshot) {
    synchronous_snapshot_ = std::move(snapshot);
  }
  void set_cancel_snapshot(std::vector<RemoteSearchSuggestion> snapshot) {
    cancel_snapshot_ = std::move(snapshot);
    emit_during_cancel_ = true;
  }

 private:
  const raw_ptr<FakeRemoteSourceState> state_;
  SnapshotCallback callback_;
  std::vector<RemoteSearchSuggestion> synchronous_snapshot_;
  std::vector<RemoteSearchSuggestion> cancel_snapshot_;
  bool emit_during_cancel_ = false;
};

RemoteSearchSuggestion MakeRemoteSuggestion(const std::string& terms) {
  return RemoteSearchSuggestion(
      terms, terms, std::nullopt,
      GURL("https://search.example/?q=" + terms));
}

class MahoCommandModelLifecycleTask3Test : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

class MahoCommandModelTask4IntegrationTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

class MahoCommandModelUxRefinementsTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

std::vector<std::string> UxRefinementKeys(
    const std::vector<CommandSuggestion>& results) {
  std::vector<std::string> keys;
  for (const CommandSuggestion& result : results) {
    keys.push_back(result.key);
  }
  return keys;
}

TEST_F(MahoCommandModelUxRefinementsTest,
       RemoteSubtitlesPreferProviderThenHostAndNeverArtificialLiteral) {
  std::vector<RemoteSearchSuggestion> remotes = {
      MakeTask4Remote("provider", "https://www.provider.example/search",
                      "Provider", "  Provider supplied text  "),
      MakeTask4Remote("host", "https://www.host.example/search", "Host",
                      " \t "),
      MakeTask4Remote("host no www", "https://direct.example/search"),
      MakeTask4Remote("host unavailable", "file:///search"),
  };

  std::vector<CommandSuggestion> actual =
      MahoCommandModel::NormalizeRemoteSuggestions("query", {}, remotes);

  ASSERT_EQ(4u, actual.size());
  EXPECT_EQ("Provider supplied text", actual[0].subtitle);
  EXPECT_EQ("host.example", actual[1].subtitle);
  EXPECT_EQ("direct.example", actual[2].subtitle);
  EXPECT_TRUE(actual[3].subtitle.empty());
  for (const CommandSuggestion& result : actual) {
    EXPECT_NE("Search suggestion", result.subtitle);
  }
}

TEST_F(MahoCommandModelUxRefinementsTest,
       RemoteRowsFollowTypedSearchAndKeepLocalOrderAndContiguity) {
  std::vector<CommandSuggestion> locals = {
      MakeTask4Local(0),
      MakeTask4LocalSearch("query", "https://local.example/?q=query"),
      MakeSuggestion(CommandSuggestionType::kBookmark, "bookmark:0"),
      MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
      MakeSuggestion(CommandSuggestionType::kHistory, "history:tail"),
  };

  std::vector<CommandSuggestion> actual =
      MahoCommandModel::AllocateFinalResults(
          locals, MakeTask4RemoteRows(2), CommandOverlayMode::kSearch,
          "query");

  EXPECT_EQ((std::vector<std::string>{
                "local:0", "search:query", "remote-search:remote 0",
                "remote-search:remote 1", "bookmark:0"}),
            UxRefinementKeys(actual));
  ASSERT_LE(actual.size(),
            static_cast<size_t>(MahoCommandModel::kMaxVisibleRows));
}

TEST_F(MahoCommandModelUxRefinementsTest,
       RemoteRowsAppendWithoutSurvivingTypedSearchRow) {
  std::vector<CommandSuggestion> no_search = MakeTask4Locals(4);
  EXPECT_EQ((std::vector<std::string>{
                "local:0", "local:1", "local:2", "local:3",
                "remote-search:remote 0"}),
            UxRefinementKeys(MahoCommandModel::AllocateFinalResults(
                no_search, MakeTask4RemoteRows(1),
                CommandOverlayMode::kSearch, "query")));

  std::vector<CommandSuggestion> trimmed_search = MakeTask4Locals(4);
  trimmed_search.push_back(
      MakeTask4LocalSearch("query", "https://local.example/?q=query"));
  EXPECT_EQ((std::vector<std::string>{
                "local:0", "local:1", "local:2",
                "remote-search:remote 0", "remote-search:remote 1"}),
            UxRefinementKeys(MahoCommandModel::AllocateFinalResults(
                trimmed_search, MakeTask4RemoteRows(2),
                CommandOverlayMode::kSearch, "query")));
}

TEST_F(MahoCommandModelUxRefinementsTest,
       FullAllocationMatrixReservesExactLocalSlotsAndCapsAtFive) {
  for (size_t local_count = 0; local_count <= 7; ++local_count) {
    for (size_t remote_input_count = 1; remote_input_count <= 4;
         ++remote_input_count) {
      const size_t remote_count = std::min<size_t>(2, remote_input_count);
      const size_t local_slots =
          MahoCommandModel::kMaxVisibleRows - remote_count;
      std::vector<CommandSuggestion> actual =
          MahoCommandModel::AllocateFinalResults(
              MakeTask4Locals(local_count),
              MakeTask4RemoteRows(remote_input_count),
              CommandOverlayMode::kSearch, "query");

      SCOPED_TRACE(testing::Message()
                   << "locals=" << local_count
                   << " remotes=" << remote_input_count);
      ASSERT_EQ(std::min(local_count, local_slots) + remote_count,
                actual.size());
      EXPECT_LE(actual.size(),
                static_cast<size_t>(MahoCommandModel::kMaxVisibleRows));
      for (size_t i = 0; i < std::min(local_count, local_slots); ++i) {
        EXPECT_EQ("local:" + std::to_string(i), actual[i].key);
      }
    }
  }
}

TEST_F(MahoCommandModelUxRefinementsTest,
       NoValidRemoteIsByteForBehaviorDirectCapIncludingEmptyBuckets) {
  const std::vector<RemoteSearchSuggestion> invalid_remotes = {
      MakeTask4Remote("", "https://empty-terms.example/"),
      MakeTask4Remote("invalid destination", "not a url"),
  };
  ASSERT_TRUE(MahoCommandModel::NormalizeRemoteSuggestions(
                  "query", {}, invalid_remotes)
                  .empty());

  for (const auto& [mode, query] :
       std::vector<std::pair<CommandOverlayMode, std::string>>{
           {CommandOverlayMode::kSearch, "query"},
           {CommandOverlayMode::kNewTab, ""}}) {
    std::vector<CommandSuggestion> locals = {
        MakeSuggestion(CommandSuggestionType::kHistory, "history:0"),
        MakeSuggestion(CommandSuggestionType::kHistory, "history:1"),
        MakeSuggestion(CommandSuggestionType::kHistory, "history:2"),
        MakeSuggestion(CommandSuggestionType::kTab, "tab:0"),
        MakeSuggestion(CommandSuggestionType::kTab, "tab:1"),
        MakeSuggestion(CommandSuggestionType::kBookmark, "bookmark:0"),
    };
    std::vector<CommandSuggestion> expected = locals;
    MahoCommandModel::CapResults(expected, mode, query);
    std::vector<CommandSuggestion> actual =
        MahoCommandModel::AllocateFinalResults(
            locals,
            MahoCommandModel::NormalizeRemoteSuggestions(
                query, locals, invalid_remotes),
            mode, query);
    EXPECT_TRUE(MahoCommandModel::FinalSnapshotsEqual(expected, actual));
  }
}

TEST_F(MahoCommandModelUxRefinementsTest,
       LateFirstRemoteSnapshotIsIgnoredWithoutRepublication) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  model.Search("", CommandOverlayMode::kSearch,
               base::BindRepeating(
                   [](int* count, std::vector<CommandSuggestion>) {
                     ++*count;
                   },
                   &callback_count));
  const uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(3));
  model.StartRemoteSearchForTesting(generation, "query");
  const int count_after_local = callback_count;
  const std::vector<std::string> local_keys =
      UxRefinementKeys(model.results());

  task_environment_.FastForwardBy(base::Milliseconds(601));
  source_driver->Emit({MakeRemoteSuggestion("late remote")});

  EXPECT_EQ(count_after_local, callback_count);
  EXPECT_EQ(local_keys, UxRefinementKeys(model.results()));
  EXPECT_TRUE(model.remote_snapshot_for_testing().empty());
}

TEST_F(MahoCommandModelUxRefinementsTest,
       FirstRemoteSnapshotWithinThresholdIsApplied) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  model.Search("", CommandOverlayMode::kSearch,
               base::BindRepeating(
                   [](int* count, std::vector<CommandSuggestion>) {
                     ++*count;
                   },
                   &callback_count));
  const uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(3));
  model.StartRemoteSearchForTesting(generation, "query");
  const int count_after_local = callback_count;

  task_environment_.FastForwardBy(base::Milliseconds(599));
  source_driver->Emit({MakeRemoteSuggestion("timely remote")});

  EXPECT_EQ(count_after_local + 1, callback_count);
  EXPECT_NE(model.results().end(),
            std::ranges::find_if(model.results(), [](const auto& result) {
              return result.key == "remote-search:timely remote";
            }));
}

TEST_F(MahoCommandModelUxRefinementsTest,
       VisibleRemoteAllowsStreamingUpdatePastThreshold) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  model.Search("", CommandOverlayMode::kSearch,
               base::BindRepeating(
                   [](int* count, std::vector<CommandSuggestion>) {
                     ++*count;
                   },
                   &callback_count));
  const uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(3));
  model.StartRemoteSearchForTesting(generation, "query");
  source_driver->Emit({MakeRemoteSuggestion("remote initial")});
  const int count_after_initial = callback_count;

  task_environment_.FastForwardBy(base::Milliseconds(601));
  source_driver->Emit({MakeRemoteSuggestion("remote refined")});

  EXPECT_EQ(count_after_initial + 1, callback_count);
  EXPECT_NE(model.results().end(),
            std::ranges::find_if(model.results(), [](const auto& result) {
              return result.key == "remote-search:remote refined";
            }));
}

TEST_F(MahoCommandModelUxRefinementsTest,
       LateLocalSnapshotIsNeverSuppressed) {
  MahoCommandModel model(nullptr);
  int callback_count = 0;
  model.Search("", CommandOverlayMode::kSearch,
               base::BindRepeating(
                   [](int* count, std::vector<CommandSuggestion>) {
                     ++*count;
                   },
                   &callback_count));
  const uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(2));
  const int count_after_initial = callback_count;

  task_environment_.FastForwardBy(base::Milliseconds(601));
  std::vector<CommandSuggestion> updated = MakeTask4Locals(2);
  updated[1].title = "updated late local";
  model.AcceptLocalResultsForTesting(generation, std::move(updated));

  EXPECT_EQ(count_after_initial + 1, callback_count);
  ASSERT_EQ(2u, model.results().size());
  EXPECT_EQ("updated late local", model.results()[1].title);
}

TEST_F(MahoCommandModelUxRefinementsTest,
       SelectionKeyIsRestoredAcrossRemoteRepublication) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  model.Search("", CommandOverlayMode::kSearch, base::DoNothing());
  const uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(
      generation,
      {MakeTask4Local(0),
       MakeTask4LocalSearch("query", "https://local.example/?q=query"),
       MakeTask4Local(1)});
  model.set_selected_index(2);
  model.StartRemoteSearchForTesting(generation, "query");

  source_driver->Emit({MakeRemoteSuggestion("remote")});

  ASSERT_EQ(3, model.selected_index());
  EXPECT_EQ("local:1", model.results()[model.selected_index()].key);
}

TEST_F(MahoCommandModelTask4IntegrationTest,
       PublishesFirstSnapshotSuppressesEqualRemoteChurnAndPublishesNextGeneration) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  std::vector<CommandSuggestion> last_results;
  auto callback = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>* received,
         std::vector<CommandSuggestion> results) {
        ++*count;
        *received = std::move(results);
      },
      &callback_count, &last_results);

  model.Search("weather", CommandOverlayMode::kSearch, callback);
  uint32_t generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(5));
  ASSERT_EQ(1, callback_count);
  model.StartRemoteSearchForTesting(generation, "weather");
  std::vector<RemoteSearchSuggestion> remote = {
      MakeTask4Remote("weather tomorrow",
                      "https://search.example/?q=weather+tomorrow")};
  source_driver->Emit(remote);
  ASSERT_EQ(2, callback_count);
  ASSERT_EQ(5u, last_results.size());
  EXPECT_EQ("remote-search:weather tomorrow", last_results.back().key);

  source_driver->Emit(remote);
  EXPECT_EQ(2, callback_count);

  model.Search("weather", CommandOverlayMode::kSearch, callback);
  generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(generation, MakeTask4Locals(5));
  EXPECT_EQ(3, callback_count);
  model.Cancel();
}

struct Task5RemoteSourceState {
  int factory_count = 0;
  int start_count = 0;
  int cancel_count = 0;
  int destroy_count = 0;
  raw_ptr<Profile> factory_profile = nullptr;
  std::vector<std::string> queries;
  std::vector<MahoRemoteSearchSuggestionSource::SnapshotCallback> callbacks;
  std::vector<RemoteSearchSuggestion> synchronous_snapshot;
};

class Task5FakeRemoteSource final : public MahoRemoteSearchSuggestionSource {
 public:
  explicit Task5FakeRemoteSource(Task5RemoteSourceState* state)
      : state_(state) {}
  ~Task5FakeRemoteSource() override { ++state_->destroy_count; }

  void Start(const std::string& query, SnapshotCallback callback) override {
    ++state_->start_count;
    state_->queries.push_back(query);
    state_->callbacks.push_back(std::move(callback));
    if (!state_->synchronous_snapshot.empty()) {
      state_->callbacks.back().Run(state_->synchronous_snapshot);
    }
  }

  void Cancel() override { ++state_->cancel_count; }

 private:
  const raw_ptr<Task5RemoteSourceState> state_;
};

MahoCommandModel::RemoteSourceFactory MakeTask5Factory(
    Task5RemoteSourceState* state) {
  return base::BindRepeating(
      [](Task5RemoteSourceState* state, Profile* profile)
          -> std::unique_ptr<MahoRemoteSearchSuggestionSource> {
        ++state->factory_count;
        state->factory_profile = profile;
        return std::make_unique<Task5FakeRemoteSource>(state);
      },
      base::Unretained(state));
}

std::vector<std::string> Task5Keys(
    const std::vector<CommandSuggestion>& results) {
  std::vector<std::string> keys;
  for (const CommandSuggestion& result : results) {
    keys.push_back(result.key);
  }
  return keys;
}

class MahoCommandModelTask5Test : public BrowserWithTestWindowTest {
 public:
  MahoCommandModelTask5Test()
      : BrowserWithTestWindowTest(
            content::BrowserTaskEnvironment::TimeSource::MOCK_TIME,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED) {}

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    profile()->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, true);
  }

 protected:
  void FireDebounce() { task_environment()->FastForwardBy(kModelDebounceDelay); }

  void ExpectNoRemoteStart(Browser* test_browser,
                           const std::string& query,
                           CommandOverlayMode mode,
                           bool add_site_keyword = false) {
    Task5RemoteSourceState state;
    MahoCommandModel model(test_browser);
    model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
    if (add_site_keyword) {
      model.AddSiteSearchEngineForTest({
          .prefix = "gh",
          .name = "GitHub",
          .url_template = "https://github.com/search?q={searchTerms}",
      });
    }
    model.Search(query, mode, base::DoNothing());
    FireDebounce();
    EXPECT_EQ(0, state.factory_count) << query;
    EXPECT_EQ(0, state.start_count) << query;
  }
};

TEST_F(MahoCommandModelTask5Test, LazyEligibleStartOccursOnlyAfterDebounce) {
  Task5RemoteSourceState state;
  MahoCommandModel model(browser());
  model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));

  model.Search("  weather seoul  ", CommandOverlayMode::kSearch,
               base::DoNothing());
  task_environment()->FastForwardBy(kModelDebounceDelay -
                                    base::Milliseconds(1));
  EXPECT_EQ(0, state.factory_count);
  EXPECT_EQ(0, state.start_count);

  task_environment()->FastForwardBy(base::Milliseconds(1));
  ASSERT_EQ(1, state.factory_count);
  ASSERT_EQ(1, state.start_count);
  EXPECT_EQ(profile(), state.factory_profile);
  ASSERT_EQ(1u, state.queries.size());
  EXPECT_EQ("weather seoul", state.queries[0]);
}

TEST_F(MahoCommandModelTask5Test,
       IneligibleInputsAndPrivateContextsNeverConstructSource) {
  profile()->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, false);
  ExpectNoRemoteStart(browser(), "weather", CommandOverlayMode::kSearch);
  profile()->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, true);

  for (const auto& [query, mode] :
       std::vector<std::pair<std::string, CommandOverlayMode>>{
           {"", CommandOverlayMode::kSearch},
           {"   \t", CommandOverlayMode::kSearch},
           {"https://maho.app", CommandOverlayMode::kSearch},
           {"maho.app/path", CommandOverlayMode::kSearch},
           {"localhost:3000", CommandOverlayMode::kSearch},
           {"localhost/path", CommandOverlayMode::kSearch},
           {"123:3000", CommandOverlayMode::kSearch},
           {"devbox:3000", CommandOverlayMode::kSearch},
           {"build-server:8080", CommandOverlayMode::kSearch},
           {"weather", CommandOverlayMode::kCommandsOnly},
           {">", CommandOverlayMode::kSearch},
           {"  > weather", CommandOverlayMode::kSearch},
           {"weather", CommandOverlayMode::kSiteSearch},
       }) {
    ExpectNoRemoteStart(browser(), query, mode);
  }
  ExpectNoRemoteStart(browser(), "@gh chromium", CommandOverlayMode::kSearch,
                      /*add_site_keyword=*/true);

  Profile* primary_otr = profile()->GetPrimaryOTRProfile(true);
  auto primary_otr_browser =
      CreateBrowser(primary_otr, Browser::TYPE_NORMAL, false);
  ExpectNoRemoteStart(primary_otr_browser.get(), "weather",
                      CommandOverlayMode::kSearch);

  const Profile::OTRProfileID other_otr_id =
      Profile::OTRProfileID::CreateUniqueForTesting();
  Profile* other_otr =
      profile()->GetOffTheRecordProfile(other_otr_id, true);
  auto other_otr_browser =
      CreateBrowser(other_otr, Browser::TYPE_NORMAL, false);
  ExpectNoRemoteStart(other_otr_browser.get(), "weather",
                      CommandOverlayMode::kSearch);

  TestingProfile* guest_profile = profile_manager()->CreateGuestProfile();
  auto guest_browser =
      CreateBrowser(guest_profile, Browser::TYPE_APP, false);
  ExpectNoRemoteStart(guest_browser.get(), "weather",
                      CommandOverlayMode::kSearch);

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
  TestingProfile* system_profile = profile_manager()->CreateSystemProfile();
  EXPECT_FALSE(MahoCommandModel::IsRemoteSearchEligible(
      system_profile, MahoClassifyProfile(system_profile),
      CommandOverlayMode::kSearch, "weather",
      /*matches_site_search=*/false));
#endif

  ExpectNoRemoteStart(/*test_browser=*/nullptr, "weather",
                      CommandOverlayMode::kSearch);
}

TEST_F(MahoCommandModelTask5Test, PreferenceIsReadAtDebouncedFireTime) {
  Task5RemoteSourceState state;
  MahoCommandModel model(browser());
  model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));

  model.Search("weather", CommandOverlayMode::kSearch, base::DoNothing());
  profile()->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, false);
  FireDebounce();
  EXPECT_EQ(0, state.factory_count);
  EXPECT_EQ(0, state.start_count);

  profile()->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, true);
  model.Search("news", CommandOverlayMode::kSearch, base::DoNothing());
  FireDebounce();
  EXPECT_EQ(1, state.factory_count);
  EXPECT_EQ(1, state.start_count);
  ASSERT_EQ(1u, state.queries.size());
  EXPECT_EQ("news", state.queries[0]);
}

TEST_F(MahoCommandModelTask5Test,
       SynchronousStartSnapshotPublishesForInitializedGeneration) {
  Task5RemoteSourceState state;
  state.synchronous_snapshot = {
      MakeTask4Remote("weather tomorrow",
                      "https://search.example/?q=weather+tomorrow")};
  MahoCommandModel model(browser());
  model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
  std::vector<std::vector<std::string>> publications;

  model.Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](std::vector<std::vector<std::string>>* publications,
             std::vector<CommandSuggestion> results) {
            publications->push_back(Task5Keys(results));
          },
          &publications));
  FireDebounce();

  ASSERT_EQ(1u, publications.size());
  EXPECT_EQ((std::vector<std::string>{"remote-search:weather tomorrow"}),
            publications[0]);
  EXPECT_EQ(model.search_generation_for_testing(),
            model.search_generation_for_testing());
}

TEST_F(MahoCommandModelTask5Test,
       RemoteBeforeLocalLocalBeforeRemoteAndChangedStreamPublishExactKeys) {
  {
    Task5RemoteSourceState state;
    MahoCommandModel model(browser());
    model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
    std::vector<std::vector<std::string>> publications;
    model.Search(
        "weather", CommandOverlayMode::kSearch,
        base::BindRepeating(
            [](std::vector<std::vector<std::string>>* publications,
               std::vector<CommandSuggestion> results) {
              publications->push_back(Task5Keys(results));
            },
            &publications));
    FireDebounce();
    ASSERT_EQ(1u, state.callbacks.size());
    state.callbacks[0].Run({
        MakeTask4Remote("weather tomorrow", "https://search.example/?q=one"),
        MakeTask4Remote("weather hourly", "https://search.example/?q=two"),
    });
    model.AcceptLocalResultsForTesting(model.search_generation_for_testing(),
                                       MakeTask4Locals(5));
    const std::vector<std::string> remote_only = {
        "remote-search:weather tomorrow", "remote-search:weather hourly"};
    const std::vector<std::string> merged = {
        "local:0", "local:1", "local:2",
        "remote-search:weather tomorrow", "remote-search:weather hourly"};
    EXPECT_NE(publications.end(),
              std::ranges::find(publications, remote_only));
    EXPECT_NE(publications.end(), std::ranges::find(publications, merged));
  }

  {
    Task5RemoteSourceState state;
    MahoCommandModel model(browser());
    model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
    std::vector<std::vector<std::string>> publications;
    model.Search(
        "news", CommandOverlayMode::kSearch,
        base::BindRepeating(
            [](std::vector<std::vector<std::string>>* publications,
               std::vector<CommandSuggestion> results) {
              publications->push_back(Task5Keys(results));
            },
            &publications));
    FireDebounce();
    model.AcceptLocalResultsForTesting(model.search_generation_for_testing(),
                                       MakeTask4Locals(5));
    ASSERT_EQ(1u, state.callbacks.size());
    state.callbacks[0].Run({
        MakeTask4Remote("news today", "https://search.example/?q=today"),
        MakeTask4Remote("news live", "https://search.example/?q=live"),
    });
    state.callbacks[0].Run({
        MakeTask4Remote("news latest", "https://search.example/?q=latest"),
        MakeTask4Remote("news live", "https://search.example/?q=live"),
    });
    for (const std::vector<std::string>& expected : {
             std::vector<std::string>{"local:0", "local:1", "local:2",
                                      "local:3", "local:4"},
             std::vector<std::string>{"local:0", "local:1", "local:2",
                                      "remote-search:news today",
                                      "remote-search:news live"},
             std::vector<std::string>{"local:0", "local:1", "local:2",
                                      "remote-search:news latest",
                                      "remote-search:news live"}}) {
      EXPECT_NE(publications.end(), std::ranges::find(publications, expected));
    }
  }
}

TEST_F(MahoCommandModelTask5Test,
       IdenticalHiddenAndDuplicateChurnDoNotPublishOrRestartFavicons) {
  Task5RemoteSourceState state;
  MahoCommandModel model(browser());
  model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
  int callback_count = 0;
  model.Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](int* callback_count, std::vector<CommandSuggestion>) {
            ++*callback_count;
          },
          &callback_count));
  FireDebounce();
  model.AcceptLocalResultsForTesting(model.search_generation_for_testing(),
                                     MakeTask4Locals(5));
  ASSERT_EQ(1u, state.callbacks.size());
  const std::vector<RemoteSearchSuggestion> visible = {
      MakeTask4Remote("weather one", "https://search.example/?q=one"),
      MakeTask4Remote("weather two", "https://search.example/?q=two"),
      MakeTask4Remote("weather hidden", "https://search.example/?q=hidden"),
  };
  state.callbacks[0].Run(visible);
  const int callback_count_after_visible = callback_count;
  const int favicon_passes =
      model.favicon_request_pass_count_for_testing();

  state.callbacks[0].Run(visible);
  state.callbacks[0].Run({
      visible[0], visible[1],
      MakeTask4Remote("weather hidden changed",
                      "https://search.example/?q=hidden-changed"),
  });
  state.callbacks[0].Run({visible[0], visible[1], visible[0]});

  EXPECT_EQ(callback_count_after_visible, callback_count);
  EXPECT_EQ(favicon_passes,
            model.favicon_request_pass_count_for_testing());
}

TEST_F(MahoCommandModelTask5Test,
       QueryBCancelsAndRestartsSourceWhileLateQueryAIsSuppressed) {
  Task5RemoteSourceState state;
  MahoCommandModel model(browser());
  model.SetRemoteSearchSourceFactoryForTesting(MakeTask5Factory(&state));
  std::vector<std::vector<std::string>> publications;
  auto callback = base::BindRepeating(
      [](std::vector<std::vector<std::string>>* publications,
         std::vector<CommandSuggestion> results) {
        publications->push_back(Task5Keys(results));
      },
      &publications);

  model.Search("query a", CommandOverlayMode::kSearch, callback);
  FireDebounce();
  ASSERT_EQ(1, state.factory_count);
  ASSERT_EQ(1, state.start_count);
  ASSERT_EQ(1u, state.callbacks.size());

  model.Search("query b", CommandOverlayMode::kSearch, callback);
  EXPECT_EQ(1, state.cancel_count);
  FireDebounce();
  ASSERT_EQ(1, state.factory_count);
  ASSERT_EQ(2, state.start_count);
  ASSERT_EQ(2u, state.callbacks.size());

  state.callbacks[1].Run(
      {MakeTask4Remote("query b result", "https://search.example/?q=b")});
  ASSERT_FALSE(publications.empty());
  const size_t publications_after_query_b = publications.size();
  EXPECT_NE(std::ranges::find(publications.back(),
                              "remote-search:query b result"),
            publications.back().end());

  state.callbacks[0].Run(
      {MakeTask4Remote("query a result", "https://search.example/?q=a")});

  EXPECT_EQ(publications_after_query_b, publications.size());
  for (const auto& publication : publications) {
    EXPECT_EQ(publication.end(),
              std::ranges::find(publication,
                                "remote-search:query a result"));
  }
  EXPECT_EQ((std::vector<std::string>{"query a", "query b"}), state.queries);
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       QueryBInvalidatesQueryABeforeDebounce) {
  MahoCommandModel model(nullptr);
  int callback_count = 0;
  auto callback = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++*count; },
      &callback_count);

  model.Search("weather", CommandOverlayMode::kSearch, callback);
  const uint32_t weather_generation = model.search_generation_for_testing();
  model.Search("news", CommandOverlayMode::kSearch, callback);

  EXPECT_NE(weather_generation, model.search_generation_for_testing());
  model.AcceptLocalResultsForTesting(
      weather_generation,
      {MakeSuggestion(CommandSuggestionType::kHistory, "weather")});
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(model.results().empty());
  model.Cancel();
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       SynchronousRemoteSnapshotUsesInitializedGeneration) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  source->set_synchronous_snapshot({MakeRemoteSuggestion("weather today")});
  model.SetRemoteSearchSourceForTesting(std::move(source));

  model.Search("weather", CommandOverlayMode::kSearch, base::DoNothing());
  const uint32_t generation = model.search_generation_for_testing();
  model.StartRemoteSearchForTesting(generation, "weather");

  ASSERT_EQ(1u, model.remote_snapshot_for_testing().size());
  EXPECT_EQ("weather today",
            model.remote_snapshot_for_testing()[0].normalized_search_terms);
  model.Cancel();
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       LateLocalRemoteAndFaviconCallbacksAreRejected) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  LifecycleFakeRemoteSource* source_driver = source.get();
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  auto callback = base::BindRepeating(
      [](int* count, std::vector<CommandSuggestion>) { ++*count; },
      &callback_count);

  model.Search("weather", CommandOverlayMode::kSearch, callback);
  const uint32_t weather_generation = model.search_generation_for_testing();
  model.StartRemoteSearchForTesting(weather_generation, "weather");
  model.Search("news", CommandOverlayMode::kSearch, callback);
  const uint32_t news_generation = model.search_generation_for_testing();
  model.AcceptLocalResultsForTesting(
      news_generation,
      {MakeSuggestion(CommandSuggestionType::kHistory, "news")});
  ASSERT_EQ(1, callback_count);
  ASSERT_EQ(1u, model.results().size());

  model.AcceptLocalResultsForTesting(
      weather_generation,
      {MakeSuggestion(CommandSuggestionType::kHistory, "weather")});
  source_driver->Emit({MakeRemoteSuggestion("weather tomorrow")});
  model.ApplyFaviconForTesting(weather_generation, "news");

  EXPECT_EQ(1, callback_count);
  ASSERT_EQ(1u, model.results().size());
  EXPECT_EQ("news", model.results()[0].key);
  EXPECT_FALSE(model.results()[0].icon.has_value());
  EXPECT_TRUE(model.remote_snapshot_for_testing().empty());
  model.Cancel();
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       ReentrantRemoteCancelCannotPublish) {
  MahoCommandModel model(nullptr);
  FakeRemoteSourceState state;
  auto source = std::make_unique<LifecycleFakeRemoteSource>(&state);
  source->set_cancel_snapshot({MakeRemoteSuggestion("late cancel result")});
  model.SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;

  model.Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](int* count, std::vector<CommandSuggestion>) { ++*count; },
          &callback_count));
  const uint32_t generation = model.search_generation_for_testing();
  model.StartRemoteSearchForTesting(generation, "weather");
  model.Cancel();

  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(model.results().empty());
  EXPECT_TRUE(model.remote_snapshot_for_testing().empty());
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       SynchronousStartNotificationCanDestroyModelSafely) {
  ReentrantControllerState state;
  auto controller = std::make_unique<ReentrantNotificationController>(&state);
  auto source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));
  auto model = std::make_unique<MahoCommandModel>(nullptr);
  model->SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  model->Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](std::unique_ptr<MahoCommandModel>* model, int* callback_count,
             std::vector<CommandSuggestion>) {
            ++*callback_count;
            model->reset();
          },
          base::Unretained(&model), base::Unretained(&callback_count)));
  const uint32_t generation = model->search_generation_for_testing();

  model->StartRemoteSearchForTesting(generation, "weather");
  EXPECT_TRUE(model);
  EXPECT_EQ(1, state.start_return_count);
  EXPECT_EQ(0, callback_count);

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(model);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(1, state.destroy_count);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       StreamedNotificationCanDestroyModelSafely) {
  ReentrantControllerState state;
  auto controller = std::make_unique<ReentrantNotificationController>(&state);
  ReentrantNotificationController* controller_driver = controller.get();
  auto source =
      MahoRemoteSearchSuggestions::CreateForTesting(std::move(controller));
  auto model = std::make_unique<MahoCommandModel>(nullptr);
  model->SetRemoteSearchSourceForTesting(std::move(source));
  int callback_count = 0;
  model->Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](std::unique_ptr<MahoCommandModel>* model, int* callback_count,
             std::vector<CommandSuggestion>) {
            ++*callback_count;
            if (*callback_count == 2) {
              model->reset();
            }
          },
          base::Unretained(&model), base::Unretained(&callback_count)));
  model->StartRemoteSearchForTesting(model->search_generation_for_testing(),
                                     "weather");
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(model);
  ASSERT_EQ(1, callback_count);

  controller_driver->EmitStreamedSnapshot("weather tomorrow");
  EXPECT_TRUE(model);
  EXPECT_EQ(1, state.stream_return_count);
  EXPECT_EQ(1, callback_count);

  task_environment_.RunUntilIdle();
  EXPECT_FALSE(model);
  EXPECT_EQ(2, callback_count);
  EXPECT_EQ(1, state.destroy_count);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(2, callback_count);
}

TEST_F(MahoCommandModelLifecycleTask3Test,
       ModelDestructionFromOwnResultsCallbackIsSafe) {
  auto model = std::make_unique<MahoCommandModel>(nullptr);
  int callback_count = 0;
  model->Search(
      "weather", CommandOverlayMode::kSearch,
      base::BindRepeating(
          [](std::unique_ptr<MahoCommandModel>* model, int* callback_count,
             std::vector<CommandSuggestion>) {
            ++*callback_count;
            model->reset();
          },
          base::Unretained(&model), base::Unretained(&callback_count)));
  const uint32_t generation = model->search_generation_for_testing();

  model->AcceptLocalResultsForTesting(
      generation,
      {MakeSuggestion(CommandSuggestionType::kHistory, "local:weather")});

  EXPECT_FALSE(model);
  EXPECT_EQ(1, callback_count);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
}

TEST_F(MahoCommandModelLifecycleTask3Test, RefetchAllocatesNewGeneration) {
  MahoCommandModel model(nullptr);
  model.Search("weather", CommandOverlayMode::kSearch, base::DoNothing());
  const uint32_t first_generation = model.search_generation_for_testing();

  model.RefetchCurrentQuery();

  EXPECT_NE(first_generation, model.search_generation_for_testing());
  model.Cancel();
}

TEST_F(MahoCommandModelLifecycleTask3Test, DestructionCancelsRemoteSource) {
  FakeRemoteSourceState state;
  {
    MahoCommandModel model(nullptr);
    model.SetRemoteSearchSourceForTesting(
        std::make_unique<LifecycleFakeRemoteSource>(&state));
    model.Search("weather", CommandOverlayMode::kSearch, base::DoNothing());
  }

  EXPECT_GE(state.cancel_count, 1);
  EXPECT_EQ(1, state.destroy_count);
}

TEST_F(MahoCommandModelTest, PrivateSourcesAreNeverRead) {
  EXPECT_TRUE(MahoCommandModel::ShouldReadSavedSourcesForContext(
      MahoPrivateContextClass::kRegular));

  for (MahoPrivateContextClass klass :
       {MahoPrivateContextClass::kPrimaryIncognito,
        MahoPrivateContextClass::kOtherOtr, MahoPrivateContextClass::kGuest,
        MahoPrivateContextClass::kSystem,
        MahoPrivateContextClass::kDevToolsOtr,
        MahoPrivateContextClass::kNull}) {
    EXPECT_FALSE(MahoCommandModel::ShouldReadSavedSourcesForContext(klass))
        << "saved sources must never be read outside a regular profile";
  }
}

TEST_F(MahoCommandModelTest, PrivateActionRowsMatchRustContract) {
  const std::vector<std::string> expected = {
      "close_tab",   "reload_tab",     "hard_reload",     "copy_url",
      "toggle_sidebar", "toggle_split_view", "zoom_in",   "zoom_out",
      "reset_zoom",  "find_in_page",   "view_source",     "toggle_dev_tools",
      "print_page"};
  EXPECT_EQ(expected, MahoCommandModel::PrivateActionAllowlist())
      << "C++ private action allowlist must match the frozen Rust "
         "command_bar.rs search_incognito ephemeral_actions order/ids";
}

TEST_F(MahoCommandModelTest, PrivateSearchUsesTemplateUrlServiceReadOnly) {
  EXPECT_TRUE(MahoCommandModel::PrivateSearchUsesDefaultProviderOnly(
      MahoPrivateContextClass::kPrimaryIncognito));
  EXPECT_FALSE(MahoCommandModel::PrivateSearchUsesDefaultProviderOnly(
      MahoPrivateContextClass::kRegular));

  CommandSuggestion suggestion =
      MahoCommandModel::BuildDefaultProviderSearchSuggestion(
          "cats", "https://example.test/search?q=cats", "ExampleSearch");
  EXPECT_EQ(CommandSuggestionType::kSearch, suggestion.type);
  EXPECT_EQ("search:cats", suggestion.key);
  EXPECT_EQ("https://example.test/search?q=cats",
            suggestion.execution_payload);
  EXPECT_NE(std::string::npos, suggestion.title.find("ExampleSearch"));
}

TEST_F(MahoCommandModelTest, ClassifyTypedNavigationMatchesOmniboxRules) {
  auto classify = [](const std::string& text) {
    return MahoCommandModel::ClassifyTypedNavigation(text);
  };
  std::optional<MahoCommandModel::TypedNavigation> nav;

  nav = classify("example.com");
  ASSERT_TRUE(nav.has_value());
  EXPECT_EQ("http://example.com/", nav->url.spec());
  EXPECT_FALSE(nav->typed_http_scheme);

  nav = classify("http://example.com");
  ASSERT_TRUE(nav.has_value());
  EXPECT_TRUE(nav->typed_http_scheme);

  nav = classify("https://example.com/a");
  ASSERT_TRUE(nav.has_value());
  EXPECT_EQ("https://example.com/a", nav->url.spec());
  EXPECT_FALSE(nav->typed_http_scheme);

  nav = classify("localhost:3000");
  ASSERT_TRUE(nav.has_value());
  EXPECT_EQ("http://localhost:3000/", nav->url.spec());

  nav = classify("192.168.0.1");
  ASSERT_TRUE(nav.has_value());
  EXPECT_EQ("http", nav->url.scheme());

  nav = classify("chrome://settings");
  ASSERT_TRUE(nav.has_value());
  EXPECT_EQ("chrome://settings/", nav->url.spec());

  EXPECT_TRUE(classify("file:///tmp/x").has_value());

  // Plain words and multi-word queries are searches.
  EXPECT_FALSE(classify("naver").has_value());
  EXPECT_FALSE(classify("how to cook rice").has_value());
  EXPECT_FALSE(classify("").has_value());
  // Script schemes are never navigated from the palette.
  EXPECT_FALSE(classify("javascript:alert(1)").has_value());
}

TEST_F(MahoCommandModelTest, ExpandSiteSearchTemplateEscapesQuery) {
  // Spaces become '+', matching Chromium's {searchTerms} substitution.
  EXPECT_EQ("https://www.youtube.com/results?search_query=cat+videos",
            MahoCommandModel::ExpandSiteSearchTemplate(
                "https://www.youtube.com/results?search_query={query}",
                "cat videos")
                .spec());
  EXPECT_EQ("https://example.test/?q=a%26b",
            MahoCommandModel::ExpandSiteSearchTemplate(
                "https://example.test/?q={searchTerms}", "a&b")
                .spec());
  EXPECT_EQ("https://example.test/s?k=x",
            MahoCommandModel::ExpandSiteSearchTemplate(
                "https://example.test/s?k=%s", "x")
                .spec());
  // No placeholder or a non-web scheme yields an invalid URL.
  EXPECT_FALSE(MahoCommandModel::ExpandSiteSearchTemplate(
                   "https://example.test/", "x")
                   .is_valid());
  EXPECT_FALSE(MahoCommandModel::ExpandSiteSearchTemplate(
                   "javascript:{query}", "x")
                   .is_valid());
}

TEST_F(MahoCommandModelTest, BuildsNavigationSuggestionForWebAndMahoUrls) {
  std::optional<CommandSuggestion> web =
      MahoCommandModel::BuildNavigationSuggestionForQuery("example.com/path");
  ASSERT_TRUE(web.has_value());
  EXPECT_EQ(CommandSuggestionType::kNavigation, web->type);
  EXPECT_EQ("http://example.com/path", web->execution_payload);

  std::optional<CommandSuggestion> maho =
      MahoCommandModel::BuildNavigationSuggestionForQuery("maho://settings/");
  ASSERT_TRUE(maho.has_value());
  EXPECT_EQ(CommandSuggestionType::kNavigation, maho->type);
  EXPECT_EQ("maho://settings/", maho->execution_payload);
}

TEST_F(MahoCommandModelTest, RejectsUnlistedMahoNavigationSuggestion) {
  EXPECT_FALSE(MahoCommandModel::BuildNavigationSuggestionForQuery(
                   "maho://not-listed/")
                   .has_value());
}

TEST_F(MahoCommandModelTest, MatchExactSiteSearchKeyword) {
  MahoCommandModel model(nullptr);

  MahoCommandModel::SiteSearchEngine engine1;
  engine1.prefix = "gh";
  engine1.name = "GitHub";
  engine1.url_template = "https://github.com/search?q={searchTerms}";
  model.AddSiteSearchEngineForTest(engine1);

  MahoCommandModel::SiteSearchEngine engine2;
  engine2.prefix = "jira";
  engine2.name = "Jira";
  engine2.url_template = "https://jira.com/search?q={searchTerms}";
  model.AddSiteSearchEngineForTest(engine2);

  // Exact matches
  EXPECT_EQ(&model.site_search_engines()[0], model.MatchExactSiteSearchKeyword("gh"));
  EXPECT_EQ(&model.site_search_engines()[0], model.MatchExactSiteSearchKeyword("@gh"));
  EXPECT_EQ(&model.site_search_engines()[1], model.MatchExactSiteSearchKeyword("jira"));
  EXPECT_EQ(&model.site_search_engines()[1], model.MatchExactSiteSearchKeyword("@jira"));

  // Non-matches
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword("gh chromium"));
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword("jira MAHO-123"));
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword("jira issue"));
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword("ghh"));
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword("@"));
  EXPECT_EQ(nullptr, model.MatchExactSiteSearchKeyword(""));
}

TEST_F(MahoCommandModelTest, ParseSearchResultsJsonFiltersAiAnswer) {
  std::string json_str = R"([
    {
      "kind": "history",
      "title": "Google",
      "key": "hist:google"
    },
    {
      "kind": "aiAnswer",
      "title": "Ask AI",
      "key": "ai:ask"
    },
    {
      "kind": "bookmark",
      "title": "Maho Browser",
      "key": "bm:maho"
    }
  ])";

  std::vector<CommandSuggestion> results =
      MahoCommandModel::ParseSearchResultsJson(json_str);

  ASSERT_EQ(2u, results.size())
      << "The parsed results must skip the 'aiAnswer' item, leaving exactly 2 items.";
  EXPECT_EQ("hist:google", results[0].key);
  EXPECT_EQ("bm:maho", results[1].key);
}

}  // namespace

class MahoCommandModelTabObserverTest : public BrowserWithTestWindowTest {
 protected:
  void BuildTabCache(MahoCommandModel& model) { model.BuildTabsJson(); }
  bool IsTabCacheDirty(const MahoCommandModel& model) {
    return model.tabs_cache_dirty_;
  }
};

TEST_F(MahoCommandModelTabObserverTest, TabChangesInvalidateCachedTabs) {
  AddTab(browser(), GURL("https://example.test/"));
  MahoCommandModel model(browser());
  TabStripModel* strip = browser()->GetTabStripModel();
  auto* tab = strip->GetActiveTab();
  ASSERT_NE(nullptr, tab);

  for (TabChangeType change_type :
       {TabChangeType::kAll, TabChangeType::kLoadingOnly,
        TabChangeType::kAttentionOnly, TabChangeType::kBlockedOnly,
        TabChangeType::kResourceUsageOnly}) {
    BuildTabCache(model);
    ASSERT_FALSE(IsTabCacheDirty(model));
    // Dispatch through the actual observed model, using its real tab. The
    // observer notification is synchronous, so no event-loop wait is needed.
    strip->NotifyTabChanged(tab, change_type);
    EXPECT_TRUE(IsTabCacheDirty(model));
  }
}

}  // namespace maho
