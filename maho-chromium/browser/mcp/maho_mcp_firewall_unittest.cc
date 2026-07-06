// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_firewall.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoMcpFirewallTest, V1_PasswordFieldRedacted) {
  const std::string html =
      R"(<input type="password" value="s3cret" name="pw">)";
  const std::string out = MahoMcpFirewall::RedactPasswordFields(html);
  EXPECT_NE(out.find(MahoMcpFirewall::kRedacted), std::string::npos);
  EXPECT_EQ(out.find("s3cret"), std::string::npos);
}

TEST(MahoMcpFirewallTest, V1_MultiplePasswordFieldsAllRedacted) {
  const std::string html =
      R"(<input type='password' value='one'/>)"
      R"(<input type="text" value="visible"/>)"
      R"(<input type="password" value="two"/>)";
  const std::string out = MahoMcpFirewall::RedactPasswordFields(html);
  EXPECT_EQ(out.find("one"), std::string::npos);
  EXPECT_EQ(out.find("two"), std::string::npos);
  EXPECT_NE(out.find("visible"), std::string::npos);
}

TEST(MahoMcpFirewallTest, V1_NoPasswordFieldsUnchanged) {
  const std::string html = R"(<input type="text" value="foo"/>)";
  EXPECT_EQ(MahoMcpFirewall::RedactPasswordFields(html), html);
}

TEST(MahoMcpFirewallTest, V2_HeadersHardScrubList) {
  base::DictValue headers;
  headers.Set("Cookie", "session=abc123");
  headers.Set("Authorization", "Bearer xyz");
  headers.Set("Content-Type", "application/json");
  MahoMcpFirewall::RedactHeaders(headers);
  EXPECT_EQ(*headers.FindString("Cookie"), MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("Authorization"), MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("Content-Type"), "application/json");
}

TEST(MahoMcpFirewallTest, V2_HeadersRegexPattern) {
  base::DictValue headers;
  headers.Set("X-Custom-Auth-Header", "creds");
  headers.Set("X-Some-Token-Foo", "tok");
  headers.Set("X-Session-Bar", "sess");
  headers.Set("X-Safe-Header", "ok");
  MahoMcpFirewall::RedactHeaders(headers);
  EXPECT_EQ(*headers.FindString("X-Custom-Auth-Header"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("X-Some-Token-Foo"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("X-Session-Bar"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("X-Safe-Header"), "ok");
}

TEST(MahoMcpFirewallTest, V3_AxTreeAutofillRedacted) {
  base::DictValue node;
  node.Set("role", "textbox");
  node.Set("value", "user@example.com");
  node.Set("AXAutocompleteValue", "cached-suggestion");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*node.FindString("AXAutocompleteValue"),
            MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, V3_AxTreeRecursesIntoChildren) {
  base::DictValue root;
  root.Set("role", "form");
  base::ListValue children;
  base::DictValue child;
  child.Set("role", "textbox");
  child.Set("value", "cc-1234-5678-9012-3456");
  children.Append(std::move(child));
  root.Set("children", std::move(children));
  MahoMcpFirewall::RedactAxTreeAutofill(root);
  base::ListValue* out_children = root.FindList("children");
  ASSERT_TRUE(out_children);
  ASSERT_EQ(out_children->size(), 1u);
  const base::DictValue* out_child = (*out_children)[0].GetIfDict();
  ASSERT_TRUE(out_child);
  EXPECT_EQ(*out_child->FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, V4_OAuthImplicitFlowFragment) {
  const std::string url =
      "https://example.com/cb#access_token=abc&expires_in=3600";
  const std::string out = MahoMcpFirewall::RedactUrl(url);
  EXPECT_EQ(out.find("abc"), std::string::npos);
  EXPECT_NE(out.find(MahoMcpFirewall::kRedacted), std::string::npos);
  EXPECT_NE(out.find("expires_in=3600"), std::string::npos);
}

TEST(MahoMcpFirewallTest, V4_QueryParamsRedacted) {
  const std::string url =
      "https://api.example.com/foo?id_token=xyz&keep=this&session_id=zzz";
  const std::string out = MahoMcpFirewall::RedactUrl(url);
  EXPECT_EQ(out.find("xyz"), std::string::npos);
  EXPECT_EQ(out.find("zzz"), std::string::npos);
  EXPECT_NE(out.find("keep=this"), std::string::npos);
}

TEST(MahoMcpFirewallTest, V4_InvalidUrlPassthrough) {
  const std::string bad = "not a url";
  EXPECT_EQ(MahoMcpFirewall::RedactUrl(bad), bad);
}

TEST(MahoMcpFirewallTest, RedactAllTraversesJson) {
  base::DictValue payload;
  payload.Set("url", "https://x.com/cb#access_token=leaked");
  payload.Set("html", R"(<input type='password' value='leak'/>)");
  base::DictValue headers;
  headers.Set("Authorization", "Bearer leak");
  payload.Set("headers", std::move(headers));

  base::Value wrapped(std::move(payload));
  MahoMcpFirewall::RedactAll(wrapped);
  const base::DictValue& out = wrapped.GetDict();

  const std::string* url = out.FindString("url");
  ASSERT_TRUE(url);
  EXPECT_EQ(url->find("leaked"), std::string::npos);

  const std::string* html = out.FindString("html");
  ASSERT_TRUE(html);
  EXPECT_EQ(html->find("leak"), std::string::npos);

  const base::DictValue* h = out.FindDict("headers");
  ASSERT_TRUE(h);
  EXPECT_EQ(*h->FindString("Authorization"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, CredentialPredicates) {
  EXPECT_TRUE(MahoMcpFirewall::IsCredentialHeaderName("cookie"));
  EXPECT_TRUE(MahoMcpFirewall::IsCredentialHeaderName("Authorization"));
  EXPECT_TRUE(MahoMcpFirewall::IsCredentialHeaderName("X-Some-Auth-Value"));
  EXPECT_FALSE(MahoMcpFirewall::IsCredentialHeaderName("Content-Type"));

  EXPECT_TRUE(MahoMcpFirewall::IsCredentialParamName("access_token"));
  EXPECT_TRUE(MahoMcpFirewall::IsCredentialParamName("SessionKey"));
  EXPECT_FALSE(MahoMcpFirewall::IsCredentialParamName("page"));
}

TEST(MahoMcpFirewallTest, RedactPasswordFields_NestedInputTags) {
  const std::string html =
      R"(<form><div><input type="password" value="nested1"/></div>)"
      R"(<span><input type="password" value="nested2"/></span></form>)";
  const std::string out = MahoMcpFirewall::RedactPasswordFields(html);
  EXPECT_EQ(out.find("nested1"), std::string::npos);
  EXPECT_EQ(out.find("nested2"), std::string::npos);
  EXPECT_NE(out.find(MahoMcpFirewall::kRedacted), std::string::npos);
}

TEST(MahoMcpFirewallTest, RedactHeaders_CustomAuthToken) {
  base::DictValue headers;
  headers.Set("X-Custom-Auth-Token", "secret-value");
  headers.Set("Accept", "text/html");
  MahoMcpFirewall::RedactHeaders(headers);
  EXPECT_EQ(*headers.FindString("X-Custom-Auth-Token"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*headers.FindString("Accept"), "text/html");
}

TEST(MahoMcpFirewallTest, RedactUrl_AccessTokenQueryParam) {
  const std::string url =
      "https://api.example.com/data?access_token=xyz123&page=2";
  const std::string out = MahoMcpFirewall::RedactUrl(url);
  EXPECT_EQ(out.find("xyz123"), std::string::npos);
  EXPECT_NE(out.find("access_token=[REDACTED]"), std::string::npos);
  EXPECT_NE(out.find("page=2"), std::string::npos);
}

TEST(MahoMcpFirewallTest, RedactUrl_FragmentAccessToken) {
  const std::string url =
      "https://app.example.com/callback#access_token=leaked_token&type=bearer";
  const std::string out = MahoMcpFirewall::RedactUrl(url);
  EXPECT_EQ(out.find("leaked_token"), std::string::npos);
  EXPECT_NE(out.find("access_token=[REDACTED]"), std::string::npos);
  EXPECT_NE(out.find("type=bearer"), std::string::npos);
}

TEST(MahoMcpFirewallTest, RedactAll_NestedDictTraversal) {
  base::DictValue inner;
  inner.Set("url", "https://x.com/path?token=secret123");
  inner.Set("html",
            R"(<input type="password" value="deep_secret"/>)");

  base::DictValue outer;
  outer.Set("status", "ok");
  outer.Set("data", std::move(inner));

  base::Value wrapped(std::move(outer));
  MahoMcpFirewall::RedactAll(wrapped);

  const base::DictValue& root = wrapped.GetDict();
  const base::DictValue* data = root.FindDict("data");
  ASSERT_TRUE(data);
  const std::string* url = data->FindString("url");
  ASSERT_TRUE(url);
  EXPECT_EQ(url->find("secret123"), std::string::npos);
  EXPECT_NE(url->find("[REDACTED]"), std::string::npos);
  const std::string* html = data->FindString("html");
  ASSERT_TRUE(html);
  EXPECT_EQ(html->find("deep_secret"), std::string::npos);
}

TEST(MahoMcpFirewallTest, Wrap_AppliesAllVectors) {
  base::DictValue payload;
  payload.Set("url", "https://example.com/cb?access_token=leaked");
  payload.Set("html",
              R"(<input type="password" value="pw123"/>)");
  base::DictValue headers;
  headers.Set("Authorization", "Bearer secret");
  headers.Set("Content-Type", "text/html");
  payload.Set("headers", std::move(headers));

  Redacted<base::Value> safe =
      MahoMcpFirewall::Wrap(base::Value(std::move(payload)));

  const base::DictValue& out = safe.get().GetDict();

  const std::string* url = out.FindString("url");
  ASSERT_TRUE(url);
  EXPECT_EQ(url->find("leaked"), std::string::npos);
  EXPECT_NE(url->find("[REDACTED]"), std::string::npos);

  const std::string* html = out.FindString("html");
  ASSERT_TRUE(html);
  EXPECT_EQ(html->find("pw123"), std::string::npos);

  const base::DictValue* h = out.FindDict("headers");
  ASSERT_TRUE(h);
  EXPECT_EQ(*h->FindString("Authorization"), MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*h->FindString("Content-Type"), "text/html");
}

// Compile-time enforcement documentation:
// The following code (guarded by #if 0) documents that Redacted<T> cannot be
// constructed outside MahoMcpFirewall. If this #if 0 is changed to #if 1, it
// MUST produce a compile error (private constructor).
#if 0
TEST(MahoMcpFirewallTest, RedactedTypeCompileTimeEnforced) {
  // This should NOT compile — Redacted<T> ctor is private.
  Redacted<base::Value> illegal(base::Value("bypass"));
  (void)illegal;
}
#endif

}  // namespace maho
