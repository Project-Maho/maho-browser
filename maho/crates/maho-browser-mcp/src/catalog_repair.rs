// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bridge-side repair of the browser-published tool projection.
//!
//! The browser is authoritative for *which* capabilities exist and for the
//! policy attached to each one. This module never adds, removes, renames, or
//! re-authorizes a capability. It only repairs the parts of the published
//! descriptor that contradict the browser's own runtime parser, so a model
//! reading `tools/list` sees the argument shapes the browser actually accepts.
//!
//! Every entry below is anchored to the accepting parser branch in
//! `maho-chromium/browser/mcp/maho_mcp_session.cc`; the repair widens the
//! schema to the parser, never the other way round.

use serde_json::{json, Map, Value};

/// Applied to every descriptor before it is projected into an MCP `Tool`.
pub(crate) fn repair_descriptor(name: &str, schema: &mut Map<String, Value>) {
    repair_tab_id(schema);
    repair_locator(schema);
    repair_wait(schema);
    repair_observe(schema);
    repair_action(name, schema);
    repair_enums(name, schema);
}

/// A selection-useful description for `name`, or `None` to keep the
/// browser-published one.
///
/// P1-2 and the terse-description sweep. The browser ships a one-line gloss
/// per tool; several are indistinguishable from a sibling that costs very
/// different wire bytes, and a handful actively mislead about the result
/// shape. Each replacement states what the tool does, what it returns, and
/// when to prefer it over its neighbours.
pub(crate) fn describe(name: &str) -> Option<&'static str> {
    Some(match name {
        // --- P1-2: the three page reads were mutually indistinguishable. ---
        "browser_page_text" => {
            "Plain visible text of the page, with no structure or metadata. Returns \
             {text}. Cheapest page read — prefer it when you only need prose. Use \
             page_accessibility_snapshot_v2 instead if you intend to click or type."
        }
        "browser_page_context" => {
            "Document metadata plus structured body text. Returns {url, title, content, \
             meta_description, language}. Prefer it over browser_page_text when you need \
             the URL, title or language alongside the content."
        }
        "browser_page_content" => {
            "Full page content with credential redaction applied. Returns {text, url, \
             title, redacted}, where `redacted` reports whether sensitive fields were \
             masked. Largest of the three page reads — prefer browser_page_text unless \
             you need the redaction flag or the URL and title together."
        }

        // --- Name/behaviour mismatch: this returns a cursor, not text. ---
        "browser_search_in_page" => {
            "Run the in-page find bar for a query. Returns {match_count, \
             active_match_index} and no matched text. To read what matched, follow up \
             with browser_page_text or page_accessibility_snapshot_v2."
        }

        // --- The highest-traffic pair in the catalog. ---
        "browser_accessibility_snapshot" => {
            "Accessibility tree of the page with integer `ref` labels. Those refs are \
             what browser_click, browser_type and browser_hover consume. Prefer \
             page_accessibility_snapshot_v2, which returns the same refs but supports \
             scoping, size budgets and diffs."
        }
        "page_accessibility_snapshot_v2" => {
            "Accessibility snapshot with integer `ref` labels, scoped and budgeted. \
             Supersedes browser_accessibility_snapshot: pass `scope` to snapshot one \
             subtree, `max_bytes`/`max_depth` to bound the payload, or \
             `since_snapshot_token` to get only what changed. The primary way to find \
             elements to act on."
        }

        // --- Tabs. ---
        "browser_tab_list" => {
            "List open tabs. Returns {tabs:[{id, title, url, is_active, targetable, \
             stable_id, tab_strip_index}]}. Only rows with targetable=true can be passed \
             as tab_id to page and action tools. Start here when you need a tab_id."
        }
        "browser_tab_get" => {
            "Details of one tab by id. Returns the same fields as one browser_tab_list \
             row. Use it to re-read a known tab's title and URL without listing them all."
        }
        "browser_tab_new" => {
            "Open a new tab and make it active. Returns the created tab, including the \
             `id` to pass to later calls. Omit `url` for about:blank."
        }
        "browser_tab_switch" => {
            "Bring a tab to the foreground by id. Most page and action tools accept a \
             tab_id directly, so switch only when the tab must actually be visible."
        }
        "browser_tab_close" => "Close a tab by id. The tab_id becomes invalid afterwards.",

        // --- Navigation. ---
        "browser_navigate" => {
            "Navigate a tab to a URL. Returns once the load is committed, not once the \
             page is fully quiet — follow with browser_wait_for_navigation or a snapshot \
             if you need the settled page."
        }
        "browser_wait_for_navigation" => {
            "Block until the tab finishes navigating or `timeout_ms` elapses. Use after \
             a click that triggers a page load and before reading the new page."
        }

        // --- Input. ---
        "browser_click" => {
            "Click an element by its integer accessibility `ref` from a snapshot. Use \
             browser_locator_click instead when you want to target by CSS or role/name, \
             or need a post-click wait."
        }
        "browser_type" => {
            "Type text into an element by its integer accessibility `ref`. Credential \
             fields are refused unless `allow_credentials` is set."
        }
        "browser_hover" => {
            "Hover the pointer over an element by its integer accessibility `ref`. Use \
             it to open hover menus before snapshotting them."
        }
        "browser_select" => {
            "Choose an option in a <select> by accessibility `ref`, matching `value` \
             against the option values."
        }
        "browser_scroll" => {
            "Scroll the viewport, or one element when `ref` is given. Use it to bring \
             off-screen content into a subsequent snapshot."
        }
        "browser_key_press" => {
            "Press a single key, optionally with modifiers, in a tab. Use it for Enter, \
             Escape and Tab; use browser_type for text."
        }
        "browser_file_upload_select" => {
            "Supply a local file path to a file chooser that the page has already \
             opened. Trigger the chooser first by clicking the upload control."
        }

        // --- DOM by selector. ---
        "page_query_selector" => {
            "Find the first element matching a CSS selector. Returns {ref_id, text, tag}, \
             where `ref_id` is a string handle for page_get_text and page_get_attribute. \
             That handle is not the integer `ref` the click and type tools take."
        }
        "page_get_text" => {
            "Text content of an element previously returned by page_query_selector. \
             Takes the string `ref_id` from that call."
        }
        "page_get_attribute" => {
            "One attribute of an element previously returned by page_query_selector. \
             Returns {value}, null when the attribute is absent."
        }
        "page_wait_for_selector" => {
            "Block until a CSS selector appears or `timeout_ms` elapses. Returns {found, \
             ref_id}; check `found` rather than assuming success."
        }

        // --- Capture and diagnostics. ---
        "browser_screenshot_full" => {
            "Redacted full-page PNG screenshot. Costly in wire bytes — prefer \
             page_accessibility_snapshot_v2 unless you specifically need pixels."
        }
        "browser_screenshot_element" => {
            "Redacted PNG screenshot of one element by accessibility `ref`. Much smaller \
             than browser_screenshot_full when you only need one region."
        }
        "browser_console_messages" => {
            "Recent console messages for a tab. Narrow with `level_filter`, `limit` or \
             `since_timestamp_ms`. Use it to diagnose why a page misbehaved."
        }
        "browser_network_start_capture" => {
            "Begin recording network requests for a tab. Pair with \
             browser_network_stop_capture; nothing is recorded until you call this."
        }
        "browser_network_stop_capture" => {
            "Stop recording and return the captured traffic as HAR. Ends the session \
             started by browser_network_start_capture."
        }
        "browser_network_get_har" => {
            "Read the HAR captured so far without stopping the capture. Use it to \
             inspect traffic mid-flight."
        }
        "browser_set_viewport_size" => {
            "Resize the tab viewport in CSS pixels. Use it to exercise responsive \
             layouts before snapshotting or screenshotting."
        }

        // --- Profile data. ---
        "browser_history_search" => {
            "Search browsing history. Returns entries with {url, title, visited_at, \
             visit_count}. Searches pages already visited — use web_search for the \
             open web."
        }
        "browser_bookmarks_search" => {
            "Search saved bookmarks by query. Returns matching bookmarks with their \
             titles, URLs and folders."
        }
        "browser_bookmark_create" => {
            "Save a bookmark. `folder` is optional; without it the bookmark goes to \
             Maho's default folder."
        }
        "browser_same_origin_fetch" => {
            "Fetch an HTTPS URL using the current tab's exact origin, so the page's \
             cookies and session apply. Use it for same-site API calls that a plain \
             web fetch could not authenticate."
        }
        "browser_get_blocked_domains" => {
            "Read the session-scoped blocked-domain list currently in force."
        }
        "browser_set_blocked_domains" => {
            "Replace the session-scoped blocked-domain list. Replaces the whole list \
             rather than appending."
        }

        // --- Locator-based actions. ---
        "browser_locator_click" | "input.locator_click" => {
            "Click an element chosen by locator (`ref`, `css`, or `role`+`name`), with an \
             optional post-click wait and observation. Prefer it over browser_click when \
             you do not already hold a snapshot ref or you need to wait for the result."
        }
        "browser_locator_type" | "input.locator_type" => {
            "Type text into an element chosen by locator (`ref`, `css`, or `role`+`name`). \
             Set `submit` to press Enter afterwards."
        }
        "browser_act_and_observe" => {
            "Perform one action and return the resulting page change in a single \
             round-trip, saving a separate snapshot call. Not atomic: if the wait fails \
             the action has still been applied."
        }

        // --- Routines. ---
        "browser_routines_list" => {
            "List saved browser routines with the ids that browser_routines_run takes."
        }
        "browser_routines_run" => "Run a saved routine by id. Get ids from browser_routines_list.",

        // --- Mail. ---
        "mail_list_accounts" => {
            "List configured mail accounts. Returns the account_id values every other \
             mail tool requires — start here."
        }
        "mail_list_folders" => {
            "List folders in one mail account. Returns the folder_id values that \
             mail_list_emails takes."
        }
        "mail_list_emails" => {
            "List emails in a folder, newest first, with `limit` and `offset` paging. \
             Returns headers only — use mail_get_email for a body."
        }
        "mail_get_email" => "Fetch one email in full, including its body, by email_id.",
        "mail_search_emails" => {
            "Search emails by query, optionally narrowed to one account or folder. \
             Prefer it over listing a folder when you know what you are looking for."
        }
        "mail_list_thread" => "List every message in one conversation thread, oldest first.",
        // The mail write tools take an opaque `request_json` string whose
        // field names are owned by the C++ mail broker, so the bridge cannot
        // publish a typed shape without inventing one. What it can do is say
        // plainly that the argument is a JSON *string* and that these are
        // approval-gated, which the bare glosses did not.
        "mail_send" => {
            "Send an email. `request_json` is a JSON document encoded as a string, not an \
             object. Always prompts the user for explicit approval, and is refused if they \
             decline."
        }
        "mail_queue_email" => {
            "Queue an email for later delivery instead of sending it now. `request_json` is \
             a JSON document encoded as a string. Approval-gated like mail_send."
        }
        "mail_save_draft" => {
            "Save a new mail draft without sending it. `request_json` is a JSON document \
             encoded as a string. Use mail_update_draft to revise an existing draft."
        }
        "mail_update_draft" => {
            "Revise an existing draft, identified by `draft_id`. `request_json` is a JSON \
             document encoded as a string."
        }
        "mail_flag" => {
            "Change flags on a message, such as read, starred or archived. `request_json` \
             is a JSON document encoded as a string."
        }
        "mail_extract_otp" => {
            "Find a recent one-time passcode in mail and return the code, for completing \
             a login you have already started. Narrow with `max_age_seconds`."
        }
        _ => return None,
    })
}

/// A published `outputSchema` for `name`, or `None` when the result shape is
/// not known to the bridge.
///
/// P0-5. The browser publishes no output schema for any tool, so a model must
/// learn every result shape by trial. These shapes are the versioned wire
/// types in `protocol.rs`, so publishing them adds no new source of truth —
/// it exposes one that already exists.
pub(crate) fn output_schema(name: &str) -> Option<Value> {
    // Parsed from static text rather than built with `json!`, so the schema
    // documents read like the JSON they become.
    let raw: &str = match name {
        // TabListResult
        "browser_tab_list" => {
            r#"{"type":"object",
                "properties":{"tabs":{"type":"array","items":TAB_INFO}},
                "required":["tabs"]}"#
        }
        // TabNewResult
        "browser_tab_new" => {
            r#"{"type":"object","properties":{"tab":TAB_INFO},"required":["tab"]}"#
        }
        // TabInfo
        "browser_tab_get" => "TAB_INFO",
        // PageTextResult
        "browser_page_text" => {
            r#"{"type":"object",
                "properties":{"text":{"type":"string","description":"Plain visible page text."}},
                "required":["text"]}"#
        }
        // PageContentResult
        "browser_page_content" => {
            r#"{"type":"object",
                "properties":{
                  "text":{"type":"string"},
                  "url":{"type":"string"},
                  "title":{"type":"string"},
                  "redacted":{"type":"boolean",
                    "description":"Whether sensitive fields were masked in `text`."}},
                "required":["text","url","title","redacted"]}"#
        }
        // PageContextResult
        "browser_page_context" => {
            r#"{"type":"object",
                "properties":{
                  "url":{"type":"string"},
                  "title":{"type":"string"},
                  "content":{"type":"string","description":"Structured body text."},
                  "meta_description":{"type":["string","null"]},
                  "language":{"type":["string","null"],
                    "description":"Detected page language."}},
                "required":["url","title","content"]}"#
        }
        // SearchInPageResult — the shape most often guessed wrong.
        "browser_search_in_page" => {
            r#"{"type":"object",
                "description":"Find-bar counters only; carries no matched text.",
                "properties":{
                  "match_count":{"type":"integer","description":"Number of matches."},
                  "active_match_index":{"type":"integer",
                    "description":"0-based index of the active match, or -1 when there is none."}},
                "required":["match_count","active_match_index"]}"#
        }
        // QuerySelectorResult
        "page_query_selector" => {
            r#"{"type":"object",
                "properties":{
                  "ref_id":{"type":"string",
                    "description":"String handle for page_get_text / page_get_attribute. Not the integer `ref` used by click and type."},
                  "text":{"type":["string","null"]},
                  "tag":{"type":"string","description":"HTML tag name."}},
                "required":["ref_id","tag"]}"#
        }
        // GetTextResult
        "page_get_text" => {
            r#"{"type":"object","properties":{"text":{"type":"string"}},"required":["text"]}"#
        }
        // GetAttributeResult
        "page_get_attribute" => {
            r#"{"type":"object",
                "properties":{"value":{"type":["string","null"],
                  "description":"Attribute value, null when the attribute is absent."}},
                "required":["value"]}"#
        }
        // WaitForSelectorResult
        "page_wait_for_selector" => {
            r#"{"type":"object",
                "properties":{
                  "found":{"type":"boolean",
                    "description":"Whether the selector appeared before the timeout."},
                  "ref_id":{"type":["string","null"]}},
                "required":["found"]}"#
        }
        // HistorySearchResult
        "browser_history_search" => {
            r#"{"type":"object",
                "properties":{"entries":{"type":"array","items":{
                  "type":"object",
                  "properties":{
                    "url":{"type":"string"},
                    "title":{"type":"string"},
                    "visited_at":{"type":"string",
                      "description":"ISO 8601 timestamp of the last visit."},
                    "visit_count":{"type":"integer"}},
                  "required":["url","title","visited_at","visit_count"]}}},
                "required":["entries"]}"#
        }
        _ => return None,
    };
    // `TAB_INFO` is spliced rather than repeated so the tab shape has one
    // definition; `protocol.rs` `TabInfo` is the source of truth.
    serde_json::from_str(&raw.replace("TAB_INFO", TAB_INFO_SCHEMA)).ok()
}

/// `protocol.rs` `TabInfo`, shared by every tool that returns a tab.
const TAB_INFO_SCHEMA: &str = r#"{
    "type":"object",
    "description":"One browser tab.",
    "properties":{
      "id":{"type":"integer","description":"Browser-internal tab id."},
      "stable_id":{"type":"string","description":"Stable sidebar/core identity when exposed."},
      "title":{"type":"string"},
      "url":{"type":"string"},
      "is_active":{"type":"boolean","description":"Whether this is the foreground tab."},
      "targetable":{"type":"boolean",
        "description":"Whether `id` may be passed as tab_id to page and action tools."},
      "tab_strip_index":{"type":"integer",
        "description":"Tab-strip position, or -1 for sidebar/core-only rows."}},
    "required":["id","title","url","is_active"]}"#;

/// Parse a static schema fragment. The text is a compile-time literal owned by
/// this file, so a parse failure is a bug in the literal rather than anything
/// the browser or a caller can trigger; such a fragment is dropped instead of
/// corrupting the published schema.
fn parsed(raw: &str) -> Option<Value> {
    serde_json::from_str(raw).ok()
}

/// Insert a static schema fragment under `key`, leaving the schema untouched
/// if the literal does not parse.
fn set_parsed(target: &mut Map<String, Value>, key: &str, raw: &str) {
    if let Some(value) = parsed(raw) {
        target.insert(key.to_string(), value);
    }
}

fn properties_mut(schema: &mut Map<String, Value>) -> Option<&mut Map<String, Value>> {
    schema.get_mut("properties")?.as_object_mut()
}

fn property_mut<'a>(
    schema: &'a mut Map<String, Value>,
    key: &str,
) -> Option<&'a mut Map<String, Value>> {
    properties_mut(schema)?.get_mut(key)?.as_object_mut()
}

/// P1-6. `tab_id: 0` is documented as "the active tab" sentinel, but `0` is
/// also a legitimate `TabInfo.id` (see `protocol.rs` `TabInfo`), so a model
/// that reads a real tab `0` from `browser_tab_list` and passes it through
/// silently addresses the active tab instead.
///
/// The browser resolves absent and `0` identically
/// (`maho_mcp_session.cc:1652-1654`, `:1682-1684`), so the sentinel cannot be
/// removed from the wire without a browser change. What the bridge can do is
/// stop advertising `0` as a way to *say* "active": omission is the
/// unambiguous spelling, and `minimum: 1` makes a real-tab `0` a client-side
/// validation failure instead of a silent wrong-tab read.
fn repair_tab_id(schema: &mut Map<String, Value>) {
    let Some(tab_id) = property_mut(schema, "tab_id") else {
        return;
    };
    if tab_id.get("type").and_then(Value::as_str) != Some("integer") {
        return;
    }
    tab_id.insert("minimum".to_string(), Value::from(1));
    tab_id.insert(
        "description".to_string(),
        Value::from(
            "Browser tab id from browser_tab_list. Mutating tools require it: \
             omitting one fails with -32013 instead of following the focused tab. \
             Read-only tools omit it to target the active tab. Do not pass 0: it is \
             the legacy active-tab sentinel and cannot address a real tab 0.",
        ),
    );
}

/// P1-5. The locator object declares every sub-field optional, so `{}` and
/// `{"role":"button"}` both validate and both fail at runtime with
/// `locator_not_found`. State the real constraint — "ref, css, or role/name"
/// — in the description only.
///
/// It must NOT be published as `anyOf`/`oneOf` next to `properties`: Gemini's
/// function-declaration converter rejects that shape for the whole request
/// (`properties[locator].properties: only allowed for OBJECT type`), so every
/// tool-bearing turn on a Gemini model failed with a 400. The browser parser
/// still rejects an unsatisfiable locator at runtime.
///
/// Parser: `maho_mcp_session.cc:3667-3686`.
fn repair_locator(schema: &mut Map<String, Value>) {
    let Some(locator) = property_mut(schema, "locator") else {
        return;
    };
    if !locator.contains_key("properties") {
        return;
    }
    locator.remove("anyOf");
    locator.insert(
        "description".to_string(),
        Value::from(
            "Target element. Supply exactly one of: `ref` (integer from an accessibility \
             snapshot, most reliable), `css` (CSS selector), or `role` plus `name` \
             (accessible role and label). A locator with none of those never resolves.",
        ),
    );
}

/// P0-2. The browser reads `wait` as a dict only
/// (`maho_mcp_session.cc:3813`), reading `mode`, `timeout_ms` and `selector`
/// from it. Two published shapes contradict that parser:
///
/// * `browser_act_and_observe` publishes a bare untyped `{"type":"object"}`,
///   so the model cannot know any of the three field names.
/// * The locator tools publish `mode` and `timeout_ms` but omit `selector`,
///   which the parser reads at `:3821` and which `mode: "selector"` requires.
fn repair_wait(schema: &mut Map<String, Value>) {
    let Some(wait) = property_mut(schema, "wait") else {
        return;
    };
    if wait.get("type").and_then(Value::as_str) != Some("object") {
        return;
    }
    let properties = wait
        .entry("properties")
        .or_insert_with(|| json!({}))
        .as_object_mut();
    let Some(properties) = properties else {
        return;
    };
    set_parsed(properties, "mode", WAIT_MODE_SCHEMA);
    set_parsed(properties, "timeout_ms", WAIT_TIMEOUT_SCHEMA);
    set_parsed(properties, "selector", WAIT_SELECTOR_SCHEMA);
    wait.insert(
        "description".to_string(),
        Value::from(
            "Post-action wait. Omit to return as soon as the action dispatches; a bare \
             `{}` waits for the page to go quiet.",
        ),
    );
}

const WAIT_MODE_SCHEMA: &str = r#"{
    "type":"string",
    "enum":["none","auto","navigation","selector"],
    "description":"Wait strategy applied after the action. `auto` waits for the page to go quiet, `navigation` waits for a document change, `selector` waits for `selector` to appear, `none` returns immediately. Defaults to `auto` when `wait` is present without `mode`."}"#;

const WAIT_TIMEOUT_SCHEMA: &str = r#"{
    "type":"integer","minimum":1,"default":10000,
    "description":"Maximum wait in milliseconds. Defaults to 10000."}"#;

const WAIT_SELECTOR_SCHEMA: &str = r#"{
    "type":"string",
    "description":"CSS selector to wait for. Required when `mode` is `selector`."}"#;

/// The string-or-dict union the browser's `observe` parser actually accepts.
const OBSERVE_ONE_OF: &str = r#"[
    {"type":"string","enum":["diff","none"]},
    {"type":"object",
     "properties":{"mode":{"type":"string","enum":["diff","none"]}},
     "required":["mode"]}]"#;

/// P0-2 companion. The browser accepts `observe` as **either** a bare string
/// **or** a dict carrying `mode` (`maho_mcp_session.cc:3623-3630`), but each
/// published schema documents only one of the two:
///
/// * locator tools publish `{"type":"string"}` — the dict form is hidden;
/// * `browser_act_and_observe` publishes an untyped `{"type":"object"}` — the
///   string form is hidden and the `mode` field name is undiscoverable.
///
/// Publish the union the parser actually implements.
fn repair_observe(schema: &mut Map<String, Value>) {
    let Some(observe) = property_mut(schema, "observe") else {
        return;
    };
    if observe.contains_key("oneOf") {
        return;
    }
    observe.remove("type");
    observe.remove("enum");
    observe.remove("properties");
    set_parsed(observe, "oneOf", OBSERVE_ONE_OF);
    observe.insert(
        "description".to_string(),
        Value::from(
            "What to report after the action. `diff` returns an accessibility diff against \
             the pre-action snapshot; `none` skips observation. Accepts the bare string or \
             `{\"mode\": ...}`.",
        ),
    );
}

/// P0-1. `browser_act_and_observe` publishes `action` as an untyped
/// `{"type":"object"}`, yet the parser reads a specific shape from it:
/// `kind` (`maho_mcp_session.cc:3761`), `text` (`:3776-3781`) and a nested
/// `locator` (`:3661`) used when no top-level `locator` was supplied.
/// None of those field names are discoverable from the published schema, so a
/// model must guess them; a wrong guess is silently dropped rather than
/// rejected. Publish the shape the parser reads.
/// The fields `browser_act_and_observe`'s parser actually reads from `action`.
const ACTION_PROPERTIES: &str = r#"{
    "kind":{"type":"string","enum":["click","type","navigate","observe"],
      "description":"Which action to perform."},
    "text":{"type":"string",
      "description":"Text to type when `kind` is `type`. A top-level `text` takes precedence."},
    "locator":{"type":"object",
      "description":"Target element, same shape as the top-level `locator`. Only consulted when no top-level `locator` is supplied.",
      "properties":{
        "ref":{"type":"integer","description":"Accessibility ref."},
        "css":{"type":"string","description":"CSS selector."},
        "role":{"type":"string","description":"Accessibility role."},
        "name":{"type":"string","description":"Accessible name."},
        "exact":{"type":"boolean","description":"Require an exact accessible-name match."}}}}"#;

fn repair_action(name: &str, schema: &mut Map<String, Value>) {
    if name != "browser_act_and_observe" {
        return;
    }
    let Some(action) = property_mut(schema, "action") else {
        return;
    };
    if action.get("type").and_then(Value::as_str) != Some("object") {
        return;
    }
    if action.contains_key("properties") {
        return;
    }
    set_parsed(action, "properties", ACTION_PROPERTIES);
    set_parsed(action, "required", r#"["kind"]"#);
    action.insert(
        "description".to_string(),
        Value::from("The action to perform, as `{\"kind\": ...}` plus the fields that kind needs."),
    );
}

/// P1-1. Legal values that the runtime hard-compares against a fixed set are
/// published as bare strings with the values buried in prose, so an
/// out-of-set value is schema-valid and then a silent no-op. Promote the
/// closed sets to `enum`.
fn repair_enums(name: &str, schema: &mut Map<String, Value>) {
    // `lease`: the runtime recognises exactly the literal "scoped"
    // (`maho_mcp_session.cc:3596-3598`). Any other value silently skips lease
    // acquisition and the mutation then fails with -32007.
    if let Some(lease) = property_mut(schema, "lease") {
        if lease.get("type").and_then(Value::as_str) == Some("string") {
            set_parsed(lease, "enum", r#"["scoped"]"#);
            lease.insert(
                "description".to_string(),
                Value::from(
                    "Set to `scoped` to acquire a scoped tab lease for this mutation. \
                     Omit only when the caller already holds a lease; otherwise the call \
                     fails with -32007 (active tab lease required).",
                ),
            );
        }
    }

    // `direction` on browser_scroll: prose-only "up, down, left, or right".
    if name == "browser_scroll" {
        if let Some(direction) = property_mut(schema, "direction") {
            if direction.get("type").and_then(Value::as_str) == Some("string") {
                set_parsed(direction, "enum", r#"["up","down","left","right"]"#);
            }
        }
        if let Some(pixels) = property_mut(schema, "pixels") {
            pixels.insert("default".to_string(), Value::from(300));
        }
    }

    // `method` on browser_same_origin_fetch: prose-only "GET or POST".
    if name == "browser_same_origin_fetch" {
        if let Some(method) = property_mut(schema, "method") {
            if method.get("type").and_then(Value::as_str) == Some("string") {
                set_parsed(method, "enum", r#"["GET","POST"]"#);
                method.insert("default".to_string(), Value::from("GET"));
            }
        }
    }

    // `mode` on the v2 snapshot: prose-only "interactive, compact, full".
    if name == "page_accessibility_snapshot_v2" {
        if let Some(mode) = property_mut(schema, "mode") {
            if mode.get("type").and_then(Value::as_str) == Some("string") {
                set_parsed(mode, "enum", r#"["interactive","compact","full"]"#);
                mode.insert("default".to_string(), Value::from("interactive"));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn schema(value: Value) -> Map<String, Value> {
        value.as_object().cloned().expect("object schema")
    }

    /// Gemini rejects a schema node that carries `properties` next to a
    /// combinator (`properties[locator].properties: only allowed for OBJECT
    /// type`), failing the whole request.
    fn assert_no_combinator_beside_properties(path: &str, node: &Value) {
        if let Some(obj) = node.as_object() {
            if obj.contains_key("properties") {
                for key in ["anyOf", "oneOf", "allOf"] {
                    assert!(
                        !obj.contains_key(key),
                        "{path} publishes `{key}` beside `properties`"
                    );
                }
            }
            for (k, v) in obj {
                assert_no_combinator_beside_properties(&format!("{path}.{k}"), v);
            }
        } else if let Some(items) = node.as_array() {
            for (i, v) in items.iter().enumerate() {
                assert_no_combinator_beside_properties(&format!("{path}[{i}]"), v);
            }
        }
    }

    #[test]
    fn locator_schemas_stay_gemini_compatible() {
        for name in ["input.locator_click", "browser_act_and_observe"] {
            let mut s = schema(json!({
                "type": "object",
                "properties": {
                    "action": {"type": "object"},
                    "locator": {
                        "type": "object",
                        "properties": {
                            "ref": {"type": "integer"},
                            "css": {"type": "string"},
                            "role": {"type": "string"},
                            "name": {"type": "string"}
                        },
                        "anyOf": [{"required": ["ref"]}]
                    }
                }
            }));
            repair_descriptor(name, &mut s);
            assert_no_combinator_beside_properties(name, &Value::Object(s.clone()));
            let description = s["properties"]["locator"]["description"]
                .as_str()
                .expect("locator keeps the constraint in its description");
            assert!(description.contains("`role` plus `name`"));
        }
    }

    #[test]
    fn wait_object_publishes_every_field_the_parser_reads() {
        // The parser reads mode, timeout_ms and selector; the locator tools
        // published only the first two, so `mode: "selector"` was unusable.
        let mut s = schema(json!({
            "type": "object",
            "properties": {
                "wait": {
                    "type": "object",
                    "properties": {
                        "mode": {"type": "string", "enum": ["none", "auto", "navigation", "selector"]},
                        "timeout_ms": {"type": "integer"}
                    }
                }
            }
        }));
        repair_descriptor("input.locator_click", &mut s);
        let wait = &s["properties"]["wait"]["properties"];
        assert_eq!(wait["selector"]["type"], "string");
        assert_eq!(
            wait["mode"]["enum"],
            json!(["none", "auto", "navigation", "selector"])
        );
        assert_eq!(wait["timeout_ms"]["default"], 10000);
    }

    #[test]
    fn untyped_wait_object_gains_the_parsers_field_names() {
        // browser_act_and_observe published a bare {"type":"object"}.
        let mut s = schema(json!({
            "type": "object",
            "properties": {"wait": {"type": "object", "description": "Wait specification."}}
        }));
        repair_descriptor("browser_act_and_observe", &mut s);
        let wait = s["properties"]["wait"]["properties"]
            .as_object()
            .expect("untyped wait object is now typed");
        assert!(wait.contains_key("mode"));
        assert!(wait.contains_key("timeout_ms"));
        assert!(wait.contains_key("selector"));
    }

    #[test]
    fn observe_publishes_the_string_or_dict_union_the_parser_accepts() {
        // Parser: FindString("observe") else FindDict("observe")/mode.
        for (name, published) in [
            (
                "input.locator_click",
                json!({"type": "string", "enum": ["diff", "none"]}),
            ),
            ("browser_act_and_observe", json!({"type": "object"})),
        ] {
            let mut s = schema(json!({
                "type": "object",
                "properties": {"observe": published}
            }));
            repair_descriptor(name, &mut s);
            let observe = &s["properties"]["observe"];
            let one_of = observe["oneOf"]
                .as_array()
                .expect("observe publishes oneOf");
            assert_eq!(one_of.len(), 2, "{name}: both accepted forms are published");
            assert_eq!(one_of[0]["type"], "string");
            assert_eq!(one_of[1]["type"], "object");
            assert_eq!(
                one_of[1]["properties"]["mode"]["enum"],
                json!(["diff", "none"])
            );
            assert!(
                observe.get("type").is_none(),
                "{name}: the single-form `type` must be dropped, it contradicts the union"
            );
        }
    }

    #[test]
    fn act_and_observe_action_publishes_the_fields_the_parser_reads() {
        let mut s = schema(json!({
            "type": "object",
            "properties": {"action": {"type": "object", "description": "Action specification."}}
        }));
        repair_descriptor("browser_act_and_observe", &mut s);
        let action = &s["properties"]["action"];
        assert_eq!(action["required"], json!(["kind"]));
        let props = action["properties"].as_object().expect("action is typed");
        assert!(props.contains_key("kind"), "parser reads action.kind");
        assert!(props.contains_key("text"), "parser reads action.text");
        assert!(
            props.contains_key("locator"),
            "parser falls back to action.locator when no top-level locator"
        );
        assert_eq!(
            props["kind"]["enum"],
            json!(["click", "type", "navigate", "observe"])
        );
    }

    #[test]
    fn action_repair_is_scoped_to_the_tool_that_owns_it() {
        let mut s = schema(json!({
            "type": "object",
            "properties": {"action": {"type": "object"}}
        }));
        repair_descriptor("some_other_tool", &mut s);
        assert!(
            s["properties"]["action"].get("properties").is_none(),
            "only browser_act_and_observe's action shape is known"
        );
    }

    #[test]
    fn lease_enumerates_the_only_value_the_runtime_accepts() {
        let mut s = schema(json!({
            "type": "object",
            "properties": {"lease": {"type": "string", "description": "Lease management mode (e.g. 'scoped')."}}
        }));
        repair_descriptor("input.locator_click", &mut s);
        assert_eq!(s["properties"]["lease"]["enum"], json!(["scoped"]));
    }

    #[test]
    fn closed_string_sets_become_enums() {
        let mut scroll = schema(json!({
            "type": "object",
            "properties": {
                "direction": {"type": "string", "description": "Scroll direction: up, down, left, or right."},
                "pixels": {"type": "integer"}
            }
        }));
        repair_descriptor("browser_scroll", &mut scroll);
        assert_eq!(
            scroll["properties"]["direction"]["enum"],
            json!(["up", "down", "left", "right"])
        );
        assert_eq!(scroll["properties"]["pixels"]["default"], 300);

        let mut fetch = schema(json!({
            "type": "object",
            "properties": {"method": {"type": "string", "description": "HTTP method: GET or POST."}}
        }));
        repair_descriptor("browser_same_origin_fetch", &mut fetch);
        assert_eq!(
            fetch["properties"]["method"]["enum"],
            json!(["GET", "POST"])
        );

        let mut snapshot = schema(json!({
            "type": "object",
            "properties": {"mode": {"type": "string"}}
        }));
        repair_descriptor("page_accessibility_snapshot_v2", &mut snapshot);
        assert_eq!(
            snapshot["properties"]["mode"]["enum"],
            json!(["interactive", "compact", "full"])
        );
    }

    #[test]
    fn tab_id_stops_advertising_the_colliding_zero_sentinel() {
        let mut s = schema(json!({
            "type": "object",
            "properties": {
                "tab_id": {
                    "type": "integer",
                    "description": "Browser tab id. Omit or pass 0 for the active tab."
                }
            }
        }));
        repair_descriptor("browser_page_text", &mut s);
        let tab_id = &s["properties"]["tab_id"];
        assert_eq!(
            tab_id["minimum"], 1,
            "tab 0 must fail validation rather than silently retarget the active tab"
        );
        let description = tab_id["description"].as_str().expect("description");
        assert!(
            !description.contains("pass 0 for the active tab"),
            "0 must no longer be advertised as the way to say 'active'"
        );
        assert!(
            description.contains("-32013"),
            "mutations must be told the id is required, not that omitting it retargets the active tab"
        );
        assert!(description.contains("Read-only"));
    }

    /// P0-5. Every published output schema must match the versioned wire type
    /// in `protocol.rs` that the browser actually returns.
    #[test]
    fn output_schemas_match_the_declared_wire_types() {
        // SearchInPageResult: counters, no text. This is the shape a model
        // most often guesses wrong, so pin it exactly.
        let search = output_schema("browser_search_in_page").expect("search has a known shape");
        assert_eq!(
            search["required"],
            json!(["match_count", "active_match_index"])
        );
        assert!(
            search["properties"].get("text").is_none(),
            "search_in_page returns no text; publishing one would relearn the original bug"
        );

        // PageContentResult carries the redaction flag; PageTextResult does not.
        let content = output_schema("browser_page_content").expect("page_content");
        assert_eq!(
            content["required"],
            json!(["text", "url", "title", "redacted"])
        );
        let text = output_schema("browser_page_text").expect("page_text");
        assert_eq!(text["required"], json!(["text"]));

        // TabInfo.targetable is what makes a tab addressable (P1-6).
        let tabs = output_schema("browser_tab_list").expect("tab_list");
        let item = &tabs["properties"]["tabs"]["items"]["properties"];
        assert_eq!(item["targetable"]["type"], "boolean");
        assert_eq!(item["id"]["type"], "integer");

        // page_query_selector returns a *string* ref_id, distinct from the
        // integer accessibility ref the click/type tools take.
        let qs = output_schema("page_query_selector").expect("query_selector");
        assert_eq!(qs["properties"]["ref_id"]["type"], "string");

        // Unknown tools stay absent rather than getting an invented shape.
        assert!(output_schema("browser_set_blocked_domains").is_none());
    }

    /// P1-2. The three page reads shared near-identical glosses and an
    /// identical schema, so a model had to probe to pick one.
    #[test]
    fn page_reads_are_distinguishable_and_state_what_they_return() {
        let text = describe("browser_page_text").expect("page_text");
        let context = describe("browser_page_context").expect("page_context");
        let content = describe("browser_page_content").expect("page_content");

        assert_ne!(text, context);
        assert_ne!(context, content);
        assert_ne!(text, content);

        // Each names a distinguishing field of its own result type.
        assert!(text.contains("Cheapest"));
        assert!(context.contains("meta_description"));
        assert!(content.contains("redacted"));

        // Each points at when to prefer a neighbour.
        for description in [text, context, content] {
            assert!(
                description.contains("Prefer")
                    || description.contains("prefer")
                    || description.contains("instead"),
                "{description:?} gives no selection guidance"
            );
        }
    }

    /// The terse-description sweep: replacements must actually be
    /// selection-useful, not merely longer.
    #[test]
    fn replacement_descriptions_are_substantive() {
        for name in [
            "browser_tab_list",
            "browser_search_in_page",
            "page_accessibility_snapshot_v2",
            "browser_screenshot_full",
            "mail_list_accounts",
        ] {
            let description = describe(name).unwrap_or_else(|| panic!("{name} has no description"));
            assert!(
                description.len() >= 40,
                "{name}: {description:?} is still terse"
            );
            assert!(
                description.trim_end().ends_with('.'),
                "{name}: descriptions are complete sentences"
            );
        }

        // browser_search_in_page must warn that it returns no text, since its
        // name promises otherwise.
        let search = describe("browser_search_in_page").expect("search");
        assert!(search.contains("no matched text"));

        // The v1/v2 snapshot pair must say which supersedes which.
        assert!(describe("browser_accessibility_snapshot")
            .expect("v1")
            .contains("Prefer page_accessibility_snapshot_v2"));
        assert!(describe("page_accessibility_snapshot_v2")
            .expect("v2")
            .contains("Supersedes"));
    }

    /// The mail write tools take an opaque `request_json` *string*. The bridge
    /// must not invent a typed shape for it, but it must at least stop the
    /// model reading "JSON request" as "pass an object here".
    #[test]
    fn mail_write_descriptions_say_the_payload_is_a_string() {
        for name in [
            "mail_send",
            "mail_queue_email",
            "mail_save_draft",
            "mail_update_draft",
            "mail_flag",
        ] {
            let description = describe(name).unwrap_or_else(|| panic!("{name} has no description"));
            assert!(
                description.contains("encoded as a string"),
                "{name}: must say the payload is a string, not an object"
            );
            assert!(description.len() >= 40, "{name}: still terse");
        }

        // No invented field names: the broker owns that contract.
        for name in ["mail_send", "mail_queue_email"] {
            let description = describe(name).expect("description");
            for invented in ["\"to\"", "\"subject\"", "\"body\"", "recipients"] {
                assert!(
                    !description.contains(invented),
                    "{name}: must not invent broker field names ({invented})"
                );
            }
        }
    }

    #[test]
    fn unknown_tools_keep_the_browser_published_description() {
        assert!(describe("some_future_browser_tool").is_none());
    }

    #[test]
    fn repair_leaves_unrelated_schemas_untouched() {
        let original = json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {"query": {"type": "string"}},
            "required": ["query"]
        });
        let mut s = schema(original.clone());
        repair_descriptor("browser_bookmarks_search", &mut s);
        assert_eq!(Value::Object(s), original);
    }
}
