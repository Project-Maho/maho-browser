// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <unistd.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

// R-8 privacy-isolation coverage for the MCP surface.
//
// These tests drive the production-registered MahoMcpBrowserDelegate through a
// real MahoMcpSession against real regular and Incognito profiles. That is a
// same-UID equivalent of the ${profile}/maho.sock transport: the socket server
// simply feeds ProcessData on the UI sequence with the same delegate, so
// exercising the session + delegate directly proves the same non-revealing
// resolution contract without duplicating the socket plumbing.

namespace maho {
namespace {

std::unique_ptr<MahoMcpSession> MakeInitializedSession() {
  auto session = std::make_unique<MahoMcpSession>(getuid());
  session->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"itest","version":"0.1.0"}}})"
      "\n");
  return session;
}

std::unique_ptr<MahoMcpSession> MakeInitializedTrustedCliSession() {
#if BUILDFLAG(IS_APPLE)
  constexpr char kMahoCliPath[] =
      "/Applications/Maho.app/Contents/Helpers/maho";
#else
  constexpr char kMahoCliPath[] = "/usr/bin/maho";
#endif
  auto session = std::make_unique<MahoMcpSession>(
      getuid(), nullptr, kMahoCliPath, true);
  auto responses = session->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"itest","version":"0.1.0"}}})"
      "\n");
  CHECK_EQ(responses.size(), 1u);
  return session;
}

base::Value CallTool(MahoMcpSession* session,
                     const std::string& name,
                     const std::string& args,
                     int id) {
  auto responses = session->ProcessData(base::StringPrintf(
      R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
      R"("params":{"name":"%s","arguments":%s}})"
      "\n",
      id, name.c_str(), args.c_str()));
  if (responses.empty()) {
    return base::Value();
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses.back(), base::JSON_PARSE_RFC);
  return parsed.has_value() ? std::move(*parsed) : base::Value();
}

std::optional<base::Value> DeferredOutput(const base::Value& response) {
  const base::DictValue* result = response.GetDict().FindDict("result");
  if (!result) {
    return std::nullopt;
  }
  const std::string* output_json = result->FindString("outputJson");
  if (!output_json) {
    return std::nullopt;
  }
  return base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
}

std::optional<int> FindRefForElementId(const base::Value& node,
                                       const std::string& element_id) {
  const base::DictValue* dict = node.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }
  const std::string* snapshot_element_id = dict->FindString("elementId");
  if (snapshot_element_id && *snapshot_element_id == element_id) {
    return dict->FindInt("ref");
  }
  const base::ListValue* children = dict->FindList("children");
  if (!children) {
    return std::nullopt;
  }
  for (const base::Value& child : *children) {
    if (std::optional<int> ref = FindRefForElementId(child, element_id)) {
      return ref;
    }
  }
  return std::nullopt;
}

const base::DictValue* FindNodeForElementId(const base::Value& node,
                                            const std::string& element_id) {
  const base::DictValue* dict = node.GetIfDict();
  if (!dict) {
    return nullptr;
  }
  const std::string* snapshot_element_id = dict->FindString("elementId");
  if (snapshot_element_id && *snapshot_element_id == element_id) {
    return dict;
  }
  const base::ListValue* children = dict->FindList("children");
  if (!children) {
    return nullptr;
  }
  for (const base::Value& child : *children) {
    if (const base::DictValue* found =
            FindNodeForElementId(child, element_id)) {
      return found;
    }
  }
  return nullptr;
}

int ActiveTabSessionId(Browser* browser) {
  content::WebContents* wc = browser->GetTabStripModel()->GetActiveWebContents();
  if (!wc) {
    return 0;
  }
  sessions::SessionTabHelper* helper =
      sessions::SessionTabHelper::FromWebContents(wc);
  return helper ? helper->session_id().id() : 0;
}

using MahoMcpIncognitoBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       NormalTransportPositiveControl) {
  const int regular_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_NE(regular_id, 0);

  auto session = MakeInitializedSession();

  base::Value list = CallTool(session.get(), "browser_tab_list", "{}", 2);
  const auto* result = list.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* tabs = result->FindList("tabs");
  ASSERT_TRUE(tabs);
  bool found_regular = false;
  for (const auto& tab : *tabs) {
    if (tab.GetDict().FindInt("id").value_or(-1) == regular_id) {
      found_regular = true;
    }
  }
  EXPECT_TRUE(found_regular);

  base::Value get = CallTool(session.get(), "browser_tab_get",
                             base::StringPrintf(R"({"tab_id":%d})", regular_id),
                             3);
  EXPECT_TRUE(get.GetDict().FindDict("result"));
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest, OtrTargetsAreNonEnumerable) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, GURL("about:blank")));
  const int otr_id = ActiveTabSessionId(incognito);
  ASSERT_NE(otr_id, 0);

  auto session = MakeInitializedSession();
  base::Value list = CallTool(session.get(), "browser_tab_list", "{}", 2);
  const auto* result = list.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* tabs = result->FindList("tabs");
  ASSERT_TRUE(tabs);
  for (const auto& tab : *tabs) {
    EXPECT_NE(tab.GetDict().FindInt("id").value_or(-1), otr_id)
        << "OTR tab must not be enumerable over MCP";
  }
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       DirectOtrIdIsNonRevealingNotFound) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, GURL("about:blank")));
  const int otr_id = ActiveTabSessionId(incognito);
  ASSERT_NE(otr_id, 0);

  auto session = MakeInitializedSession();

  base::Value otr = CallTool(session.get(), "browser_tab_get",
                             base::StringPrintf(R"({"tab_id":%d})", otr_id), 2);
  const auto* otr_error = otr.GetDict().FindDict("error");
  ASSERT_TRUE(otr_error);
  EXPECT_EQ(otr_error->FindInt("code").value(), -32004);
  EXPECT_EQ(*otr_error->FindString("message"), "tab not found");

  base::Value unknown =
      CallTool(session.get(), "browser_tab_get", R"({"tab_id":987654321})", 3);
  const auto* unknown_error = unknown.GetDict().FindDict("error");
  ASSERT_TRUE(unknown_error);
  // An explicit OTR id must be byte-for-byte indistinguishable from an
  // unknown id.
  EXPECT_EQ(unknown_error->FindInt("code").value(),
            otr_error->FindInt("code").value());
  EXPECT_EQ(*unknown_error->FindString("message"),
            *otr_error->FindString("message"));
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       ZeroAndOmittedWithOtrActiveAreDenied) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, GURL("about:blank")));

  auto session = MakeInitializedSession();

  // Omitted tab id while the frontmost/active window is OTR: -32005.
  base::Value omitted = CallTool(session.get(), "browser_page_text", "{}", 2);
  const auto* omitted_error = omitted.GetDict().FindDict("error");
  ASSERT_TRUE(omitted_error);
  EXPECT_EQ(omitted_error->FindInt("code").value(), -32005);
  EXPECT_EQ(*omitted_error->FindString("message"), "no eligible active tab");

  // Explicit zero is treated identically to omitted.
  base::Value zero =
      CallTool(session.get(), "browser_page_text", R"({"tab_id":0})", 3);
  const auto* zero_error = zero.GetDict().FindDict("error");
  ASSERT_TRUE(zero_error);
  EXPECT_EQ(zero_error->FindInt("code").value(), -32005);
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       ActiveSwitchRaceCannotRetarget) {
  const int regular_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_NE(regular_id, 0);

  auto session = MakeInitializedSession();
  base::Value before =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", regular_id), 2);
  ASSERT_TRUE(before.GetDict().FindDict("result"));

  // Switch the active/frontmost window to an OTR window.
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, GURL("about:blank")));
  const int otr_id = ActiveTabSessionId(incognito);
  ASSERT_NE(otr_id, 0);

  // The explicit regular target still resolves to the same tab; it is never
  // silently retargeted to the now-frontmost OTR window.
  base::Value after =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", regular_id), 3);
  const auto* after_result = after.GetDict().FindDict("result");
  ASSERT_TRUE(after_result);
  EXPECT_EQ(after_result->FindInt("id").value(), regular_id);

  // The OTR tab remains non-revealing.
  base::Value otr = CallTool(session.get(), "browser_tab_get",
                             base::StringPrintf(R"({"tab_id":%d})", otr_id), 4);
  const auto* otr_error = otr.GetDict().FindDict("error");
  ASSERT_TRUE(otr_error);
  EXPECT_EQ(otr_error->FindInt("code").value(), -32004);
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       CloseReuseGenerationRaceIsDenied) {
  // Add a second regular tab so closing it does not tear down the browser.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("about:blank"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const int tab_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_NE(tab_id, 0);
  const int closed_index = browser()->GetTabStripModel()->active_index();

  auto session = MakeInitializedSession();
  base::Value before =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", tab_id), 2);
  ASSERT_TRUE(before.GetDict().FindDict("result"));

  browser()->GetTabStripModel()->CloseWebContentsAt(
      closed_index, TabCloseTypes::CLOSE_NONE);

  base::Value after =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", tab_id), 3);
  const auto* after_error = after.GetDict().FindDict("error");
  ASSERT_TRUE(after_error);
  EXPECT_EQ(after_error->FindInt("code").value(), -32004);
  EXPECT_EQ(*after_error->FindString("message"), "tab not found");
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       TrustedCliTypeIsVerifiedAgainstRendererAxState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<input%20id%3D'mcp-target'%20aria-label%3D'MCP%20target'>")));
  const int tab_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_NE(tab_id, 0);

  auto session = MakeInitializedTrustedCliSession();
  base::Value snapshot = CallTool(
      session.get(), "browser_accessibility_snapshot",
      base::StringPrintf(R"({"tab_id":%d})", tab_id), 2);
  const base::DictValue* snapshot_result = snapshot.GetDict().FindDict("result");
  ASSERT_TRUE(snapshot_result);
  std::optional<int> ref =
      FindRefForElementId(base::Value(snapshot_result->Clone()), "mcp-target");
  ASSERT_TRUE(ref);

  std::vector<std::string> deferred_responses;
  base::RunLoop run_loop;
  bool timed_out = false;
  session->SetDeferredResponseSender(base::BindRepeating(
      [](std::vector<std::string>* responses, base::RunLoop* loop,
         std::string response) {
        responses->push_back(std::move(response));
        loop->Quit();
      },
      &deferred_responses, &run_loop));
  base::OneShotTimer timeout;
  timeout.Start(FROM_HERE, base::Seconds(5),
                base::BindOnce(
                    [](bool* out_timed_out, base::RunLoop* loop) {
                      *out_timed_out = true;
                      loop->Quit();
                    },
                    &timed_out, &run_loop));

  auto immediate = session->ProcessData(base::StringPrintf(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_type",)"
      R"("arguments":{"tab_id":%d,"ref":%d,"text":"verified value"}}})"
      "\n",
      tab_id, *ref));
  EXPECT_TRUE(immediate.empty());
  run_loop.Run();
  timeout.Stop();
  ASSERT_FALSE(timed_out);
  ASSERT_EQ(deferred_responses.size(), 1u);

  std::optional<base::Value> typed_response =
      base::JSONReader::Read(deferred_responses.front(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(typed_response);
  std::optional<base::Value> typed_output = DeferredOutput(*typed_response);
  ASSERT_TRUE(typed_output);
  const base::DictValue* typed_output_dict = typed_output->GetIfDict();
  ASSERT_TRUE(typed_output_dict);
  EXPECT_TRUE(typed_output_dict->FindBool("typed").value_or(false));
  EXPECT_TRUE(typed_output_dict->FindBool("verified").value_or(false));

  base::Value after = CallTool(
      session.get(), "browser_accessibility_snapshot",
      base::StringPrintf(R"({"tab_id":%d})", tab_id), 4);
  const base::DictValue* after_result = after.GetDict().FindDict("result");
  ASSERT_TRUE(after_result);
  const base::DictValue* field =
      FindNodeForElementId(base::Value(after_result->Clone()), "mcp-target");
  ASSERT_TRUE(field);
  const std::string* value = field->FindString("value");
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, "verified value");
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       SnapshotRefRejectsCommittedNavigation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html,<input%20id%3D'mcp-target'%20aria-label%3D'MCP%20target'>")));
  const int tab_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_NE(tab_id, 0);

  auto session = MakeInitializedTrustedCliSession();
  base::Value snapshot = CallTool(
      session.get(), "browser_accessibility_snapshot",
      base::StringPrintf(R"({"tab_id":%d})", tab_id), 2);
  const base::DictValue* snapshot_result = snapshot.GetDict().FindDict("result");
  ASSERT_TRUE(snapshot_result);
  std::optional<int> ref =
      FindRefForElementId(base::Value(snapshot_result->Clone()), "mcp-target");
  ASSERT_TRUE(ref);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<input%20id%3D'new-target'>")));

  base::Value response = CallTool(
      session.get(), "browser_type",
      base::StringPrintf(
          R"({"tab_id":%d,"ref":%d,"text":"must not dispatch"})",
          tab_id, *ref),
      3);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorStaleReference);
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       TypeInIframeSucceedsWithVerification) {
  // Test iframe typing and AX fallback verification for nested frames.
  const std::string html =
      "data:text/html,"
      "<html><body>"
      "<iframe id='test-frame' srcdoc=\""
      "<html><body><input id='iframe-input' placeholder='Name'></body></html>"
      "\"></iframe>"
      "</body></html>";
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(html)));

  const int tab_id = ActiveTabSessionId(static_cast<Browser*>(browser()));
  ASSERT_GT(tab_id, 0);

  auto session = MakeInitializedTrustedCliSession();
  base::Value snapshot = CallTool(
      session.get(), "browser_accessibility_snapshot",
      base::StringPrintf(R"({"tab_id":%d})", tab_id), 2);
  const base::DictValue* snapshot_result = snapshot.GetDict().FindDict("result");
  ASSERT_TRUE(snapshot_result);
  std::optional<int> ref =
      FindRefForElementId(base::Value(snapshot_result->Clone()), "iframe-input");
  ASSERT_TRUE(ref);

  base::Value response = CallTool(
      session.get(), "browser_type",
      base::StringPrintf(
          R"({"tab_id":%d,"ref":%d,"text":"윤인도"})",
          tab_id, *ref),
      3);
  const base::DictValue* type_result = response.GetDict().FindDict("result");
  ASSERT_TRUE(type_result);
  EXPECT_TRUE(type_result->FindBool("typed").value_or(false));
  EXPECT_TRUE(type_result->FindBool("verified").value_or(false));

  // Also verify numbers
  base::Value response_num = CallTool(
      session.get(), "browser_type",
      base::StringPrintf(
          R"({"tab_id":%d,"ref":%d,"text":"01024674727"})",
          tab_id, *ref),
      4);
  const base::DictValue* num_result = response_num.GetDict().FindDict("result");
  ASSERT_TRUE(num_result);
  EXPECT_TRUE(num_result->FindBool("typed").value_or(false));
  EXPECT_TRUE(num_result->FindBool("verified").value_or(false));
}

IN_PROC_BROWSER_TEST_F(MahoMcpIncognitoBrowserTest,
                       ProfileToolsRequireRegularActiveBrowser) {
  // Positive control: with an eligible regular browser present, a profile/
  // new-tab tool resolves and succeeds. A zero-regular-browser state cannot be
  // constructed inside an in-process browser test, so the -32006 negative is
  // covered by the session unit tests (NoCachedOrZeroActiveFallback).
  auto session = MakeInitializedSession();
  base::Value resp = CallTool(session.get(), "browser_tab_new", "{}", 2);
  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindDict("tab"));
}

}  // namespace
}  // namespace maho
