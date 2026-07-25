// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_firewall.h"

#include "base/json/json_writer.h"
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

TEST(MahoMcpFirewallTest, V1_AutocompleteCredentialFieldsRedacted) {
  const std::string html =
      R"(<input value="S3NTINEL-current" autocomplete="current-password">)"
      R"(<input aria-label="One-time code" value="S3NTINEL-totp">)"
      R"(<textarea data-maho-field="recovery-code">S3NTINEL-recovery</textarea>)";
  const std::string out = MahoMcpFirewall::RedactPasswordFields(html);
  EXPECT_EQ(out.find("S3NTINEL-current"), std::string::npos);
  EXPECT_EQ(out.find("S3NTINEL-totp"), std::string::npos);
  EXPECT_EQ(out.find("S3NTINEL-recovery"), std::string::npos);
}

TEST(MahoMcpFirewallTest, V1_OrdinarySemanticFieldsSurvive) {
  const std::string html =
      R"(<input name="query" aria-label="Search" value="pizza recipes">)"
      R"(<textarea name="notes">ordinary notes</textarea>)";
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
  node.Set("protected", true);
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
  child.Set("autocomplete", "cc-number");
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

TEST(MahoMcpFirewallTest, Baseline_OrdinarySearchboxValuePreserved) {
  base::DictValue node;
  node.Set("role", "searchbox");
  node.Set("name", "Search");
  node.Set("value", "pizza recipes");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), "pizza recipes");
}

TEST(MahoMcpFirewallTest, Baseline_OrdinaryTextboxValuePreserved) {
  base::DictValue node;
  node.Set("role", "textbox");
  node.Set("name", "Full name");
  node.Set("value", "Ada Lovelace");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), "Ada Lovelace");
}

TEST(MahoMcpFirewallTest, Baseline_OrdinaryDictAndHeadersSurvive) {
  base::DictValue payload;
  payload.Set("status", "ok");
  payload.Set("title", "Weekly report");
  base::DictValue headers;
  headers.Set("Content-Type", "application/json");
  headers.Set("Accept-Language", "en-US");
  payload.Set("headers", std::move(headers));
  payload.Set("url", "https://example.com/docs?page=2&sort=asc");

  base::Value wrapped(std::move(payload));
  MahoMcpFirewall::RedactAll(wrapped);
  const base::DictValue& out = wrapped.GetDict();
  EXPECT_EQ(*out.FindString("status"), "ok");
  EXPECT_EQ(*out.FindString("title"), "Weekly report");
  const base::DictValue* h = out.FindDict("headers");
  ASSERT_TRUE(h);
  EXPECT_EQ(*h->FindString("Content-Type"), "application/json");
  EXPECT_EQ(*h->FindString("Accept-Language"), "en-US");
  const std::string* url = out.FindString("url");
  ASSERT_TRUE(url);
  EXPECT_NE(url->find("page=2"), std::string::npos);
  EXPECT_NE(url->find("sort=asc"), std::string::npos);
}

TEST(MahoMcpFirewallTest, Context_ProtectedFieldRedactedRegardlessOfRole) {
  base::DictValue node;
  node.Set("role", "generic");
  node.Set("protected", true);
  node.Set("value", "S3NTINEL-maho-vault-9F4C");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_InputTypePasswordRedacted) {
  base::DictValue node;
  node.Set("role", "generic");
  node.Set("inputType", "password");
  node.Set("value", "S3NTINEL-maho-vault-9F4C");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_AutocompleteCurrentPasswordRedacted) {
  base::DictValue node;
  node.Set("role", "custom-credential-widget");
  node.Set("autocomplete", "current-password");
  node.Set("value", "S3NTINEL-maho-vault-9F4C");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_AutocompleteOneTimeCodeRedacted) {
  base::DictValue node;
  node.Set("role", "generic");
  node.Set("autocomplete", "one-time-code");
  node.Set("value", "123456");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_CredentialNameRecoveryCodeRedacted) {
  base::DictValue node;
  node.Set("role", "generic");
  node.Set("name", "Recovery code");
  node.Set("value", "ABCD-EFGH-IJKL");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_CustomRoleNamePasswordRedacted) {
  base::DictValue node;
  node.Set("role", "custom-widget");
  node.Set("name", "Password");
  node.Set("value", "S3NTINEL-maho-vault-9F4C");
  MahoMcpFirewall::RedactAxTreeAutofill(node);
  EXPECT_EQ(*node.FindString("value"), MahoMcpFirewall::kRedacted);
}

TEST(MahoMcpFirewallTest, Context_RedactAllAppliesPreciseAxDetection) {
  base::DictValue password_node;
  password_node.Set("role", "textbox");
  password_node.Set("name", "Password");
  password_node.Set("protected", true);
  password_node.Set("value", "S3NTINEL-maho-vault-9F4C");

  base::DictValue search_node;
  search_node.Set("role", "searchbox");
  search_node.Set("name", "Search");
  search_node.Set("value", "quarterly earnings");

  base::ListValue children;
  children.Append(std::move(password_node));
  children.Append(std::move(search_node));

  base::DictValue form;
  form.Set("role", "form");
  form.Set("children", std::move(children));

  base::Value wrapped(std::move(form));
  MahoMcpFirewall::RedactAll(wrapped);

  const base::ListValue* out = wrapped.GetDict().FindList("children");
  ASSERT_TRUE(out);
  ASSERT_EQ(out->size(), 2u);
  EXPECT_EQ(*(*out)[0].GetDict().FindString("value"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*(*out)[1].GetDict().FindString("value"), "quarterly earnings");
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

TEST(MahoMcpFirewallTest, RedactString_JwtToken_Redacted) {
  std::string raw = "Here is my token: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c and it is secret.";
  std::string out = MahoMcpFirewall::RedactString(raw);
  EXPECT_EQ(out, "Here is my token: [REDACTED_JWT] and it is secret.");
}

TEST(MahoMcpFirewallTest, RedactString_BearerHeaderInLog_Redacted) {
  std::string raw = "Authorization failed: Bearer xyz123.abc_def";
  std::string out = MahoMcpFirewall::RedactString(raw);
  EXPECT_EQ(out, "Authorization failed: Bearer [REDACTED]");
}

TEST(MahoMcpFirewallTest, RedactString_NoPatternMatch_PassThrough) {
  std::string raw = "This is a normal log message without any credentials.";
  std::string out = MahoMcpFirewall::RedactString(raw);
  EXPECT_EQ(out, raw);
}

TEST(MahoMcpFirewallTest, RedactString_HeaderAndAssignmentValuesRedacted) {
  const std::string raw =
      "Authorization: Basic S3NTINEL-auth\n"
      "Cookie: session=S3NTINEL-cookie; theme=dark\n"
      "password=S3NTINEL-password&keep=ordinary";
  const std::string out = MahoMcpFirewall::RedactString(raw);
  EXPECT_EQ(out.find("S3NTINEL-auth"), std::string::npos);
  EXPECT_EQ(out.find("S3NTINEL-cookie"), std::string::npos);
  EXPECT_EQ(out.find("S3NTINEL-password"), std::string::npos);
  EXPECT_NE(out.find("keep=ordinary"), std::string::npos);
}

TEST(MahoMcpFirewallTest, RedactAll_NameValuePairsInNestedLists) {
  base::ListValue fields;
  base::DictValue password;
  password.Set("name", "current-password");
  password.Set("value", "S3NTINEL-password");
  fields.Append(std::move(password));
  base::DictValue ordinary;
  ordinary.Set("name", "query");
  ordinary.Set("value", "pizza recipes");
  fields.Append(std::move(ordinary));
  base::DictValue payload;
  payload.Set("fields", std::move(fields));

  base::Value wrapped(std::move(payload));
  MahoMcpFirewall::RedactAll(wrapped);

  const base::ListValue* out = wrapped.GetDict().FindList("fields");
  ASSERT_TRUE(out);
  EXPECT_EQ(*(*out)[0].GetDict().FindString("value"),
            MahoMcpFirewall::kRedacted);
  EXPECT_EQ(*(*out)[1].GetDict().FindString("value"), "pizza recipes");
}

TEST(MahoMcpFirewallTest, WrapHar_RedactsStandardHarMetadataShapes) {
  base::ListValue headers;
  base::DictValue authorization;
  authorization.Set("name", "Authorization");
  authorization.Set("value", "Basic S3NTINEL-auth");
  headers.Append(std::move(authorization));
  base::DictValue accept;
  accept.Set("name", "Accept");
  accept.Set("value", "application/json");
  headers.Append(std::move(accept));

  base::ListValue query;
  base::DictValue token;
  token.Set("name", "access_token");
  token.Set("value", "S3NTINEL-query");
  query.Append(std::move(token));

  base::ListValue params;
  base::DictValue password;
  password.Set("name", "password");
  password.Set("value", "S3NTINEL-post-param");
  params.Append(std::move(password));
  base::DictValue post_data;
  post_data.Set("params", std::move(params));
  post_data.Set("text", "password=S3NTINEL-post-text&keep=ordinary");

  base::DictValue request;
  request.Set("url",
              "https://example.test/login?access_token=S3NTINEL-url&keep=1");
  request.Set("headers", std::move(headers));
  request.Set("queryString", std::move(query));
  request.Set("postData", std::move(post_data));
  base::DictValue entry;
  entry.Set("request", std::move(request));
  base::ListValue entries;
  entries.Append(std::move(entry));
  base::DictValue har;
  har.Set("entries", std::move(entries));

  Redacted<base::DictValue> safe = MahoMcpFirewall::WrapHar(std::move(har));
  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(base::Value(safe.get().Clone()),
                                      &serialized));
  EXPECT_EQ(serialized.find("S3NTINEL-"), std::string::npos);
  EXPECT_NE(serialized.find("application/json"), std::string::npos);
  EXPECT_NE(serialized.find("keep=ordinary"), std::string::npos);
}

TEST(MahoMcpFirewallTest, RedactAll_CredentialShapedKeys) {
  base::DictValue dict;
  dict.Set("token", "secret_value");
  dict.Set("password", "my_password");
  dict.Set("message", "Bearer xyz123.abc");
  
  base::Value wrapped(std::move(dict));
  MahoMcpFirewall::RedactAll(wrapped);
  
  const base::DictValue& redacted = wrapped.GetDict();
  EXPECT_EQ(*redacted.FindString("token"), "[REDACTED]");
  EXPECT_EQ(*redacted.FindString("password"), "[REDACTED]");
  EXPECT_EQ(*redacted.FindString("message"), "Bearer [REDACTED]");
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
