// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
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

int ActiveTabSessionId(Browser* browser) {
  content::WebContents* wc = browser->tab_strip_model()->GetActiveWebContents();
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
  const int regular_id = ActiveTabSessionId(browser());
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
  Browser* incognito = CreateIncognitoBrowser();
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
  Browser* incognito = CreateIncognitoBrowser();
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
  Browser* incognito = CreateIncognitoBrowser();
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
  const int regular_id = ActiveTabSessionId(browser());
  ASSERT_NE(regular_id, 0);

  auto session = MakeInitializedSession();
  base::Value before =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", regular_id), 2);
  ASSERT_TRUE(before.GetDict().FindDict("result"));

  // Switch the active/frontmost window to an OTR window.
  Browser* incognito = CreateIncognitoBrowser();
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
  const int tab_id = ActiveTabSessionId(browser());
  ASSERT_NE(tab_id, 0);
  const int closed_index = browser()->tab_strip_model()->active_index();

  auto session = MakeInitializedSession();
  base::Value before =
      CallTool(session.get(), "browser_tab_get",
               base::StringPrintf(R"({"tab_id":%d})", tab_id), 2);
  ASSERT_TRUE(before.GetDict().FindDict("result"));

  browser()->tab_strip_model()->CloseWebContentsAt(
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
