# PROJECT KNOWLEDGE BASE

**Generated:** 2026-07-29
**Commit:** bb3dd286
**Branch:** main

## OVERVIEW
Maho Browser monorepo: Rust core crates + Chromium overlay fork + native shells
(macOS/iOS/Android). It includes integrated desktop Mail, a legacy/reference
Tauri Mail tree, and Maho Website (Astro). Brave-style architecture —
`maho-chromium` patches upstream Chromium and `maho` crates provide backend
logic via the FFI bridge.

## COMMUNICATION (MANDATORY)
- **한국어로 답할 때는 항상 존댓말로, 공손하게 답한다.** 반말 금지. 사용자를 친구처럼 대하지 말 것.
- When replying in Korean, ALWAYS use polite/formal speech (존댓말). Never use casual/informal speech (반말).
- Keep a respectful, professional tone at all times.

## LICENSING & DISTRIBUTION
- **Maho is CLOSED-SOURCE / PROPRIETARY.** This monorepo (browser, mail, relay, native shells) is NOT open source. Do NOT introduce OSS-claim language ("open source", "MIT licensed", "auditable source", "View on GitHub for source code", etc.) into marketing copy, docs, or WebUI strings.
- **Public source repositories do NOT exist for this codebase.** Never link to a public source repo URL for Maho itself. Any `github.com/*/maho*` link outside of upstream dependency references is wrong by default.
- **Downloads are distributed via a separate PUBLIC GitHub RELEASE-ONLY repository** (release binaries + release notes only, no source). This repo hosts `.dmg` / `.exe` / `.AppImage` / `.apk` and equivalent artifacts. When wiring download URLs (e.g. `website/src/pages/download.astro`), point to the release-only repo's `/releases` path, NOT to any main-source-tree URL.
- Upstream dependency GitHub links (Chromium, Lucide, Fontsource, etc.) are fine — they refer to third-party OSS, not to Maho's source.

## STRUCTURE
```
maho-workspace/
├── maho/               # Rust workspace + native shells (see maho/AGENTS.md)
│   ├── crates/         # maho-core, maho-ffi, maho-storage, maho-types, maho-engine, maho-import, maho-agent, maho-browser-mcp, maho-cli, maho-jni, maho-bench
│   ├── relay/          # Standalone sync relay server (separate Cargo workspace; see maho/relay/AGENTS.md)
│   ├── web-ai/         # Unified AI WebView bundle (Preact, iOS + Android + Desktop; see maho/web-ai/AGENTS.md)
│   ├── ios-shell/      # iOS shell
│   ├── android-shell/  # Android Gradle project
│   └── appium-tests/   # Regression automation
├── maho-chromium/      # Chromium overlay (see maho-chromium/AGENTS.md)
│   ├── chromium_src/   # Upstream file overrides (#define + #include pattern)
│   ├── browser/        # Maho C++ browser code + WebUI resources
│   ├── components/     # Mojo IDL, shared constants
│   ├── third_party/maho/ # maho-core prebuilt static lib + C++ bridge
│   └── build/          # GN config + build scripts
├── mail/               # Legacy/reference Tauri Mail workspace
├── website/            # Maho Website (Astro 6 + Starlight)
├── chromium/           # Upstream Chromium source (gitignored, ~121GB, gclient-managed)
└── docs/               # Design docs
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Rust backend logic | `maho/crates/maho-core/` | Manager-per-concern architecture |
| Shared types | `maho/crates/maho-types/` | Contract types only |
| FFI bridge (Rust→C++) | `maho/crates/maho-ffi/` → `maho-chromium/third_party/maho/` | |
| Chromium UI features | `maho-chromium/browser/ui/` | Views + WebUI surfaces |
| WebUI resources (TS) | `maho-chromium/browser/resources/` | Per-feature app.ts + HTML |
| Maho Mail (shipped UI) | `maho-chromium/browser/resources/maho_mail/` | Browser-integrated Mail WebUI at `chrome://maho-mail`. Canonical owner for all Mail product work. |
| Maho Mail (helper + backend) | `maho-chromium/browser/mail_helper/` + `maho/mail-core/` | Profile-scoped helper process and mail engine |
| Maho Mail (legacy Tauri reference) | `mail/src/`, `mail/src-tauri/` | Reference-only workspace. No standalone Mail app or `maho-mail` binary ships. See `mail/AGENTS.md`. |
| Maho Website | `website/src/` | Astro pages & Starlight docs |
| Upstream overrides | `maho-chromium/chromium_src/` | Brave-style #define + #include |
| Sync relay server | `maho/relay/` | Standalone workspace; do NOT add to root Cargo.toml. See `maho/relay/AGENTS.md` |
| AI WebView bundle (iOS/Android/Desktop) | `maho/web-ai/` | Preact SPA loaded by WKWebView/WebView; see `maho/web-ai/AGENTS.md` |
| Build Chromium | `chromium/src/out/Default/` | Canonical: `python3 maho-chromium/build/scripts/build_maho.py` from repo root. It holds the shared `.build.lock`, so builds SERIALIZE — this is intentional: concurrent Chromium builds exhaust RAM and swap-thrash, making all builds slower. Pass `--ninja-target <T>` (repeatable) to build a narrower target through the same lock-backed wrapper. Build the NARROWEST target that verifies your change; avoid full `unit_tests`/`interactive_ui_tests`/`browser_tests` unless you must run that suite. Do NOT run raw `autoninja -C out/Default` on the shared machine — it bypasses the lock; if unavoidable, wrap it: `python3 maho/scripts/build_lock.py -- autoninja -C out/Default <target>`. |
| Build maho-core lib | `maho-chromium/build/scripts/build_maho_core.py` | Produces prebuilt static lib |
| Regression tests | `maho/appium-tests/` | Appium runners |
| Icon system (canonical) | `docs/icon-system.md` + `maho/assets/icons/sf-to-lucide-mapping.json` | Lucide is the unified icon library across all surfaces. Per-platform: `lucide-react` (Mail; canonical for new WebUI surfaces, see `maho-chromium/docs/webui-shadcn-conventions.md`), legacy vendored SVGs at `browser/resources/<feature>/icons/` for pre-shadcn surfaces, `aggregate_vector_icons` GN target at `maho-chromium/browser/ui/views/maho_lucide_icons/` (Native Views; auto-generated from `manifest.json` via `maho-chromium/build/scripts/lucide_icons/generate.py`), `lucide-icons-swift` SwiftPM (macOS/iOS), AVDs at `maho/android-shell/app/src/main/res/drawable/ic_*.xml`. License: `LICENSES/lucide.txt` |
| Reference screenshots (Arc/design target) | `maho/reference_screenshots/` | Ground-truth UI targets. e.g. `arc-library-space.jpeg` = Arc's Spaces library view reference for Maho's Archive-Spaces overlay. ALWAYS consult before claiming visual parity. |

## CODE MAP
| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `MahoBrowserMainExtraParts` | C++ Class | `maho-chromium/browser/maho_browser_main_extra_parts.h` | Chromium startup lifecycle hook, FFI init, relay client |
| `MahoCore` | C FFI Struct | `maho/crates/maho-ffi/src/lib.rs` | Rust core engine handle passed to C++ bridge |
| `maho-cli` | Rust Binary | `maho/crates/maho-cli/src/main.rs` | Main CLI entry point for browser/mail/mcp commands |
| `build-queue` | Rust Binary | `maho/build-queue/src/main.rs` | Local socket daemon managing lock-backed Chromium builds |
| `relay` | WASM / Worker | `maho/relay/src/lib.rs` | Cloudflare Workers sync relay server entry point (`fetch`) |
| `web-ai` | Preact App | `maho/web-ai/src/main.tsx` | Cross-platform WebView bundle entry for AI surface |

## CONVENTIONS
- `bun run` for repo scripts, `bunx` for one-off CLIs
- `bun run test` / `bun run typecheck` proxy to Cargo at maho/ root
- Repo-provided build/test wrappers serialize through the workspace root `.build.lock`, so `bun run test` and `bun run typecheck` may pause and print `Waiting for build lock: ...` when another lock-backed step is active
- Chromium C++: explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- Mojo handlers are thin — delegate to helpers in `browser/ai/` or `browser/ui/`
- WebUI pattern: each feature has `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts`
- `plan/` is local planning scratch, not canonical project documentation. Keep it gitignored; durable decisions and ship-state notes belong in AGENTS.md, ADRs, or committed docs.
- Subtree AGENTS.md files take precedence over this root guide

## CURRENT SHIP STATUS
- **Maho Mail is an opt-in Beta.** New profiles default to `maho.mail.enabled=false`; users enable it only from the always-visible Features pane in Maho Settings. Existing profiles that explicitly stored `true` remain enabled.
- The Mail feature set includes Gmail sync, sidebar and macOS Dock unread badges, OS notifications, sanitized remote images, and Maho Agent Mail-broker access. Disabling the Settings toggle live-hides the sidebar entry, shuts down the helper, rejects agent Mail tools, and closes active Mail WebUI handlers.
- **Browser-integrated Mail is the only Mail that ships.** `mail/` is a legacy Tauri reference tree. No standalone Mail application, `maho-mail` binary, or `maho-mcp` binary is distributed; Mail lives at `chrome://maho-mail`, CLI access goes through `maho mail <subcommand>`, and MCP access goes through the browser's in-process MCP server.
- **Mail agent access has three gates and fixed denial reason codes.** `maho.mail.enabled` (feature), helper lifecycle readiness, and `maho.ai.mail_read_allowed` (read consent, default false). Denials return, in evaluation order, `mail_tool_unknown`, `mail_feature_disabled`, `mail_helper_starting` or `mail_helper_unavailable`, `mail_global_policy_denied`, `mail_read_consent_required`. Completion-time and surface-specific denials also include `mail_helper_generation_changed` and `mail_tool_not_available_on_agent`. Writes and account mutations never inherit a global allow; they resolve to `mail_typed_approval_required` and return `mail_typed_approval_denied` when the user rejects the prompt. These strings are a public contract shared by `maho-chromium/browser/ai/maho_mail_tool_authorization.h`, `maho/crates/maho-agent/src/permission.rs`, and the website docs. Do not rename one side alone.
- Mail WebUI/resources/helper targets remain bundled while runtime-gated by `maho.mail.enabled`. Do not remove those targets as part of feature work; bundle cleanup is separate shared-build infrastructure work.
- Keeping the WebUI/resources/helper targets in the bundle is acceptable for now; they are unreachable dead weight. Removing them from GN/build scripts is a separate cleanup requiring explicit approval because it touches shared build infrastructure.

## LOCALIZATION — ENGLISH ONLY (i18n SHELVED)
- **Maho ships English only.** This matches Maho Browser's product policy. Localization / i18n is **indefinitely shelved** as of 2026-07-30.
- **Hardcoded English user-facing strings are NOT a defect.** Do not open work, file findings, or "fix" surfaces just because they contain literal English strings. `chrome://maho-settings` in particular has zero localization call sites by design under this policy.
- **Do not build new localization infrastructure.** No `.grd`/`.grdp` files, no new `GetLocalizedStrings` mojom seams, no per-surface JSON locale bundles. Adding one requires explicit user approval reversing this decision.
- Existing localization assets are left in place, not extended:
  - `maho_welcome` has a mojom `GetLocalizedStrings` seam (201 `IDS_MAHO_WELCOME_*` keys). It works; leave it alone. Do not replicate the pattern elsewhere.
  - `maho_mail` carries `react/i18n/locales/{en,ko}.json` (846 lines each, translated). Maho ships English only; do not extend these assets.
  - The built app bundles 456 upstream Chromium locale `.pak` files including `ko.lproj`. That is inherited upstream behavior, **not** a Maho localization commitment.
- Shelving record: `plan/settings-i18n-localization-seam-2026-07-30.md` (marked SHELVED) retains the string-surface sizing (~400-500 occurrences) and the two candidate mechanisms, should localization ever be revived.

## ANTI-PATTERNS (THIS PROJECT)
- **No adding relay/ or mail/ crates to root workspace members** (libsqlite3-sys conflict)
- No type-safety suppression
- No domain types in `maho-core`; use `maho-types`
- No direct FFI/business-logic mixing in `maho-ffi`
- **NEVER delete `.build.lock`** — see BUILD LOCK below
- **No i18n/localization work** — Maho is English-only; hardcoded English strings are not defects. See LOCALIZATION section above.
- **빌드 실패 시 GN 파일 강제 수정 금지** — 빌드 (특히 `build_maho.py`) 가 GN regen 단계 또는 그 이후에서 실패하면 즉시 중단하고 사용자에게 보고. `chromium/src/**/BUILD.gn`, `maho-chromium/**/BUILD.gn`, `chromium/src/chrome/test/BUILD.gn` 등의 GN 파일을 우회 fix 목적으로 직접 수정하지 말 것. 다른 세션의 미완 작업이나 working tree dirty state 인 경우가 많고, 임의 cleanup 은 추후 충돌·revert·데이터 손실을 부른다. fix 가 필요해 보여도 **사용자 명시 승인 없이는 GN 편집·삭제 금지**.
- **Chromium/GN/build feature flags 또는 build flags 추가·변경 전에는 반드시 사용자 명시 승인을 받을 것.**
- **브랜치 생성/체크아웃, 워크트리 생성 절대 금지** — `git branch`, `git checkout -b`, `git switch -c`, `git switch <other-branch>`, `git worktree add` 등 브랜치를 새로 만들거나 다른 브랜치로 이동하거나 워크트리를 추가하는 작업을 하지 말 것. 항상 사용자가 현재 체크아웃해 둔 브랜치에서 그대로 작업한다. 별도 브랜치나 워크트리가 필요해 보이면 작업을 멈추고 사용자에게 보고할 것.
- **마호 브라우저 대상 자동화 진행 시 무조건 Maho CLI 사용** — 브라우저 대상 제어/자동화 작업(탭 제어, 웹 콘솔 작업, 폼 입력, 네비게이션 등)을 수행할 때는 반드시 Maho CLI(`maho tab`, `maho tool`, `maho browser repl`, `maho session`, `maho vault` 등)를 사용한다. OS 레벨 스크립팅(AppleScript, System Events, OS 마우스 클릭)이나 Orca/computer-use/기타 데스크톱 GUI 자동화 도구는 엄격히 금지한다. CLI 기능에 제약이나 버그가 발생할 경우 우회 수단(OS 자동화)을 찾지 말고 Maho 브라우저/CLI의 근본 원인을 직접 수정한다.
- **macOS Desktop E2E/인터랙티브 테스트는 무조건 Tart VM 격리 실행** — 브라우저 UI 인터랙티브 테스트(`interactive_ui_tests`, `interactive-smoke`), AI 패널 E2E(`ai-mock`), 전체 E2E 리그레션 매트릭스 검증은 반드시 Tart macOS 가상 머신(`maho-e2e-golden`, `TART_HOME=/Volumes/maho-build-v2/maho-tart`) 환경에서 `macos_vm_e2e.py` / `macos_vm_live_matrix.py`를 통해 실행한다. 호스트 데스크톱에서 직접 UI 창을 띄워 마우스/키보드/포커스 간섭을 일으키는 테스트 실행은 엄격히 금지한다.
- **랜딩페이지 Hero 영상/애니메이션 임의 교체 금지** — 랜딩페이지(`website/src/pages/index.astro`, `BrowserHeroSection.astro`)의 메인 Hero 데모 비디오(`public/demo/hero/maho-hero-capture.mp4`, `maho-hero-capture-poster.png`)는 사용자가 직접 캡처한 원본 에셋입니다. 사용자의 명시적인 사전 승인 없이 이를 HyperFrames, 모의(mock) 인터랙티브 컴포넌트, 다른 영상/포스터 등으로 교체하거나 수정하지 말 것.

## BUILD LOCK — `.build.lock`

The workspace root `.build.lock` serializes all build/test entrypoints (`build_maho.py`, `bun run test`, `bun run typecheck`) via `fcntl.flock`. **The lock lives on the file descriptor, not the file path.**

**NEVER `rm .build.lock`:**
- The holding process still locks the deleted inode (fd stays open)
- A new process creates a fresh file (new inode) and locks it immediately
- Both builds run concurrently → corrupted output, partial writes, linker failures

**If a build times out waiting for the lock:**
```bash
# 1. Find the process holding the lock
lsof .build.lock

# 2. If a build IS running → wait for it, or kill the PROCESS (not the file)
kill <PID>

# 3. If no process holds the file → the lock was already released; just retry
python3 maho-chromium/build/scripts/build_maho.py
```

The lock is **automatically released** when the holding process exits (normal exit, crash, or SIGKILL). There is no scenario where deleting the file is the correct action.

## COMMANDS
```bash
# Rust workspace (Browser)
cd maho && bun run typecheck          # cargo check --workspace
cd maho && bun run test               # cargo test --workspace

# Maho Mail (legacy Tauri reference workspace only — NOT the shipped Mail app)
# Shipped Mail is Browser-integrated: build the browser and open chrome://maho-mail.
cd mail && bun run tauri dev          # Run the legacy Tauri reference app
cd mail && bun run test               # vitest (frontend)
cd mail && cargo test                 # Rust tests (run inside mail/ folder)

# Maho Website
cd website && bun run dev             # Astro dev (port 5555)
cd website && bun run build           # Build site

# Chromium build — ALWAYS via build_maho.py; it holds the shared .build.lock so builds
# SERIALIZE. Concurrent Chromium builds on one machine exhaust RAM (→ tens of GB of swap)
# and make ALL builds crawl. One heavy build at a time finishes faster than three thrashing.
python3 maho-chromium/build/scripts/build_maho.py    # canonical, builds chrome target
# Fast locked incremental rebuild of a NARROW target (skip rust + lucide, keep the lock):
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target <target>

# Build the NARROWEST target that verifies your change. Do NOT build full `unit_tests`,
# `interactive_ui_tests`, or `browser_tests` (they compile every test TU — huge) unless you
# actually need to run that whole suite. To just check a change compiles, build `chrome` or
# the single affected target.

# Builds via build_maho.py auto-enqueue through the build-queue daemon: it serializes
# all builds on out/Default (one at a time — prevents RAM/swap thrash) with crash-safe
# auto-release and queue visibility, over a .build.lock fallback.
# Check the queue: printf 'STATUS\n' | nc -U .build-queue.sock
# Full details: docs/operations/build-queue.md
#
# DO NOT run raw `autoninja -C out/Default ...` on the shared build machine — it BYPASSES the
# queue/lock, so parallel agents build at once, blow past RAM, and everything swap-thrashes.
# If you truly need raw ninja, wrap it in the lock:
#   python3 maho/scripts/build_lock.py -- autoninja -C out/Default <target>

# Build maho-core static lib for Chromium
python3 maho-chromium/build/scripts/build_maho_core.py

# Relay server
cd maho/relay && cargo test && cargo clippy -- -D warnings
```

## NOTES
- `chromium/` is gitignored (121GB upstream); managed by gclient, not this repo's git
- **Never use `git sparse-checkout`** in this workspace. `chromium/` is already gitignored and gclient-managed, so there is nothing to sparse-checkout. Do not infer sparse-checkout from the repo size unless the user explicitly requests it.
- Symlink setup: `ln -s ../../maho-chromium chromium/src/maho`
- **Three Cargo workspaces**: `maho/Cargo.toml` (main), `maho/relay/Cargo.toml` (isolated), and `mail/Cargo.toml` (isolated).
- See `maho/AGENTS.md` for crate-level detail; `maho-chromium/AGENTS.md` for overlay detail
- Subdirectory AGENTS.md files take precedence over this root guide
