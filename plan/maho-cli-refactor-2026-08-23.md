# Maho CLI refactor plan — 2026-08-23

## Scope

Add an agent-first top-level entry and first-class browser interaction verbs to `maho/crates/maho-cli` without removing or renaming existing commands, deprecation shims, MCP capabilities, or backend behavior. Keep implementation changes in the Rust CLI crate, apart from this plan document.

## Ordering

1. Preserve the current command tree and existing local changes. Normalize agent-first argv into the existing command tree before clap rather than duplicating handlers.
2. Reuse `cmd_run` for root prompts. Treat a root `http://` or `https://` argument as a browser-open request. Support both `--session <id>` and `--session=<id>` for agent-first continuation, and `maho -- <prompt>` as the explicit escape for prompts beginning with `-`.
3. Parse argv losslessly for routing decisions. Recognize `--allow-robots-bypass` only in the root/global option prefix, respect global option values and `--`, and never remove the token from subcommand or positional data.
4. Keep `MAHO_AGENT_SYSTEM_PROMPT` input-only. Carry robots consent in parsed CLI state and compose the effective robots policy only when constructing an agent backend (`run`, `agent`, or `research`), so non-agent child processes such as the MCP exec path do not inherit a mutated environment. Strip a previously composed built-in robots policy before appending the invocation policy to avoid duplicates.
5. Add `click`, `type`, `key`, `scroll`, `hover`, and `select` as thin top-level wrappers around existing browser MCP tools. Selector-based verbs resolve a DOM selector to a safe accessibility ref before dispatch; `type` targets `:focus` and may submit with Enter.
6. Fail closed when mapping selector identity into the accessibility tree: expected DOM identity fields must be present and exact, incompatible roles are rejected, the best candidate must clear a minimum confidence threshold, and equal-score best candidates remain ambiguous. A role-only similar AX node is never enough when the queried DOM node is absent.
7. Resolve one concrete tab before each semantic mutation, acquire and verify a 60-second browser lease before selector/snapshot work, hold it through the mutation, heartbeat during long critical sections, and release on normal success/error through a lease guard. Stateful control and action calls use the existing connection without transparent reconnect/replay. On task cancellation/Ctrl+C, the browser lease TTL is the cleanup backstop because async same-session release cannot run from Rust `Drop`.
8. Keep separate CLI processes parallel-friendly: there is no process-global mutex. Serialization is limited to the browser's per-tab lease and dependent operations within one semantic invocation.
9. Keep human tool discovery/rendering in `cmd_tool`/main rather than argv preprocessing. Live discovery continues to fall back to the packaged catalog through the existing command implementation.
10. Keep the existing LemonSqueezy single pay-what-you-want checkout URL. Preserve the compatible `--amount 10|50|100` input only as a suggested amount and make help/output/tests explicit that the buyer chooses the final amount in checkout.
11. Add focused tests for argv routing/consent boundaries, duplicate-free policy composition, fail-closed selector fingerprinting, semantic argument mapping, and billing wording. Add a fake-browser lease-ordering test only if it stays small; otherwise rely on the explicit implementation ordering plus existing browser test harnesses.
12. Verify with `cd maho && bun run typecheck` and `cargo test --manifest-path maho/Cargo.toml -p maho-cli` from the workspace root. Do not change the known pre-existing packaged tool-catalog snapshot mismatch (75 vs 82) or HNSW benchmark cap.
13. Inspect the final scoped diff, stage only `maho/crates/maho-cli/**` plus this plan, and create one atomic conventional-style commit. Do not push.

## Compatibility constraints

- Additive CLI surface only; no existing command or deprecation shim is removed or renamed.
- Work on the currently checked-out branch; no branch or worktree creation.
- No `BUILD.gn`, GN flag, shared `.build.lock`, or non-CLI implementation edits.
- Existing browser MCP tools remain the authoritative implementation; no duplicate backend is introduced.
- Unrelated dirty files in other workstreams must remain untouched and unstaged.
