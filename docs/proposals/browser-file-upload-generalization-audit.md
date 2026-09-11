# Arbitrary-file upload implementation audit

**Audit date:** 2026-09-11.  
**Delegation:** `0070b079-acb0-43bd-b7e4-2a75502721a3`, generation 2.  
**Source baseline:** HEAD `80b437e1342703d51142c963160fddec8a7b898e` plus the current working tree. The tree contains substantial pre-existing, unrelated changes.  
**Design reference:** `docs/proposals/browser-file-upload-generalization.md`, preserved as the historical generation-1 review and proposal.  
**Scope:** Independent review and verification, not implementation of the remaining architectural work.

## 1. Verdict

**The core extension-neutral feature changes are present and correct in the inspected sources. The complete architectural proposal is not implemented, and this audit does not give unconditional release approval.**

The REPL help/usage and upload schema are generalized. The packaged schema now requires `path`, matching the live registry and handler. The desktop executor now dispatches upload through the same handler as MCP. The production delegate retains the absolute-path and parent-reference guards and adds existence and non-directory checks before native completion. No AAB-only restriction was found in the reviewed execution path.

Fresh REPL tests and an independent request-capture check verify CLI dispatch for AAB and non-AAB paths, including spaces, Unicode, and no extension. The narrow C++ target built successfully, and seven focused upload/authorization/schema checks passed. These results do not prove that a real browser chooser delivered file bytes or that any portal accepted an upload.

The proposal deliberately required more than these changes. Synchronous filesystem access, missing readable-regular-file validation, pending-chooser lifecycle/mode binding, interception reproducibility, and reconnect/replay handling remain unresolved. Broader regression testing is also not green: the CLI snapshot omits `browser.visual_click`; C++ catalog tests expose related projection/count drift; and non-upload executor tests have an assertion failure and a crash. These findings must remain visible rather than being converted into an all-green implementation claim.

## 2. Delivered changes and source evidence

All locations below refer to the working tree inspected during this audit. Paths are workspace-relative.

| Area | Inspected evidence | Result |
| --- | --- | --- |
| REPL help | `maho/crates/maho-cli/src/main.rs:4935-4955` | Help says `upload <path>` and describes a local file for an active pending chooser. No AAB language. |
| REPL usage and dispatch | `maho/crates/maho-cli/src/main.rs:5438-5451` | Missing input prints `Usage: upload <absolute-path>`. A nonempty remainder is sent as `{"path": path}` to `browser_file_upload_select`. No suffix allowlist. |
| Packaged catalog | `maho/crates/maho-cli/src/tool_catalog_snapshot.json:563-595` | Generic description; required `path`; string property `path`; optional integer `tab_id`; `additionalProperties: false`. Existing capability/version/policy fields preserved. |
| Live registry | `maho-chromium/browser/ai/maho_browser_tool_registry.cc:395-398` | Requires `path`, described as `Absolute path to the local file to select.` Matches the corrected packaged upload property. |
| MCP interface | `maho-chromium/browser/mcp/maho_mcp_session.h:643-646` | Comment describes one local file and browser-boundary path validation. Signature remains compatible. |
| Shared upload handler | `maho-chromium/browser/mcp/maho_mcp_browser_action_handler.h:290-307` | Rejects absent/non-string/empty `path`; calls the delegate; returns generic execution failure or `file_selected: true`. Errors are not AAB-specific. |
| Desktop executor | `maho-chromium/browser/ai/maho_browser_tool_executor.cc:976-990` | New branch calls `MahoMcpBrowserActionHandler::HandleFileUpload(delegate, tab_id, &arguments)` and uses the existing error path. |
| MCP dispatch | `maho-chromium/browser/mcp/maho_mcp_session.cc:5408-5417` | Calls the same shared handler with the resolved target. No alternate extension policy. |
| Default delegate | `maho-chromium/browser/mcp/maho_mcp_session.cc:5425-5428` | Unimplemented delegates still fail closed. |
| Production delegate | `maho-chromium/browser/maho_browser_main_extra_parts.cc:4584-4596` | WebContents, absolute-path, parent-reference, existence, and non-directory guards precede native completion. No extension check. |
| Native completion | `maho-chromium/browser/maho_browser_main_extra_parts.cc:1022-1040` | Requires pending state, removes it before listener delivery, and supplies one native file with a basename display name. |
| Rust tests | `maho/crates/maho-cli/tests/repl_test.rs:123-179` | Existing help test extended; missing-argument and `.msix` dispatch tests added. All 17 REPL tests passed in this audit. |
| C++ fake and assertions | `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:255-262`, `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:832-859` | Approved `.png` selection asserts `ok`, one delegate call, and the exact path. |
| C++ schema regression | `maho-chromium/browser/ai/maho_browser_tool_registry_unittest.cc:404-409` | The passing runtime-schema test explicitly checks upload properties `path`/`tab_id` and required `path`. |

The generation-1 finding that the inspected runtime already lacked an AAB suffix guard remains true. This patch corrects discovery, wording, desktop dispatch, and some validation; it does not remove a guard that exists in this source baseline.

Two names in the supplied implementation summary do not match the working tree. The help assertion is in `repl_help_prints_help`, not a new `repl_upload_help_and_usage` test. The C++ upload assertion is in `AuthorizationReceivesTypedMetadataForBoundaryAction`, not `BoundaryMetadataPreservedInActionApproval`. Verification selected the actual names and used `--gtest_fail_if_no_test_selected`.

## 3. Security checks: confirmed and not implied

### 3.1 Confirmed at the production boundary

The inspected body is equivalent to the following control flow; this is a review excerpt, not a proposed replacement:

```cpp
const base::FilePath supplied = base::FilePath::FromUTF8Unsafe(path);
if (!wc || !supplied.IsAbsolute() || supplied.ReferencesParent()) {
  return false;
}
base::ScopedAllowBlockingForTesting allow_blocking;
if (!base::PathExists(supplied) || base::DirectoryExists(supplied)) {
  return false;
}
return MahoCompleteAutomationFileChooser(wc, supplied);
```

All four requested guards are present. The parent-reference guard is not replaced by normalization. No new extension allowlist, shell expansion, file execution, renderer-JavaScript injection, or file-content-in-JSON path was introduced. A rejected path returns before the pending chooser is consumed.

The shared handler rejects missing, wrong-type, and empty paths before calling the delegate (`maho-chromium/browser/mcp/maho_mcp_browser_action_handler.h:290-303`). The checks above were verified by source inspection and source assertions, not by executing the real production delegate against invalid filesystem fixtures.

### 3.2 Existing authorization remains in front of dispatch

The action contract still declares upload `Sensitive`, approval `Required`, lease `Required`, `ActiveTabOrigin`, and `FailClosed` (`maho-chromium/browser/ai/maho_browser_action_contract.def:23-23`). The filesystem-boundary predicate still recognizes `input.file_upload_select` (`maho-chromium/browser/ai/maho_browser_action_contract.h:104-116`).

The desktop executor resolves the target, authorizes the action, and revalidates it before reaching the new branch (`maho-chromium/browser/ai/maho_browser_tool_executor.cc:681-744`). The MCP session retains its lease gate and upload approval gate, including internal CLI callers (`maho-chromium/browser/mcp/maho_mcp_session.cc:2070-2102`, `maho-chromium/browser/mcp/maho_mcp_session.cc:2150-2165`). The existing controller-specific behavior, including interactive CLI lease acquisition, was not changed by this upload patch; this audit does not reinterpret that behavior as new hardening.

These observations establish that the new dispatch branch does not bypass the existing gates. They do not establish file-specific consent, initiating-document binding, or a post-approval lease-generation guarantee that the proposal separately requested.

### 3.3 Limits of the new filesystem predicate

An existing path that is not a directory is not necessarily a readable regular file. The added predicates do not establish file type, successful readable access, immutable file identity, or a symlink/reparse-point policy. They also do not constitute explicit rejection of embedded NUL or platform-specific namespaces before path conversion. These are distinctions between the delivered checks and the proposal, not a claim of a reproduced exploit.

## 4. Review findings and recommended disposition

Priorities here concern implementation/release follow-up. They are not blockers to finishing this audit report.

### A1 — P1: Production validation performs synchronous filesystem work

**Location:** `maho-chromium/browser/maho_browser_main_extra_parts.cc:4584-4596`.

The added `ScopedAllowBlockingForTesting` surrounds synchronous `PathExists` and `DirectoryExists` calls in the browser delegate and stays alive through chooser completion. It neither creates a worker task nor makes filesystem access asynchronous. A slow or unavailable filesystem can therefore stall the calling browser sequence. This conflicts directly with proposal sections 5/R4 and 7/Step 3, which require a `MayBlock` worker and UI-sequence completion.

**Disposition:** Extract a testable asynchronous validator and update the delegate/handler completion path coherently. Revalidate the exact pending chooser, target, policy, and lease after worker completion. Do not treat renaming the blocking allowance as the architectural fix. A deliberately synchronous implementation would need an explicit design exception and operational limits; none was supplied in this task.

### A2 — P1: Readable-regular-file validation is still incomplete

**Location:** `maho-chromium/browser/maho_browser_main_extra_parts.cc:4588-4596`.

The existence/non-directory checks improve rejection of ordinary missing paths and directories but do not fulfill the proposal's regular-file/readability contract. No helper, typed validation result, explicit link policy, or file-identity check was added here. The shared handler still reduces every delegate failure to the same generic execution error (`maho-chromium/browser/mcp/maho_mcp_browser_action_handler.h:300-307`).

**Disposition:** Add safe regular-file/readability validation without whole-file buffering; explicitly reject or handle special files, links, embedded NUL, and platform namespaces. Keep absolute-path and parent-reference checks. Test real filesystem fixtures separately from fake-delegate tests and document residual path-based time-of-check/time-of-use races.

### A3 — P1: Pending chooser mode, document, and owner binding remain unchanged

**Location:** `maho-chromium/browser/maho_browser_main_extra_parts.cc:949-968`, `maho-chromium/browser/maho_browser_main_extra_parts.cc:980-1040`.

Pending state stores only a listener and mode. Interception tests whether a tab has any active lease; it does not store that lease's owner/generation, the initiating document/frame, or a deadline. Completion forwards the stored mode with one file and an empty base directory. There is no explicit supported-mode check in these helpers. Existing supersession cancellation and consume-before-delivery behavior are useful but are not the complete lifecycle design.

**Disposition:** Implement proposal Step 4's operation identity, supported open/open-multiple modes, cancellation, and initiating-context/lease binding. Keep folders/save dialogs out of the single-file contract until deliberately implemented. Test navigation, frame removal, tab destruction, lease transfer/expiry, replacement chooser, and repeated completion. This audit did not reproduce a cross-document delivery or folder-mode failure in a running browser.

### A4 — P1 release-evidence gap: Interception recipe ownership remains unresolved

**Locations:** the ownership comment at `maho-chromium/browser/maho_browser_main_extra_parts.cc:973-975`; `maho-chromium/build/scripts/apply_chromium_src_overrides.py`; `maho-chromium/build/scripts/test_apply_chromium_src_overrides.py`.

Focused searches still found no maintained `MahoBeginAutomationFileChooser`/`RunFileChooser` interception recipe or chooser-specific regression in the reviewed script pair. Passing the 38 existing recipe tests does not prove that a fresh upstream tree receives this hook. The standard build wrapper processes many other existing overrides; successful local linking is not fresh-tree installation evidence.

**Disposition:** Identify the real owning recipe or add the anchored, idempotent recipe and clean/twice-applied/drift tests proposed in Step 4. Do not maintain the feature solely as an upstream working-tree edit.

A fresh direct read of `chromium/src/base/threading/thread_restrictions.h` was denied by the scoped file tool because the Chromium path resolves through a symlink outside the allowed workspace. That restriction was not bypassed. The generation-1 document's upstream inspection is historical evidence, not a fresh generation-2 inspection. The repository's prescribed build wrapper is the build evidence used here.

### A5 — P2: CLI upload still uses reconnect-and-replay transport

**Locations:** `maho/crates/maho-cli/src/main.rs:5033-5045`, `maho/crates/maho-cli/src/browser.rs:279-284`, `maho/crates/maho-cli/src/browser.rs:323-346`, `maho/crates/maho-browser-mcp/src/client.rs:390-410`.

`upload` still calls `mutate`, which uses `send_tool_call`; that helper selects reconnect-enabled transport. On a qualifying broken connection, `call_with_timeout` reconnects and sends the request again. The library already offers no-replay methods (`maho/crates/maho-browser-mcp/src/client.rs:412-444`). Upload also remains outside the slow-tool list and uses the 15-second default unless overridden (`maho/crates/maho-cli/src/browser.rs:23-66`).

**Disposition:** Implement proposal Step 5: preserve only a definitive pre-dispatch lease-rejection retry; do not automatically resend an ambiguously completed upload selection. Test disconnect-after-send, delayed approval, timeout, and cancellation. This audit established the replay path from source; it did not inject a disconnect into a real browser operation.

### A6 — P2: Persistent regression coverage is narrower than the feature matrix

**Locations:** `maho/crates/maho-cli/tests/repl_test.rs:8-56`, `maho/crates/maho-cli/tests/repl_test.rs:123-179`, `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:255-262`, `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:832-859`.

The new `.msix` REPL test only asserts an `ok` string; its mock responds without validating the requested tool or arguments. The C++ fake records the path and call count but always returns true and does not record/assert the resolved upload tab. Existing upload-denial tests assert unrelated click counters rather than the newly available upload counter. No new real-file validator or chooser browser tests were supplied.

The independent audit capture filled the immediate CLI evidence gap for 12 paths, but it is not a replacement for committed regressions. The existing request-capturing REPL helper at `maho/crates/maho-cli/tests/repl_test.rs:368-417` can support those assertions without a new testing architecture.

**Disposition:** Persist exact tool/argument/count assertions and the extension/path matrix. Add upload-specific denial, target, failure, and successful-result checks; test the production validator separately. The MCP test at `maho-chromium/browser/mcp/maho_mcp_session_unittest.cc:3420-3457` already checks missing path, resolved tab, AAB compatibility, success, and delegate failure, but still uses a fake delegate. That separate MCP binary was inspected, not executed in this audit.

### A7 — Existing unrelated regression: Visual-click catalog and test expectations drift

**Locations:** `maho/crates/maho-cli/tests/tool_test.rs:300-335`; `maho-chromium/browser/ai/maho_browser_capability_catalog.def:89-89`; `maho/crates/maho-cli/src/tool_catalog_snapshot.json`; `maho-chromium/browser/ai/maho_browser_tool_registry_unittest.cc:128-145`; `maho-chromium/browser/ai/maho_browser_tool_registry_unittest.cc:212-221`.

The broader `tool_test` suite fails `packaged_snapshot_tracks_every_canonical_count_and_id`: the snapshot declares 86 canonical capabilities while the current canonical set contains 87. Independent set comparison identifies exactly one missing ID, `browser.visual_click`, and no extra IDs.

The upload snapshot diff only changes its description, property, and required field. The canonical catalog diff separately adds visual click. Thus this failure is not caused by renaming the upload field to `path`. It was left untouched rather than concealing an unrelated change in an upload audit.

Two C++ registry failures expose related expectations: `CharacterizesCurrentDesktopProjection` expects 14 action contracts but observes 15; `SerializedBrowserActionsExposeCanonicalContracts` expects every contract in the desktop projection and fails on `browser_visual_click`. The current visual-click catalog row is separately feature-gated and MCP-scoped. Meanwhile, `CatalogSchemasMatchRuntimeArguments` passes its upload-specific `path` assertions.

**Disposition:** Reconcile the snapshot, feature/surface-aware projections, and their tests through the catalog owner's workflow. Do not merely change an expected count or skip the failing test to obtain a green result. The full CLI tool suite remains 10 passed / 1 failed; the C++ registry suite remains 25 passed / 2 failed.

### A8 — Additional non-upload C++ regression failures and test crash

**Locations:** `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:510-523`; `maho-chromium/browser/ai/maho_browser_tool_executor_unittest.cc:1567-1608`.

The full 127-test binary did not complete. `LeaseRequiredActionFailsWithoutLeaseHolder`, which executes `browser_click`, fails its expected `lease registry` error-substring assertion. Later, `LookupPageAdapterForSeamInterceptsAndFallsThrough`, which executes `page_get_text`, crashes with SIGSEGV. Its final assertion dereferences `generic_result.FindString("tier")` without first checking for a present string. The captured stack points to this test's string inequality assertion, consistent with the missing/null field dereference. No upload is dispatched by either failing test.

A diagnostic run excluded only the crashing page-adapter test so the rest could finish: **123 passed / 3 failed out of 126**. The three failures were the lease-message assertion and the two registry failures in A7. This explicit diagnostic exclusion is not a repair and is not evidence that the entire suite passes.

**Disposition:** Have the relevant owners reconcile the lease error contract and page-adapter result assertions, using presence assertions before dereferencing optional fields. Then rerun all 127 tests without the diagnostic exclusion. This audit does not establish when those non-upload failures were introduced and does not change their implementation or tests.

## 5. Conformance to the architectural proposal

| Proposal work | Audit disposition |
| --- | --- |
| Step 1: failing cases and real browser fixtures | Partial. Source defects and CLI mock cases are covered; real chooser bytes/fixture uploads are not demonstrated. |
| Step 2: generalized copy, `path` parity, shared desktop dispatch | Implemented. The help placeholder is `<path>` instead of the proposal's `<absolute-path>`; usage/schema correctly state absolute-path semantics. This is a minor consistency nit, not a functional suffix restriction. |
| Step 3: asynchronous readable-file validation and typed completion | Partial. Existence/non-directory checks were added; worker dispatch, regular/readable-file policy, and typed outcomes were not. |
| Step 4: pending context/modes/lifecycle and reproducible interception | Not demonstrated as implemented; inspected pending helpers retain their earlier design. |
| Step 5: no-replay transport and outcome/deadline documentation | Not implemented in the inspected CLI path. |
| Step 6 / section 11: complete release evidence | Not satisfied by the current source and test evidence. |
| Optional `browser_file_drop` design | Still a proposal. It is not required for the minimal chooser correction and should not be falsely advertised as shipped. |

Selection remains one local file on the browser host, for an already-opened intercepted chooser. It is not a standalone `maho upload` command, automatic submission, arbitrary dropzone support, or remote-client file transfer. `file_selected: true` is not evidence of a completed server upload.

## 6. Fresh verification record

Commands were issued in generation 2 through the scoped execution harness. Build/test wrappers retain the repository's shared locking; no lock was removed and no GN/build flags were edited to repair a failure. Repeated and filtered test runs below overlap; their pass counts must not be summed into a unique-test total.

| Check | Observed result | Interpretation |
| --- | --- | --- |
| Lock-backed Cargo run of `repl_test` and `tool_test` | REPL: 17 passed. Tool suite: 10 passed, 1 failed. Combined exit 101. | Upload-specific regressions pass; A7 prevents an all-green broad suite. |
| Independent current-binary audit | 5 tests passed; exit 0. | Two mock-transport tests plus three source/schema assertions, including 12 exact file-path payloads. |
| Offline CLI `tool describe input.file_upload_select` | Exit 0; generic `path` schema returned from `packaged_snapshot`, with `runtimeAvailable: false`. | Confirms discovery output, not live browser functionality. |
| Existing overlay recipe suite | 38 passed; exit 0. | Existing recipe coverage only, not chooser-hook installation coverage. |
| Narrow C++ target build | Initial run timed out during linking. Incremental retry completed with exit 0. | `maho_browser_tool_executor_test` built through the prescribed wrapper. No compiler error was reported in the timed-out attempt. |
| Focused C++ upload/security/schema run | 7 passed; exit 0; zero-test failure guard enabled. | Five upload-boundary/authorization cases plus two runtime schema/contract cases. |
| Complete C++ binary, 127 tests selected | Aborted with SIGSEGV; wrapper exit 245. A lease-message assertion also failed before the crash. | Not a passing full-suite run. See A8. |
| Diagnostic C++ remainder, crash case explicitly excluded | 126 ran: 123 passed, 3 failed; exit 1. | Records remaining failures, not a green release gate. See A7-A8. |

### CLI request capture and source assertions

The independent capture used an isolated Unix socket and temporary HOME/config/data directories under the task's workspace scratch area, all cleaned on exit. It inspected actual `tools/call` requests from the freshly Cargo-built CLI. Each of `.aab`, `.msix`, `.dmg`, `.zip`, `.png`, `.pdf`, `.ipa`, `.pkg`, `.CUSTOM`, an extensionless name, a filename with spaces, and a Unicode filename produced exactly the requested `path`, with tool name `browser_file_upload_select`. Help and an empty upload sent no tool request. These fixtures never read or upload real local files to a website.

The three additional audit assertions checked packaged/live-source upload schema alignment, guard ordering before native completion, and shared desktop dispatch plus sensitive/approval/lease contract metadata. They are explicitly source assertions, not production filesystem or browser tests.

### C++ focused selection

The successful seven-test selection was:

```text
MahoBrowserToolExecutorTest.AuthorizationReceivesTypedMetadataForBoundaryAction
MahoBrowserToolExecutorTest.SensitiveActionRunsWithoutAuthorization
MahoBrowserToolExecutorTest.ApprovalDenialBlocksLeaseAndDispatch
MahoBrowserToolExecutorParityTest.BoundaryMutationApprovalDefaultDenyPreventsDispatch
MahoBrowserToolExecutorRuntimeConfigTest.ReadOnlyTierDeniesSensitiveFileScopedActionWithTierMessage
MahoBrowserToolRegistryTest.CatalogSchemasMatchRuntimeArguments
MahoBrowserToolRegistryTest.ActionContractsRemainThePolicySource
```

The full registry sweep was also run alongside the five upload cases: 30 passed / 2 failed out of 32, identifying the same two registry issues later confirmed by the 126-test diagnostic run. No source tests were deleted, altered, or disabled.

### Commands and traceability

Executed command forms, from the workspace root:

```sh
python3 maho/scripts/build_lock.py --timeout 120 -- cargo test --manifest-path maho/Cargo.toml -p maho-cli --test repl_test --test tool_test
python3 -m unittest discover -s maho-chromium/build/scripts -p test_apply_chromium_src_overrides.py
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target maho_browser_tool_executor_test
python3 maho/scripts/build_lock.py --timeout 240 -- chromium/src/out/Default/maho_browser_tool_executor_test --gtest_fail_if_no_test_selected
python3 maho/scripts/build_lock.py --timeout 120 -- chromium/src/out/Default/maho_browser_tool_executor_test --gtest_fail_if_no_test_selected --gtest_filter=-MahoBrowserToolExecutorParityTest.LookupPageAdapterForSeamInterceptsAndFallsThrough
```

The seven-test command uses the same lock-backed executable invocation, with `--gtest_filter` set to the colon-joined names above. The inline Python capture harness, standalone discovery check, and complete outputs are preserved by the scoped CommandManager.

| Evidence | Command ID |
| --- | --- |
| Cargo REPL plus broad tool suite | `82ff5a57-14e5-40b1-8b6d-f22996404346` |
| Independent five-check audit | `6826a813-02d4-462e-8f4e-59b8a65b903b` |
| Offline upload discovery | `e452d8a1-0342-4ffc-9d3d-9a79bc72a7b5` |
| Overlay tests | `1e6076b1-ad2a-4014-9acb-54758029200b` |
| Initial timed-out C++ build | `20baae7e-c37d-4291-b5e2-45cd6eba72d7` |
| Successful C++ build retry | `8a537452-4c69-46d2-ba5d-5400e9cfe3e0` |
| Full C++ run with crash | `a7d2c5ee-8f6b-4a27-82d5-ba3b6142640f` |
| Registry plus upload sweep | `618e2d31-dcc0-433e-afaf-dfea9e2ff0a0` |
| Diagnostic 126-test remainder | `5a3de4a1-0cd5-480c-b729-138d88dcdf23` |
| Successful seven-test selection | `c687e2dc-f9c8-45e2-a407-80ef09bc5ea0` |

## 7. Verification limits and next acceptance evidence

This audit does not claim a live portal upload, real renderer-selected bytes, Windows/Linux path coverage, hostile filesystem/permission tests, cancellation/race reproduction, fresh upstream hook installation, or drag-and-drop implementation. It does not infer that a successful unit binary proves every production target or supported platform was rebuilt. The separate MCP session and chooser browser-test targets were not built or executed here.

Before claiming full proposal completion, resolve or explicitly accept A1-A5, commit the missing regression coverage, reconcile A7-A8, and run the proposal's isolated browser fixture under the repository's required VM/CLI workflow. Verify chooser delivery separately from page/server upload acceptance. Do not use production store submissions, private files, or OS GUI automation as substitutes for that fixture.

## 8. Change boundary and completion semantics

This generation intentionally changes only this Markdown audit report. The original proposal and all implementation/test sources are preserved. The standard C++ build wrapper reapplied existing generated/upstream overlays; those are build activity, not newly authored upload implementation. Source fingerprints and the original proposal hash are checked again when finalizing the report.

The bridge labels Python-led commands, including the prescribed lock-backed Cargo/C++ commands, `not_verification` despite their actual test outputs and exit codes. This documentation-only audit therefore supplies those concrete results explicitly rather than claiming a recognized automated runtime-verification gate. A documentation completion check is not a substitute for the failed broader tests or the missing browser evidence.

A completed audit means that the inspection, test results, findings, and limitations have been recorded. It does not mean every finding has been remediated or that the release acceptance checklist is green. Any completion-harness success for this generation must be read with that distinction.
