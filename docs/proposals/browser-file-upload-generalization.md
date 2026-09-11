# Browser file upload generalization: code review and modification plan

**Status:** Review and implementation proposal; no runtime changes are made by this document.  
**Review date:** 2026-09-11.  
**Workspace baseline:** `80b437e1342703d51142c963160fddec8a7b898e`, plus the working-tree files inspected during this review. The workspace already contains unrelated changes; this is not a claim that the checkout is clean.  
**Local Chromium version:** `149.0.7795.0`, from `chromium/src/chrome/VERSION:1-4`. This is the inspected version, not a claim about the latest Chromium release.

## 1. Executive conclusion

Generalize the existing **single-file selection** capability, keeping its tool name, canonical capability ID, `path` parameter, approval requirements, lease requirements, and Chromium-native delivery mechanism. Add a separate capability for actual drag-and-drop; do not silently turn chooser selection into drop injection.

**The reported extension restriction is not present in the inspected live implementation.** The production delegate currently checks `IsAbsolute()` and `ReferencesParent()` and then calls `MahoCompleteAutomationFileChooser`; it does not check the extension. The CLI and shared upload handler likewise contain no `.aab` allowlist. The remaining AAB restriction is visible in help, schema descriptions, and an interface comment. Consequently, a correct patch against this baseline must not pretend to remove a nonexistent `ends_with(".aab")` check. A different deployed binary or branch may still contain one and must be compared separately. Evidence: S1, S4, S5, and S9 below.

The review also found issues that a wording-only change would miss:

| Finding | Priority | Consequence |
| --- | --- | --- |
| The packaged CLI catalog requires `file_path`, but the live schema and handler require `path`. | P1, functional | Offline discovery can generate an invalid request. |
| The desktop agent advertises upload but its action executor lacks an upload dispatch branch. | P1, functional | Approval can succeed and execution still returns “registered but not implemented.” |
| The production selection boundary has no explicit existence, regular-file, or readability validation. | P1, safety/reliability | Arbitrary absolute paths can reach Chromium's file-permission grant path; a successful tool response does not establish that the file can be read. |
| Pending chooser state is not bound to its initiating document or lease owner/generation. | P1, hardening | Tab lifetime alone is insufficient to prove that a later selection targets the intended request. |
| The chooser hook exists in the local Chromium tree, but no corresponding hook was found in the reviewed overlay patch recipes. | P1, release reproducibility investigation | A successful local build is insufficient evidence that a fresh overlay application recreates the feature. |
| The CLI upload path uses a reconnect-and-replay transport helper. | P2, mutation reliability | An ambiguous disconnect can replay a file-selection request. |
| AAB-only text and limited upload tests remain. | P2, discoverability/coverage | Users and agents are told the wrong contract; existing mock tests do not prove real file delivery. |

P1 here means “resolve or explicitly disposition before broadly advertising this workflow,” not a demonstrated exploit or an assertion that every deployment is affected. Drag-and-drop is a separate feature proposal, not a prerequisite for correcting chooser upload.

## 2. Scope, method, and limits

The review followed the CLI REPL, CLI discovery snapshot, Rust browser-MCP transport, C++ tool registry and safety contract, desktop-agent executor, MCP session, shared browser-action handler, production delegate, pending chooser storage, and the local Chromium listener implementation. It also inspected existing CLI, registry, executor, MCP, build-recipe, and Chromium drag-input code.

Searches covered `SelectFileForPendingChooser`, `MahoCompleteAutomationFileChooser`, `MahoBeginAutomationFileChooser`, `browser_file_upload_select`, `input.file_upload_select`, `.aab`, suffix-check variants, and drag/drop APIs. Relevant Rust FFI and C++/Rust bridge directories were searched as well: the inspected upload handoff is a **C++ delegate and Blink file-selection listener**, not a dedicated Rust FFI upload function that needs a new ABI.

Tracked-source searches were supplemented by direct inspection of local build scripts and specific files in `chromium/src`, because a tracked-only search does not capture all local generated/upstream state. Historical agent logs and bulk build artifacts were not treated as implementation evidence.

This review does **not** establish successful uploads to Microsoft Partner Center, Apple App Store Connect, Google Play Console, or any other live service. It does not run store submissions, install packages, launch browser UI, or compile Chromium. Those activities belong to the implementation verification plan. Arbitrary file selection is not a promise that a particular portal accepts a particular package or exposes a browser-upload workflow for it.

## 3. Source inventory and affected locations

All paths are relative to the workspace root. Line ranges describe the reviewed baseline; use the named symbols when subsequent edits shift them. “Change” means a proposed future implementation change, not a change made during this review.

| ID | File and lines | Role and planned treatment |
| --- | --- | --- |
| S1 | `maho/crates/maho-cli/src/main.rs:4935-4956`, `maho/crates/maho-cli/src/main.rs:5438-5451` | REPL help and upload dispatch. Generalize text; preserve `path` forwarding and single-file semantics. |
| S2 | `maho/crates/maho-cli/src/main.rs:3589-3606` | Live discovery versus packaged fallback. Add upload-specific parity coverage. |
| S3 | `maho/crates/maho-cli/src/tool_catalog_snapshot.json:563-595` | Stale AAB description and incorrect `file_path` property/required field. Synchronize with the live public schema. |
| S4 | `maho-chromium/browser/ai/maho_browser_tool_registry.cc:395-398` | Live upload schema already requires `path`; update its description only in the minimal patch. |
| S5 | `maho-chromium/browser/mcp/maho_mcp_browser_action_handler.h:286-313` | `HandleFileUpload`: nonempty string validation, delegate call, generic failure, `file_selected` response. Extend carefully for typed failures. |
| S6 | `maho-chromium/browser/mcp/maho_mcp_session.h:643-646`, `maho-chromium/browser/mcp/maho_mcp_session.cc:5425-5429` | Interface comment and default-false implementation. Generalize comment; update signature only with the asynchronous hardening step. |
| S7 | `maho-chromium/browser/mcp/maho_mcp_session.cc:5408-5417` | MCP upload dispatch to the shared handler using the resolved tab. Preserve this routing. |
| S8 | `maho-chromium/browser/maho_browser_main_extra_parts.cc:936-1040` | Pending chooser storage, lease-based interception, supersession cancellation, and native-file completion. Add document/owner/mode/lifecycle protection. |
| S9 | `maho-chromium/browser/maho_browser_main_extra_parts.cc:4583-4592` | Production `SelectFileForPendingChooser`; currently extension-neutral with absolute-path and parent-reference checks. Add valid-file checks without losing these guards. |
| S10 | `maho-chromium/browser/ai/maho_browser_capability_catalog.def:103`, `maho-chromium/browser/ai/maho_browser_action_contract.def:4-8`, `maho-chromium/browser/ai/maho_browser_action_contract.def:23` | Generic capability description and sensitive, approval-required, lease-required contract. Preserve for selection; introduce a separate contract for drop. |
| S11 | `maho-chromium/browser/ai/maho_browser_action_contract.h:89-126` | Boundary classification explicitly recognizes `input.file_upload_select`. A future `input.file_drop` must be classified too. |
| S12 | `maho-chromium/browser/ai/maho_browser_tool_executor.cc:667-744`, `maho-chromium/browser/ai/maho_browser_tool_executor.cc:945-995` | Desktop-agent resolution/authorization and dispatch chain. Add the missing upload branch. |
| S13 | `maho-chromium/browser/mcp/maho_mcp_session.cc:1936-2008`, `maho-chromium/browser/mcp/maho_mcp_session.cc:2070-2102`, `maho-chromium/browser/mcp/maho_mcp_session.cc:2150-2165`, `maho-chromium/browser/mcp/maho_mcp_session.cc:2229-2256` | Target resolution, session lease, approval including CLI, revocation/revalidation, and response wrapping. Preserve and recheck across any new asynchronous work. |
| S14 | `maho-chromium/browser/maho_browser_main_extra_parts.cc:1790-1796`, `maho-chromium/browser/maho_browser_main_extra_parts.cc:3014-3032`, `maho-chromium/browser/ai/maho_browser_tool_executor.cc:1477-1510`, `maho-chromium/browser/ai/maho_browser_tool_executor.cc:1545-1595` | Approval metadata, target token revalidation, and agent broker authorization. Extend consent with file/receiver context; do not bypass these mechanisms. |
| S15 | `maho/crates/maho-cli/src/browser.rs:23-66`, `maho/crates/maho-cli/src/browser.rs:276-283`, `maho/crates/maho-cli/src/browser.rs:323-346`, `maho/crates/maho-browser-mcp/src/client.rs:379-444` | CLI timeout and reconnect behavior versus the no-replay `call_tool` method. Route upload through no-replay transport and test ambiguous failures. |
| S16 | `maho/crates/maho-cli/src/main.rs:4958-4995`, `maho/crates/maho-cli/src/main.rs:5029-5045`, `maho/crates/maho-cli/src/main.rs:5099-5104` | REPL history, trimming, parsing, and explicit lease-required retry. Document path grammar and preserve safe pre-dispatch retry only. |
| S17 | `maho-chromium/browser/mcp/maho_mcp_session_unittest.cc:678-683`, `maho-chromium/browser/mcp/maho_mcp_session_unittest.cc:1367-1381`, `maho-chromium/browser/mcp/maho_mcp_session_unittest.cc:3420-3457` | Mock upload dispatch, approval denial, required path, tab resolution, success/failure. Extend; these are not real filesystem tests. |
| S18 | `maho-chromium/browser/ai/maho_browser_tool_registry_unittest.cc:244-251`, `maho-chromium/browser/ai/maho_browser_tool_registry_unittest.cc:404-409`, `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:823-848` | Approval/schema assertions and metadata-only `.png` test. Add real executor-dispatch assertions and catalog parity. |
| S19 | `maho/crates/maho-cli/tests/repl_test.rs:8-78`, `maho/crates/maho-cli/tests/repl_test.rs:124-143`, `maho/crates/maho-cli/tests/tool_test.rs:29-54` | Existing mock server, REPL help checks, and offline CLI harness. Add request-argument capture and upload-specific tests. |
| S20 | `maho/crates/maho-agent/src/browser_action_contract.rs:113-125`, `maho/crates/maho-agent/src/browser_action_contract.rs:279-293`, `maho/crates/maho-agent/src/fallback_ladder.rs:192-193`, `maho/crates/maho-agent/src/fallback_ladder_browser.rs:63-69` | Rust approval invariant and fallback classification. Preserve upload behavior; deliberately classify and test a future drop tool. |
| S21 | `maho-chromium/build/scripts/apply_chromium_src_overrides.py:5628-5653`, `maho-chromium/build/scripts/test_apply_chromium_src_overrides.py:1-5` | Existing `browser.cc` replacement machinery and regression-test module. Add a reproducible chooser hook if its owning recipe is indeed absent. |
| S22 | `chromium/src/chrome/browser/ui/browser.cc:2757-2779` | Local upstream declaration and `Browser::RunFileChooser` hook. Reference only; never maintain a direct hand-edit here as the fix. |
| S23 | `chromium/src/content/browser/web_contents/file_chooser_impl.cc:30-49`, `chromium/src/content/browser/web_contents/file_chooser_impl.cc:80-113`, `chromium/src/content/browser/web_contents/file_chooser_impl.cc:224-268` | Folder-only symlink filtering, listener lifetime, renderer file-read grants, and result callback. Preserve native delivery and understand its limits. |
| S24 | `chromium/src/content/public/browser/render_widget_host.h:303-344`, `chromium/src/content/browser/renderer_host/render_widget_host_impl.cc:1945-2060`, `chromium/src/content/browser/renderer_host/render_widget_host_impl.cc:3651-3656` | Native drag APIs, filtering, and file-access preparation. Basis for the proposed drop backend. |
| S25 | `chromium/src/content/browser/devtools/protocol/input_handler.cc:390-418`, `chromium/src/content/browser/devtools/protocol/input_handler.cc:1432-1546` | Chromium's protocol-to-file-drop conversion, file-access gate, widget hit-testing, acknowledgements, and cleanup. Architectural reference, not a new public debugging endpoint. |
| S26 | `maho-chromium/browser/BUILD.gn:396-446`, `maho-chromium/browser/mcp/BUILD.gn:123-149`, `maho/scripts/build_lock.py:174-188` | Existing narrow test binaries and shared build-lock command wrapper. Reuse; obtain approval before any required build-graph changes. |

## 4. Current execution flow

### 4.1 Opening and completing a chooser

```text
Page opens a file chooser
  -> Browser::RunFileChooser in the local Chromium tree
  -> MahoBeginAutomationFileChooser
       no active automation lease: preserve normal native chooser
       active lease: retain listener + mode on this WebContents

CLI REPL: upload <path>
  -> mutate -> browser::send_tool_call -> BrowserClient tools/call
  -> tool/capability lookup and MCP target/lease/approval gates
  -> MahoMcpSession upload branch
  -> MahoMcpBrowserActionHandler::HandleFileUpload
  -> production SelectFileForPendingChooser
       require WebContents, absolute path, no parent reference
  -> MahoCompleteAutomationFileChooser
  -> FileSelectListener::FileSelected(one NativeFileInfo)
  -> Chromium renderer file-access grant and chooser callback
  -> page receives a selected file; page controls any network upload
```

The lease needs to exist **when the page opens the chooser**, not merely when `upload` runs. A native dialog already opened outside interception is not converted into a pending automation chooser by a later upload command. Completion does not find a DOM input, click an upload button, or create a chooser on demand. These properties follow from S8, S9, and S22.

The current pending record holds only the listener and chooser mode. A new pending chooser cancels the previous listener. Completion removes the record before calling `FileSelected`, so repeated completion cannot reuse that same record. The handoff contains the full native path and a basename display name; it does not load the whole file into JSON (S8).

### 4.2 Desktop-agent path is incomplete

The capability catalog advertises `DesktopAgentAndBrowserMcp`, and the desktop executor resolves the target and asks for approval. Its final action dispatch chain, however, omits `browser_file_upload_select` and returns the registered-but-unimplemented error. The `.png` executor test checks approval metadata only; it never asserts successful selection. This is a concrete implementation gap, not an extension-validation failure (S10, S12, S18).

### 4.3 Public success is selection, not upload completion

`HandleFileUpload` returns `file_selected: true` when the boolean delegate returns true. Chromium's listener can return immediately when its weak owner is gone; the boolean Maho helper has no renderer acknowledgement. Chromium's eventual `FileSelected` grants file-read permission and returns chooser data; it does not report HTTP upload success (S5, S8, S23).

Keep selection and upload verification distinct. A portal may validate the extension, inspect the package, reject its contents, delay a network request, or require another explicit action. These are application outcomes to observe independently, not reasons to hardcode a Google Play restriction into the generic tool.

## 5. Detailed review findings

### R1. AAB-only language is stale; there is no live suffix guard to remove

The CLI help and missing-argument message, live schema description, interface comment, and snapshot still refer to AAB. Neither the CLI upload branch, shared handler, nor production delegate enforces that type. The capability catalog description is already generic (S1, S3-S6, S9-S10).

**Modification:** Generalize the remaining wording and add extension-neutral regression tests. Before modifying a release branch, repeat the suffix-check search and record the actual binary/build version involved in the reported failure. Where an older implementation really contains a suffix guard, remove only that guard, retaining every independent path and authorization check. Do not replace it with an enumerated list of `.msix`, `.dmg`, and other currently requested types: unknown extensions and extensionless files are part of this proposal.

### R2. Packaged discovery disagrees with the live request contract

Offline discovery uses the checked-in snapshot when connection fails. Its upload schema has `additionalProperties: false`, requires `file_path`, and describes an AAB. The live schema, CLI, handler, and schema unit test require `path`. There is no `file_path` compatibility mapping in the inspected upload handler (S1-S5, S18).

**Modification:** Correct the snapshot to `path`, not the live API to `file_path`. Synchronize its description and required/property sets with the canonical public projection. Regenerate from the established catalog process when available; otherwise make a focused update and add a deterministic parity test. Do not invent an alias merely to preserve an already-invalid snapshot contract. A migration note should tell clients that followed the old snapshot to use `path`.

### R3. Desktop upload dispatch is absent

After existing authorization, add an upload branch that calls the same shared upload implementation used by MCP. Keep both success serialization and failure serialization consistent with other desktop actions. Do not implement a second file-validation algorithm in the executor. Add fake-delegate counters and verify that an approved upload actually calls `SelectFileForPendingChooser` once with the correct tab and path, and returns the selection result (S5, S12, S18).

### R4. “Keep file existence checks” requires adding them at this baseline

The observed production boundary validates path shape only. The downstream native listener grants read permission for supplied native paths without an explicit existence/readability check in the inspected method. Thus it is inaccurate to describe existence, regular-file, or readability validation as already guaranteed (S9, S23).

**Modification:** Introduce a shared browser-side validator before native delivery. It must verify an existing readable regular file, not just a non-directory path. Keep `IsAbsolute()` and `!ReferencesParent()` before normalization, reject embedded NUL, and define symlink/reparse behavior explicitly. File I/O must not be added synchronously to the browser UI sequence. Sections 7 and 8 define the asynchronous boundary and its residual race limitations.

### R5. Pending chooser lifetime and mode handling need hardening

The pending record is tab-owned, but does not record initiating frame/document, origin, lease owner or generation, creation deadline, or an operation identity. Interception stores any supplied mode. Completion always supplies one file and an empty base directory (S8).

The folder case deserves explicit rejection or a dedicated implementation: Chromium's folder path invokes `RemoveSymlinks` with a nonempty-base-directory precondition. Feeding the current one-file/empty-base-dir completion into that mode is not valid folder support. Similarly, a listener whose owner has gone away can produce a misleading local success (S23).

**Modification:** Support ordinary open/open-multiple chooser modes with one selected file; do not advertise folders or save dialogs. Bind pending state to the requesting document and lease generation, cancel invalidated state exactly once, and distinguish “no pending chooser” from “invalid file” and “stale chooser.” Navigation, frame deletion, tab destruction, controller revocation, and lease transfer need explicit tests and cancellation behavior.

### R6. The local chooser hook is not yet reproducibility evidence

The local upstream `Browser::RunFileChooser` contains the hook. Its Maho-side comment attributes it to `apply_chromium_src_overrides.py`. Searches of the inspected build scripts found no `FileChooser`/`MahoBeginAutomationFileChooser` implementation recipe, although that script has other `browser.cc` replacements (S8, S21-S22).

**Modification:** Identify and document the hook's real owning recipe. If none exists, add an idempotent, anchored overlay replacement and a fixture test proving clean application, second application without duplication, and loud failure on unsupported upstream drift. Use the existing replacement framework. Do not fix this solely by editing `chromium/src/chrome/browser/ui/browser.cc`. This finding is scoped to the inspected recipe set; a different distribution pipeline has not been audited.

### R7. CLI transport can replay a mutation after a disconnect

`mutate` explicitly retries a lease-required rejection, which is intended to occur before dispatch. Separately, `send_tool_call` chooses `send_tool_call_with(..., true)`, which calls `BrowserClient::call_with_timeout`. That method reconnects and retries qualifying transport failures without checking whether the request was a mutation. In contrast, `BrowserClient::call_tool` deliberately uses no-replay transport (S15-S16).

**Modification:** Route upload and future drop through no-replay transport, retaining their existing timeout selection and result projection. Do not broadly refactor every CLI action in this patch. Keep a bounded lease-required retry only when the server has definitively rejected the original request before side effects; prefer structured error classification over substring matching when touching this code. After ambiguous disconnection, report an unknown outcome and require observation, not automatic resubmission. Also test the currently short default timeout against approval latency; simply increasing the timeout does not solve cancellation or replay.

## 6. Target contract and user-visible behavior

### 6.1 Chooser selection remains backward compatible

| Contract element | Proposed behavior |
| --- | --- |
| MCP name | Keep `browser_file_upload_select`. |
| Canonical capability | Keep `input.file_upload_select`. |
| Required argument | Keep one nonempty string `path`. |
| Optional target | Keep `tab_id` with existing omitted/zero active-tab semantics, then bind to the resolved concrete target. |
| File type | Any permitted readable regular local file, regardless of suffix, case, or absence of suffix. |
| Prerequisite | The target page has opened a supported chooser while the automation lease is active. |
| Success | Preserve `file_selected: true`; document that it is not upload completion. |
| Security | Preserve sensitive classification, explicit filesystem-boundary approval, lease ownership, target validity, policy checks, and output wrapping. |
| Out of scope | Directories, recursive upload, save dialogs, multiple-file API changes, automatic form submission, file copying from a remote client, and file execution. |

“Local” means local to the **browser host**, not the machine running a remote MCP client. Do not expand `~`, environment variables, globs, URLs, or relative paths implicitly. A filename extension is neither a security boundary nor evidence that a web service will accept the file.

Use the following text consistently:

```text
REPL help:
  upload <absolute-path>  Select a local file for the active pending chooser

Missing-argument usage:
  Usage: upload <absolute-path>

Schema path description:
  Absolute path to the local file to select.

Tool description:
  Select one local file for a pending file chooser. The page must already
  have opened the chooser. Selection does not confirm upload completion.

Interface comment:
  Supplies one local file to a chooser the target page has already opened.
  Production validates the path and pending chooser at the browser boundary.
```

Descriptions alone do not require a protocol rename or version bump. Correcting the snapshot restores the existing live contract. Any later additive error fields or asynchronous interface changes need their own serialization/compatibility tests rather than an unreviewed global result-version change.

### 6.2 CLI grammar and examples

`upload` is a command inside `maho browser repl` in the reviewed implementation; it is not a verified standalone `maho upload` subcommand. The current REPL trims the whole input line, then forwards the remainder after `upload` without shell parsing. Interior spaces work as raw path characters, but surrounding quotes are not removed and trailing filename whitespace cannot be faithfully expressed through this grammar (S1, S16).

Example REPL input, after opening a chooser under the session's lease:

```text
upload /absolute/path/releases/My App.msix
```

Example MCP request:

```json
{
  "jsonrpc": "2.0",
  "id": 101,
  "method": "tools/call",
  "params": {
    "name": "browser_file_upload_select",
    "arguments": {
      "tab_id": 7,
      "path": "/absolute/path/releases/My App.msix"
    }
  }
}
```

Do not add shell tokenization incidentally: it can change existing filenames containing quotes or backslashes. A quoted/escaped REPL grammar is a separate compatibility decision; direct structured tool calls remain the unambiguous path representation. Windows paths require JSON escaping when represented in JSON, but must be validated according to the browser host's native path rules rather than a POSIX-only prefix test.

## 7. Step-by-step modification plan

### Step 1 — Capture failing cases and distinguish deployed behavior

Record the target browser/CLI build identifiers and repeat the focused source search. Create a local upload fixture with a normal file input and harmless temporary files named `.aab`, `.msix`, `.dmg`, `.zip`, `.png`, `.pdf`, `.ipa`, `.pkg`, an unknown extension, and no extension. The fixture must record selected filenames, byte sizes, and content checksums so the test can distinguish actual file data from a path string.

Add regression tests for the wrong snapshot property, missing desktop dispatch, and generic descriptions before changing those areas. Establish which failures are source-contract failures and which are real browser delivery failures. Do not use successful mock responses as proof that chooser interception is installed in a fresh browser build.

### Step 2 — Make the minimal compatibility correction

Change S1, S3, S4, and S6 wording; fix the snapshot's property and required list to `path`. Leave the already-generic description in S10 intact unless adopting the fuller prerequisite text consistently. Keep at least one `.aab` fixture as a compatibility case while adding non-AAB cases; do not indiscriminately delete every occurrence of `.aab` in the repository.

In S9, retain the current extension-neutral dispatch. Only remove a suffix comparison if it actually exists in the release branch being patched. Do not weaken the absolute-path, parent-reference, WebContents, approval, or lease guards. Add the missing desktop-executor branch from R3 and test both MCP and desktop-agent paths through the shared helper.

**Exit evidence:** live and packaged upload schemas agree; help is generic; both advertised surfaces dispatch the same path; AAB and non-AAB cases have identical authorization requirements.

### Step 3 — Introduce valid-file validation and typed completion

Extract a small, testable browser-side validation component rather than expanding the monolithic main-extra-parts class. Suggested new implementation files, **not existing files**, are `maho-chromium/browser/mcp/maho_upload_file_validator.h` and `.cc`, with a corresponding unit test. A chooser coordinator can likewise be extracted when lifecycle work justifies it. Any new GN sources/dependencies require normal build-graph review and the repository's required approval.

Define a typed result covering invalid path syntax, missing/unreadable/non-regular file, unsupported chooser mode, missing/stale chooser, denied authorization, and successful delivery. Replace or supplement the internal boolean interface with an asynchronous callback so filesystem checks run on a `MayBlock` worker and chooser delivery runs on the browser/UI sequence. Update the base delegate, production delegate, fake delegates, MCP deferred-response path, and desktop executor together; no public Rust FFI upload API is necessary.

The operation should proceed as follows:

1. Validate request types and lexical path constraints without filesystem access. Resolve and retain the exact target and pending chooser identity.
2. Enforce existing capability, lease, controller, and approval requirements. Bind consent to the requested file and receiving context instead of adding a second unrelated generic prompt. Unauthorized callers must not gain a filesystem-probing interface.
3. Validate the file on a worker. Check readable regular-file status and the chosen symlink/reparse policy without opening special files in a way that can block indefinitely.
4. Return to the UI sequence and revalidate the target, requesting document, current policy, controller, lease owner/generation, pending chooser identity, and deadline. A replacement chooser must not inherit the old result.
5. Deliver the validated native path through the existing Chromium listener mechanism exactly once. Consume the pending record only on successful delivery or deliberate cancellation, not on a correctable bad-path error.

Keep the public success field stable. For errors, retain `-32602` for malformed parameters, existing authorization error codes for denial, and `-32000` for execution failures. Proposed machine-readable reasons include `invalid_path`, `file_not_found`, `file_not_readable`, `not_regular_file`, `no_pending_file_chooser`, `unsupported_chooser_mode`, and `chooser_stale`. These reasons are **new design**, not existing output. Adding `error.data.reason` requires extending the shared result/error type and both serializers; do not document it as available until that change lands.

### Step 4 — Bind pending state and repair interception reproducibility

Store an operation/chooser ID, initiating frame and document identity, top-level and requesting-frame origins, supported mode, stable tab identity, lease owner/generation, and creation/deadline information with the pending listener. A live `RenderFrameHost` alone is not proof of the same document. Reuse existing target tokens, but do not assume a token that protects WebContents/profile identity also binds the initiating document.

Observe invalidation events and cancel once on document replacement, requesting-frame deletion, tab teardown, session revocation, or lease transfer/expiry. Preserve cancellation of superseded choosers. Ordinary open-multiple mode may receive one file; folder and save requests must not be routed through the single-file completion. Under automation, return/capture an explicit unsupported-mode outcome and cancel as appropriate rather than leaving the page indefinitely blocked. Preserve normal native behavior for unleased tabs.

Resolve R6 through the overlay recipe and its tests. Verify the hook against a fresh fixture and a twice-applied fixture, then build the actual affected browser target through the repository wrapper. If the hook already belongs to another maintained recipe, fix the misleading ownership comment and add coverage there instead of installing it twice.

### Step 5 — Fix upload transport and document operation outcomes

Select no-reconnect/replay transport for upload in the CLI helper while keeping result projection intact. Preserve only an explicit, bounded retry for a definitive pre-dispatch lease rejection. Add disconnect-after-send tests showing that an already-dispatched selection is not sent again. Treat timeout after possible dispatch as an indeterminate result; observation is required before another attempt.

Align request deadlines, approval UI latency, and server cancellation. The current upload command falls into the CLI's short default timeout rather than its slow-input list (S15). A future asynchronous implementation must ensure an expired request cannot later complete a different chooser. Update user guidance to distinguish `file_selected`, page acknowledgement, and confirmed server upload. Test filenames with spaces and Unicode without broadening the CLI grammar accidentally.

### Step 6 — Release chooser generalization independently of drop injection

Land coherent patchsets: discovery/copy and agent dispatch; validation/lifecycle/transport; reproducible browser delivery and end-to-end evidence. Do not advertise full support merely because the descriptive strings are fixed. The release checklist in section 11 defines the required evidence. The drop feature in section 10 is separately gated and must not delay or weaken these fixes.

## 8. Security and operational decisions

### 8.1 Path policy

Keep the original spelling checks before canonicalization: otherwise an absolute path containing `..` can be normalized into an apparently acceptable one. Reject empty strings, embedded NUL, relative paths, drive-relative Windows paths, file URLs, and invalid host-native encodings. The string-based protocol cannot promise lossless support for every non-UTF-8 POSIX filename; document that limitation rather than using lossy conversion to select a different file.

Require an existing, readable **regular file**. `PathExists()` alone is insufficient, and “not a directory” is insufficient for FIFOs, sockets, devices, and other special files. Use appropriate platform file/handle metadata and safe open behavior. Do not read the entire file merely to validate it, and do not execute or mount packages while checking them.

Make symlinks and Windows reparse points a deliberate policy decision. Recommended compatibility policy: allow a stable link to a permitted regular file only when the resolved target remains within the authorized file scope; display or otherwise bind that target in approval. Recheck retargeting before delivery and fail on ambiguity. Do not blanket-reject every symlinked parent without evaluating common host layouts, including temporary-directory aliases. A stricter no-links policy is acceptable only when explicitly documented and tested.

Treat UNC/network shares, Windows device namespaces, alternate data streams, and nonlocal mounted paths explicitly. A reasonable initial policy rejects network/device namespaces unless separately authorized. Extension neutrality does not require accepting every filesystem namespace. Do not implement a broad extension allowlist as a substitute for these decisions.

### 8.2 Consent, origins, and leases

The contract already marks file selection as sensitive and requiring approval and a lease. The MCP approval gate applies to internal CLI callers too. Preserve those properties. Existing target resolution has controller-specific behavior, and the current approval metadata is not file-specific: the production prompt receives a tool name and browser/tab identifiers; desktop metadata includes origins but not the selected file (S10-S14).

A stronger approval should identify the exact file, the top-level page, and the actual receiving frame when different. Treat page text, suggested filenames, and DOM locators as untrusted inputs, not authorization. Do not infer approval for a secret file from a prompt embedded in a webpage. Do not globally exempt arbitrary files from approval to make automation easier.

Authorize the operation before exposing detailed filesystem metadata. For the conservative implementation, approve the exact requested path and receiver before file I/O; if resolution changes the meaning of that consent, reject or request revised consent. If a richer UI needs a metadata preflight, that read requires an explicit authorized scope and must not become an unauthenticated existence oracle. Denied consent must always prevent selection and renderer access grants.

Recheck the lease after asynchronous work and modal approval. Capturing a lease once is not sufficient if another controller takes ownership while a prompt or worker runs. Keep the existing origin/policy contract and broker behavior, but add receiving-document checks where required; declared `ActiveTabOrigin` metadata by itself is not proof of requesting-frame origin enforcement in every path.

### 8.3 File identity and time-of-check/time-of-use

Checking existence and canonicalizing a path does not eliminate time-of-check/time-of-use races. The file can disappear or change after validation, and Chromium's current handoff is still path-based. Retain useful file identity/metadata through validation and recheck before delivery, but do not claim that this creates immutable content or pins every later renderer read.

If product policy requires approval of exact immutable bytes, investigate a broker-owned immutable snapshot or a Chromium-compatible handle/blob-backed design separately. That would introduce disk usage, cleanup, naming, and potentially large-copy costs. Do not silently copy multi-gigabyte installers or describe a validation-only patch as solving this stronger problem.

### 8.4 Page filters, resources, and privacy

The current Maho hook receives mode, not the complete chooser parameters; its pending record does not retain `accept_types` (S8, S22). Preserve page/server validation behavior without claiming this helper enforces those constraints. The tool should not rewrite filenames, fake MIME types, or bypass application checks to make an incompatible upload look successful. Future exposure of chooser constraints may assist planning, but is not a new global file-type allowlist.

Use Chromium's native file delivery, not base64 file contents in tool arguments or renderer-injected JavaScript. Bound concurrent requests and deadlines, and keep metadata work off the UI sequence. Large-file behavior should not require memory proportional to file size. Clear pending state and native resources on every terminal path.

Avoid returning absolute paths, raw file contents, or sensitive filenames in telemetry and generic errors. User-facing consent may show the file, but general receipts can use an operation ID, minimal file metadata, and outcome. Account for the CLI's persisted REPL history, which currently stores command lines (S16); disclose it and consider a separately tested sensitive-command history policy. Do not globally disable security logging to hide path leakage.

## 9. Testing and verification plan

### 9.1 Coverage required before release

| Layer | Cases to add or extend | Required assertion |
| --- | --- | --- |
| Registry/catalog | Live and packaged upload schemas, required/property sets, generic descriptions, policy metadata. | Both require `path`; no AAB-only contract language; approval and lease remain required. |
| CLI REPL | Empty path; each requested extension; unknown/extensionless/uppercase suffix; spaces, Unicode, quote/backslash behavior. | Capture actual tool name and arguments, not just an `ok` response; path is not renamed or truncated. |
| CLI transport | Lease-required rejection; disconnect before response; approval delay; cancellation and timeout. | Only a definitive pre-dispatch lease rejection can retry; ambiguous mutation outcomes never replay automatically. |
| Shared handler/MCP | Missing, empty, and wrong-type path; omitted/explicit/zero tab; delegate success/failure. | Existing error codes and `file_selected` shape remain compatible; correct concrete target is used. |
| Desktop executor | Approved valid file; denied approval; missing delegate; invalid target; delegate failure. | The new branch calls the upload delegate exactly once and verifies its result, not merely approval metadata. |
| File validator | Real temporary `.aab`, `.msix`, `.dmg`, `.zip`, `.png`, `.pdf`, `.ipa`, `.pkg`, unknown and extensionless files; zero bytes and large sparse fixture. | Regular readable files are independent of extension and size does not cause whole-file buffering. |
| Invalid paths | Relative/traversal, nonexistent/unreadable, directory, FIFO/socket/device, NUL, file URL, platform-specific invalid/drive-relative/namespace paths. | No native selection or renderer file grant; no blocking special-file open; actionable authorized error. |
| Links/races | Allowed stable link, disallowed target, link retargeting, file removal/replacement between validation and completion. | Chosen policy is explicit and consistent; changed identity/context is not silently accepted. |
| Chooser lifecycle | No chooser; second completion; superseded chooser; invalid path then corrected path; unsupported folder/save mode. | Single completion/cancellation; invalid path does not consume a valid pending chooser; unsupported modes cannot enter folder completion accidentally. |
| Authorization/lifetime | Denial, revocation, lease expiry/steal, navigation, requesting-frame removal, tab/profile switch during consent or worker work. | Zero delivery to the replacement context; correct listener cleanup; denied sessions cannot inspect filesystem details. |
| Native interception | Unleased native path; leased interception; fresh overlay application; twice-applied recipe. | Normal native behavior remains; automation hook is installed reproducibly without duplicate calls. |
| Browser integration | File input with and without `accept`; change events; page-triggered asynchronous upload; server accepts/rejects. | Actual selected bytes/name/size verified separately from server upload outcome. |

Use separate filesystem and browser tests: S17's fake delegate never opens a file. Add focused validator tests and a browser test fixture for the actual production chooser coordinator/listener. Suggested new browser test filename: `maho-chromium/browser/mcp/maho_file_upload_browsertest.cc` (proposed, not currently present). Register it through a reviewed existing test target or an approved narrow target; do not claim a new test runs until its build linkage is verified.

For permission-denial cases, assert no selection and no renderer grant. Test whether metadata reads are permitted by the chosen preflight policy rather than assuming an approval callback alone proves all I/O ordering. For readability tests, use a genuinely restricted execution context or controlled file-open failure; permission-bit tests can be misleading under elevated privileges.

### 9.2 Browser fixture and environment

Use an isolated local test page with: a plain input, an input with a restrictive `accept`, a multiple-file input, a folder request, a disabled/removed input, a nested-frame input, and a separate dropzone for the later feature. The page records events and file metadata; an isolated test endpoint verifies received bytes. No production account or real package publication is necessary.

Acquire the automation lease before triggering the chooser using the supported browser input path. Verify the intended tab/document and the `input.files` result, then separately observe a fixture upload. Include active-tab changes and navigation during approval to reproduce context races. Confirm that a rejected portal upload is not reported as completed by the file-selection tool.

Follow repository isolation rules: host-side builds use the shared lock-backed wrappers; macOS live browser/GUI evidence runs in the Tart VM through the approved Maho CLI workflow, not host UI automation. Use Windows and Linux coverage for native path and special-file differences. Keep production credentials, real private files, and unrelated profiles out of fixtures.

### 9.3 Planned commands and test targets

The runtime build and Rust commands below are **implementation-phase commands, not commands executed to validate this documentation-only change**. The source-only overlay recipe `unittest` command was also run during this review; its result is recorded in section 12. Run implementation checks from the workspace root, after changes and approved build-graph updates. Use the existing wrappers; do not bypass build locks, delete a lock, or rewrite GN arguments to force a failing build.

```sh
python3 maho/scripts/build_lock.py -- cargo test --manifest-path maho/Cargo.toml -p maho-cli --test repl_test --test tool_test
python3 maho/scripts/build_lock.py -- cargo test --manifest-path maho/Cargo.toml -p maho-browser-mcp --lib
python3 maho/scripts/build_lock.py -- cargo test --manifest-path maho/Cargo.toml -p maho-agent browser_action_contract

python3 -m unittest discover -s maho-chromium/build/scripts -p test_apply_chromium_src_overrides.py

python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_browser_tool_executor_test
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_mcp_session_test
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_mcp_lease_registry_test
```

The named C++ test binaries exist in S26. Execute the produced binaries through the permitted test environment and select `MahoBrowserToolRegistryTest.*`, `MahoBrowserToolExecutorTest.*`, and the relevant `MahoMcpSessionTest.*` cases, then run the complete affected suites. An executor-only build does not prove that edits to the production main-extra-parts delegate compile: build the affected production target and the new chooser browser-test target as well, through `build_maho.py`. The new test target name must come from its approved build registration, not from this proposal.

The REPL and tool integration harnesses currently use Unix socket imports (S19). Do not claim those particular test binaries provide Windows transport coverage. Add platform-appropriate Windows tests or isolate portable request/parser tests, and explicitly report any environment-limited browser runs.

## 10. Bonus proposal: `browser_file_drop`

### 10.1 Goal and separation from chooser upload

A JavaScript dropzone may never create a native file chooser. `browser_file_upload_select` cannot complete a nonexistent pending listener. Introduce `browser_file_drop` with canonical capability **`input.file_drop`** for this case, rather than changing the existing tool's meaning.

The first version should handle **one validated local regular file** and one explicit target. Multiple files, folders, clipboard injection, OS-level dragging, and automatic submission are separate extensions. Keep this feature unadvertised or explicitly feature-gated until both MCP and desktop-agent dispatch, security controls, and browser tests are complete.

### 10.2 Proposed request and result

Proposed arguments: required `path`, required `target`, optional `tab_id`, and bounded optional `timeout_ms` (suggested default 10,000 ms, range 100-30,000 ms). The timeout is for validation and event delivery, not for completion of the site's upload. Use strict object schemas with `additionalProperties: false` and an exclusive `oneOf` for target shapes:

| Target shape | Meaning |
| --- | --- |
| `{"x": 320.5, "y": 240}` | A point in the top-level viewport's CSS-pixel coordinate system; finite and within the current viewport. |
| `{"ref": 12}` | A current, target-bound accessibility reference; not a DOM ID or an arbitrary numeric node identifier. |
| `{"selector": "#release-dropzone"}` | A CSS selector resolved by trusted browser code; must identify one visible, eligible element. It is not JavaScript. |

Require `x` and `y` together and reject mixtures such as `ref` plus coordinates. A selector with zero or multiple matches must fail clearly rather than silently selecting an arbitrary element. Define locator freshness and visibility checks explicitly. Initially restrict element resolution and receiving-frame support to contexts whose document identity can be proven; do not treat a shared renderer widget as proof that the receiver is the top-level document.

Example proposed tool arguments:

```json
{
  "tab_id": 7,
  "path": "/absolute/path/assets/screenshot.png",
  "target": {"selector": "#release-dropzone"},
  "timeout_ms": 10000
}
```

Example result after confirmed renderer event delivery:

```json
{
  "dispatched": true,
  "renderer_acknowledged": true,
  "negotiated_operation": "copy",
  "upload_status": "not_verified"
}
```

Do not call the result `uploaded: true`. A renderer acknowledgement and a negotiated copy operation do not establish server acceptance. If the outcome becomes ambiguous after dispatch, report that uncertainty without automatically repeating the drop. Proposed failures include `target_not_found`, `target_ambiguous`, `target_stale`, `target_not_visible`, `unsupported_frame_target`, `drop_not_accepted`, and `drop_timeout`, in addition to the shared path/authorization failures. These fields and reasons are a proposed new contract.

### 10.3 Feasibility and preferred backend

**Feasible, but not a schema-only change.** The inspected Chromium exposes `RenderWidgetHost::DragTargetDragEnter`, `DragTargetDragOver`, `DragTargetDrop`, leave/cleanup operations, and callbacks. Its implementation prepares file access when the actual drop is delivered. The protocol input handler shows how file paths become `DropData::filenames` and how widget hit-testing and event sequencing work (S24-S25).

Prefer a narrow C++ browser-side drop coordinator behind a new asynchronous delegate method, using the same validator and policy context as chooser selection. Suggested new files, **not existing files**, are `maho-chromium/browser/mcp/maho_file_drop_coordinator.h` and `.cc`. Avoid leaking Chromium implementation details into Rust or duplicating the security validator in the CLI.

Recommended operation sequence:

1. Validate the request; resolve a stable tab/document and target. Resolve selector/ref targets to a visible hit-testable point. Bind policy and consent to the actual receiving context and file.
2. Perform shared valid-file checks on a worker. On return, recheck controller, lease generation, policy, file scope, target freshness, and deadline. Reject changed or unsupported receivers.
3. Construct browser-owned `content::DropData` with the validated native filename and basename display name. Do not accept caller-controlled file bytes, fake MIME data, origin flags, or arbitrary text/HTML drag items.
4. Resolve the target widget with Chromium's input hit-testing and coordinate transforms. Convert CSS coordinates to the correct widget/client and screen spaces; do not multiply by device scale blindly or reuse screenshot pixel coordinates. Account for zoom, scrolling, nested frame transforms, and display scale.
5. Apply the browser's drop-data filtering so the routing identity matches the target. Preserve `FilterDropData` and Chromium's file-access preparation: `GrantFileAccessFromDropData` checks routing identity, and `DragTargetDrop` performs the grant (S24). Do not manually grant broad filesystem access.
6. Send enter, await its callback, send over, and await the negotiated operation. Use copy only; never request move/delete semantics. Reject an unaccepted operation and prevent default file navigation or a drop onto browser chrome. A page handler and a native file input may report different handling metadata, so test the accepted target classes rather than equating one boolean with upload success.
7. Recheck the operation context immediately before drop; deliver once, await its completion callback, and finish the appropriate drag leave/source/system cleanup on success or cancellation. Keep weak references and a watchdog because host methods need not produce a callback after their widget disappears.
8. Return event-delivery evidence. The caller can separately observe page state or a fixture network receipt to verify upload completion. Never retry automatically after the irreversible drop point.

The protocol implementation sometimes reports success without exposing the negotiated operation, and its drop path includes additional cleanup (S25). Use it as a version-specific reference, not proof that a single successful protocol response means the site accepted the file. A native coordinator should retain the available callback information and test its lifecycle explicitly.

### 10.4 Frames and target safety

Out-of-process iframes, same-process child frames, overlays, element movement, and navigation make target binding the main complexity beyond file validation. Browser-side widget hit-testing does not, by itself, prove that the same-origin consent context is the eventual receiving document.

For an initial implementation, reject cross-origin or otherwise unresolvable receiving-frame targets with `unsupported_frame_target`; do not silently send to the top-level widget. Add broader frame support only with frame/document identity, origin policy, coordinate transformation, and navigation-race tests. A stable tab ID is necessary but insufficient. Re-resolving a selector after approval must not silently approve a different element or receiver.

### 10.5 Alternatives and rejected shortcuts

**CDP alternative:** The official Input protocol documents experimental `Input.dispatchDragEvent`, file entries in `DragData`, and main-frame-viewport CSS coordinates. It can support a guarded prototype, but the local Chromium implementation checks `allow_file_access_` before file-related dispatch. Preserve that gate and Maho authorization; do not expose a new remote-debugging port or globally enable file access just to implement this feature. The official protocol is tip-of-tree documentation, so the checked-out implementation remains the compatibility reference. See U1 and S25.

**Hidden file input alternative:** Official `DOM.setFileInputFiles` selects files for an actual input node using its node/object identity. It is not a general dropzone operation and should not be a silent fallback that changes the user's intended interaction. See U2.

**Rejected:** Assigning a local path string to a page input as though it were a file; constructing `new File([path], ...)`, which would supply path-text bytes rather than local file contents; shipping large base64 payloads through generic evaluation; weakening renderer security; or adding OS-level drag automation. The MCP session explicitly rejects `evaluate_js` at `maho-chromium/browser/mcp/maho_mcp_session.cc:1836-1841`; this proposal does not reopen that boundary.

### 10.6 Required integration changes

Add the new descriptor and strict schema to the capability catalog and registry, with browser/tab scope, sensitive classification, required approval, required lease, explicit origin policy, and fail-closed behavior. Add `input.file_drop` to `CapabilityLeavesBrowserBoundary` and the Rust approval invariant; otherwise a generic input classification could omit the filesystem boundary. Update exact-set/catalog tests and packaged discovery only when the feature is genuinely available (S10-S11, S18, S20).

Add MCP target-tool and lease-mutation classification alongside upload (`maho-chromium/browser/mcp/maho_mcp_session.cc:845-845` and `maho-chromium/browser/mcp/maho_mcp_session.cc:908-924` are existing anchors), stale-reference protection for the ref variant, asynchronous handler/serialization, production delegate support, and an explicit desktop-executor branch. Update fallback classification deliberately; a file drop must not escape approval by being treated as ordinary page reading or generic DOM typing.

Reuse the validator, bound-consent metadata, cancellation, and no-replay transport from chooser hardening. Optionally add a CLI REPL `drop` command later, but avoid an ad hoc argument grammar for mixed file paths and coordinates; structured tool arguments are sufficient for the first release. Do not change the meaning of `upload` or trigger a drop automatically when no chooser is pending.

### 10.7 Drop-specific verification and release gate

Use local HTML fixtures for a real drop handler, a rejecting handler, a native file input, and a no-handler area that would otherwise navigate. Verify drag-enter/over/drop ordering, the actual file bytes/name/size, copy-only behavior, renderer acknowledgements, and independent upload outcomes. Include DOM movement, a covering element, stale refs, multiple selector matches, scroll/zoom/high-DPI coordinates, frames, permission denial, file disappearance, widget teardown, callback timeout, cancellation, and disconnect after dispatch.

The first release is blocked on demonstrated file-permission scoping, actual receiver binding, correct coordinates and event sequence, and no-replay/cleanup behavior. This is a design gate for the optional feature, not a blocker to delivering this review document or fixing chooser discovery.

## 11. Rollout, acceptance, and rollback

Chooser generalization is ready to advertise when all of the following are evidenced: generic and consistent discovery/help; `path` schema parity; successful MCP and desktop-agent dispatch; extension-independent real-file selection; retained approval and lease enforcement; valid-file and supported-mode rejection; stale-context cancellation; a reproducible chooser hook; and no silent mutation replay. Verify at least one AAB regression and several non-AAB files through the real browser fixture, with page/server outcomes reported separately.

Keep the optional drop tool behind an explicit feature boundary until its additional tests pass. Record support by platform and target type rather than promising all websites work. A chooser-only release must clearly state that drop-only zones are not covered.

Rollback should disable or revert the new drop capability independently. If a new validator or lifecycle rule causes compatibility problems, disable the affected new behavior through a reviewed release mechanism while retaining approval, lease, absolute-path, and parent-reference safeguards. Do not “roll back” by disabling security gates or reinstating a misleading AAB-only description. Never execute or publish an uploaded package as part of file-selection verification.

## 12. Verification record for this review deliverable

This change delivers the Markdown review and plan only. Source inspection established the current extension-neutral delegate, stale text, snapshot field mismatch, missing desktop dispatch branch, pending-state limitations, local-hook/recipe discrepancy, transport replay path, and native drag integration points. Proposed helper files, structured errors, browser tests, and drop APIs have not been implemented by this task.

Seven document/source checks passed using Python's standard-library `unittest`: artifact and required sections, balanced Markdown fences, three parseable JSON examples, validity of 61 source-path/line-range references, critical baseline source assertions, separation of existing and proposed behavior, and text/newline integrity. The baseline assertions independently checked the extension-neutral delegate, wrong snapshot property, and absent desktop dispatch branch.

The existing source-only overlay recipe suite was run with `python3 -m unittest discover -s maho-chromium/build/scripts -p test_apply_chromium_src_overrides.py`: **38 tests passed**. These tests do not include the proposed chooser-hook regression and do not prove clean-build chooser installation. The default environment had no `pytest`; the standard-library runner completed the suite without installing dependencies. A temporary environment created while checking runner availability was removed.

Recheck the final document and targeted diff after any edit. Runtime builds, filesystem security tests, browser E2E runs, and live portal uploads are **not** claimed as completed by this review. Only this Markdown deliverable is changed by the task; unrelated workspace changes are preserved.

### Upstream API references

These are public Chromium protocol references, not public links to the proprietary Maho repository. Both were consulted on 2026-09-11. Local source references S23-S25 are the primary evidence for the inspected engine version.

- **U1:** Chrome DevTools Protocol, Input domain, `dispatchDragEvent` and `DragData`: `https://chromedevtools.github.io/devtools-protocol/tot/Input/`.
- **U2:** Chrome DevTools Protocol, DOM domain, `setFileInputFiles`: `https://chromedevtools.github.io/devtools-protocol/tot/DOM/#method-setFileInputFiles`.
