# Aside Local App Architecture Notes

As of: 2026-07-25, observed local Aside Browser build `1.0.724.1` at `/Applications/Aside.app`.

Purpose: capture dirty-side, local, static-analysis findings from the installed Aside Browser bundle. This is a reverse-analysis note, not a Maho implementation plan.

## Provenance and scope warning

This document is based only on local app bundle static analysis of `/Applications/Aside.app`. It did not inspect user data, browser profiles, cookies, Keychain entries, access tokens, private cloud data, or saved credentials. It did not run dynamic bypasses, hook live processes, or attempt to defeat product controls. Treat every finding as a local packaging or static string observation, not proof of server behavior or runtime authorization semantics.

This note is intentionally dirty-side. Do not implement directly from it. Any clean-room implementation plan for Maho must be derived in a separate Maho-owned requirements or plan document that cites Maho needs, Maho constraints, and Maho-owned designs.

No proprietary Aside implementation code is copied here. The notes below intentionally summarize file layout, manifest metadata, static route strings, static settings names, and high-level skill patterns only.

## Evidence paths

- Main bundle: `/Applications/Aside.app`
- Main Info.plist: `/Applications/Aside.app/Contents/Info.plist`
- Main binary: `/Applications/Aside.app/Contents/MacOS/Aside`
- Versioned libraries root: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/`
- Agent extension manifest: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/AsideAgentManager/manifest.json`
- Password extension manifest: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/AsidePasswordManager/manifest.json`
- Daemon bundle: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/AsideDaemon/mac-arm64/Aside Daemon.app`
- Daemon binary: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/AsideDaemon/mac-arm64/Aside Daemon.app/Contents/MacOS/aside-daemon`
- Builtin skills: `/Applications/Aside.app/Contents/Frameworks/Aside Framework.framework/Versions/1.0.724.1/Libraries/AsideDaemon/mac-arm64/Aside Daemon.app/Contents/Resources/static/skills/builtin/`

## Main app facts

Observed main app facts from the static pass:

| Field | Observation | Local source |
|---|---|---|
| App path | `/Applications/Aside.app` | Bundle root |
| Bundle id | `at.studio.AsideBrowser` | `Contents/Info.plist` |
| Version | `1.0.724.1` | `ASVersion`, `CFBundleShortVersionString` |
| Browser base | Chromium native app | Chromium file types, `BrowserCrApplication`, framework layout |
| Main binary | Universal Mach-O | `Contents/MacOS/Aside` local metadata |
| Bundle size | `1.9G` observed | Bundle root local metadata |
| SCM revision | `e69b30bba288603e514cffb4c79c359cac68e923-refs/branch-heads/7871_124@{#10}` | `Contents/Info.plist` |

Takeaway: Aside packages as a Chromium-native browser bundle, with feature code and nested app assets under the Chromium-style framework library tree.

## Nested daemon facts

The bundle includes a nested `Aside Daemon.app` under `AsideDaemon/mac-arm64/` and `AsideDaemon/mac-x64/`. The arm64 daemon Info.plist reports:

| Field | Observation | Local source |
|---|---|---|
| Bundle id | `at.studio.AsideBrowser.daemon` | Daemon `Contents/Info.plist` |
| Version | `1.26.724.22` | `CFBundleVersion` |
| Display version | `1.26.724` | `CFBundleShortVersionString` |
| UI mode | `LSUIElement=true` | Daemon `Contents/Info.plist` |
| Executable | `aside-daemon` | Daemon `Contents/Info.plist` |
| Binary format | arm64 Mach-O with bundled/minified JS strings | Daemon binary static scan |

Static route and bridge clues found in the daemon scan include `/trpc`, `/agents`, `/agents/debug`, `/ws/sessions`, and `/extension-bridge`. These names are evidence of local routing surfaces only. They do not prove how authorization, authentication, or server-side policy works at runtime.

## Nested libraries and extensions

The versioned library root contains at least these product-specific components:

- `AsideAgentManager`
- `AsidePasswordManager`
- `AsideDaemon/static/skills/builtin`

`AsideAgentManager` is an MV3 extension named `Aside Browsing Agent`. Its manifest requests broad browser permissions including `debugger`, `nativeMessaging`, `tabs`, `history`, `bookmarks`, `cookies`, `downloads`, and `privacy`, plus private Aside extension APIs such as account, browser import, notification, omnibox, pref get/set, and capture-tab-without-userinteraction. Its host permissions include `<all_urls>` and `https://api.anthropic.com/*`.

`AsidePasswordManager` is an MV3 extension named `Aside Password Manager`. Its manifest and builtin `password-manager` skill show a password manager/autofill surface covering login, signup, generated password references, credential storage, passkey-style/FIDO page scripts, payment card fill, identity fill, and checkout flows.

## Daemon settings and session surface clues

Static strings and shipped skill docs point to a productized agent runtime surface. Observed settings names include:

- `compaction`
- `retry`
- `customModels`
- `defaultModel`
- `modelCategories`
- `followUpBehavior`
- `memory`
- `permission`
- `routineSuggestions`

Observed session API categories include branching, editing, clearing, updating, suspension resolution, abort, interrupt, messages, child sessions, tabs, and files. The shipped `aside` skill exposes safe read/update patterns around settings and sessions, while omitting bulky internals from returned session summaries.

Observed project API clues include read/write handling for `AGENTS.md` and `MEMORY.md`. Treat that as a design signal for per-project agent instructions and memory, not as proof of any specific persistence policy.

## Skills registry patterns

The builtin skills directory is productized, not a loose prompt dump. Observed patterns include:

- Browser and product skills: `aside`, `chrome`, `visual-browse`, `password-manager`
- File and document skills: `pdf`, `xlsx`, `docx`, `pptx`
- Domain API skills: `google-gmail`, `notion`, `linkedin`
- Site-specific skills: `site-specific/asana`, `site-specific/linkedin`, `site-specific/notion`, and others
- Skill metadata support: `autoInject` by keyword and URL, `siteSpecific`, user/builtin override behavior, and frontmatter validation tooling under the skill-creator resources

The most important observed pattern is that many skills prefer typed domain APIs or controlled globals first, then fall back to visible browser interaction only when needed. That is an external product observation, not a ready Maho design.

## Negative findings and non-claims

- The local string scan did not find `pi-mono`, `pi-ai`, `@earendil`, or `@mariozechner`. Do not claim the local bundle proves Pi package usage.
- Public Aside blog material remains the source for any Pi foundation claim. Keep that claim separate from local app-bundle evidence.
- The local string scan saw `bedrock` but did not see `mantle`. Do not claim Bedrock Mantle is wired in this local build.
- Do not infer credential storage security, cloud isolation, model routing, or user data handling from these local static strings alone.

## Observed patterns and Maho delta

This section separates bundle observations from Maho work items so future workers do not recreate mechanisms Maho already has. It is still analysis, not a direct work queue.

### Already implemented in Maho

- Skills registry mechanics already exist in `maho/crates/maho-core/src/skills_manager.rs`. The gap is first-party builtin skill catalog content and policy, not inventing registry mechanics.
- Typed external MCP/browser capabilities already exist in `maho/crates/maho-browser-mcp/src/server.rs`. The gap is unifying action tools into the in-browser agent surface, not inventing typed tools from scratch.
- Opaque Vault handle machinery already exists in `maho/crates/maho-agent/src/vault_tools.rs`. The gap is activation, which is blocked until Vault security milestones are complete.

### True gaps for catch-up planning

- P0: the in-browser agent tool executor currently has read-only browser tools, while action tools live in the MCP or external surface. The catch-up work is to expose safe action execution inside the browser agent path with Maho-owned policy and audit behavior.
- P0/P1: approval is not sufficiently first-class for action tools. Maho needs typed policy, suspension, resume, and audit records rather than string-only policy checks.
- Partial gap: fallback ladder policy is not enforced at the executor level. A Maho plan should define when to use typed browser state, DOM refs, snapshots, visible browser interaction, or pixel-level fallback, then enforce that order in the executor.
- Product gap: first-party builtin skill catalog content needs Maho-authored policy, metadata, validation, and review. This is catalog and governance work on top of existing mechanics.

### Policy-blocked or governance-gated items

- Vault-backed agent actions remain blocked until Maho's password-manager and Vault security milestones are complete. Do not market or ship secure agent vault parity from this note alone.
- Third-party and domain API catalog expansion requires a follow-up governance ADR. This note should not recommend Notion, LinkedIn, Gmail, or similar third-party API skills now.

### Separate decision needed

- The runtime architecture choice remains open: in-process C++/Rust agent runtime versus an out-of-process daemon architecture. That choice needs a Maho-owned design decision with security, reliability, crash isolation, platform, and maintenance tradeoffs.
- Per-project `AGENTS.md` and `MEMORY.md` support is a lower-priority product idea, not part of the immediate catch-up track.

Clean-room follow-up requirement: any implementation plan must be written as a separate Maho requirements or plan document. That document may use this note as input, but it must restate the problem, constraints, owned design, approval model, tests, and rollout criteria in Maho terms before engineering starts.

## Maho guardrails

- Do not copy Aside code, prompts, minified bundles, or private implementation details.
- Do not replace Maho's Swiftide direction with Pi based on this static analysis.
- Do not market secure agent vault parity until Maho's own password-manager and Vault security blockers are resolved.
- Keep local static evidence, public marketing evidence, Maho requirements, and Maho implementation plans separated in future docs.
