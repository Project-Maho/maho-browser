# maho-workspace Local Guardrails

These rules apply only inside `maho-workspace` and are layered on top of the global `~/.gemini/GEMINI.md` executor guardrails.

## 1. Repo Identity

This is a multi-project monorepo:

- `maho/` = Rust workspace + native shells
- `maho-chromium/` = Chromium overlay and C++/WebUI work
- `mail/` = separate Tauri workspace
- `website/` = Astro site
- `maho/relay/` = separate Cargo workspace

Do not assume a single build system or a single Cargo workspace.

## 2. Scope Routing

Before editing, identify which subtree owns the change.

- Rust backend logic: `maho/crates/`
- Chromium UI / sidebar / Views / WebUI: `maho-chromium/browser/ui/`
- Website: `website/`
- Mail frontend: `mail/src/`
- Mail Rust: `mail/src-tauri/`
- Relay: `maho/relay/`

Do not spread a fix across multiple subprojects unless the task explicitly requires cross-boundary changes.

## 3. Build Lock Rules

The workspace root `.build.lock` is real and must be treated as a lock file held by process file descriptor.

Never:

- delete `.build.lock`
- rename `.build.lock`
- “clear” it as a workaround

If a build or typecheck waits on the lock:

1. inspect the holding process with `lsof .build.lock`
2. wait for that process or kill the process if appropriate
3. retry the command

Do not try to solve lock contention by editing files.

## 4. GN / Chromium Guardrails

If Chromium build flow fails during GN regen or later:

- STOP
- report the failure
- do not edit any `BUILD.gn` file as a bypass

Without explicit approval, do not modify:

- `maho-chromium/**/BUILD.gn`
- `chromium/src/**/BUILD.gn`
- `chromium_src/**/BUILD.gn`

Do not modify upstream Chromium files directly. Use the overlay/override model that already exists in this repo.

## 5. Workspace Boundaries

Never add these isolated workspaces into the root workspace members:

- `maho/relay`
- `mail`

Do not “fix” Cargo problems by merging workspaces together.

## 6. Sidebar / Chromium-Specific Constraints

When working in `maho-chromium/browser/ui/views/sidebar/`:

- prefer minimal, file-local fixes
- follow Chromium C++ style
- reuse existing stable patterns before inventing new ones
- do not widen a sidebar fix into unrelated drag, layout, animation, or WebUI changes unless explicitly requested

If a task is specifically about a scoped sidebar behavior fix, treat nearby subsystems as out of scope by default.

## 7. Theme Picker ADR

The theme picker is permanently `chrome://maho-space-create`.

Do not:

- migrate it to `maho-space-config`
- resurrect a native Views theme picker
- add a second production theme selection surface

If a request appears to conflict with this, stop and mention the ADR rather than silently implementing against it.

## 8. Commands and Verification Defaults

Prefer the repo’s canonical commands:

- Chromium build: `python3 maho-chromium/build/scripts/build_maho.py`
- Rust workspace typecheck: `cd maho && bun run typecheck`
- Rust workspace test: `cd maho && bun run test`
- Mail site/test commands only inside `mail/`
- Website commands only inside `website/`

Do not use a command from one subproject as if it applies repo-wide.

## 9. Search and Repo Assumptions

Do not infer `sparse-checkout` from repo size.
Do not touch `chromium/` git state unless the user explicitly asks.
Do not assume duplicate paths mean both are editable targets; verify which subtree is canonical for the task first.

## 10. Output Expectations in This Repo

When doing implementation work here, make these explicit in your preflight or self-check if relevant:

- which subtree owns the change
- whether `.build.lock`-backed commands were used
- whether GN files were intentionally left untouched
- whether any isolated workspace boundary was involved

If you cannot verify a repo-specific constraint, say so explicitly instead of hand-waving.
