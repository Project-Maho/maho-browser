# PROJECT KNOWLEDGE BASE — maho/web-ai

**Created:** 2026-06-19
**Phase:** A complete (foundation + BYOK reference screen + both native bridges)

## OVERVIEW
Unified AI screen bundle for Maho Browser on iOS, Android, and Desktop.
Single TypeScript + Preact source compiles to one ES module (`dist/ai-bundle.js`) that is:
- Loaded by `WKWebView` on iOS (via `WebViewBridgeController.swift`)
- Loaded by `WebView` on Android (via `AIWebViewFragment.kt`)
- Served from `chrome://maho-ai/` on Desktop Chromium (Phase D, TBD)

## STRUCTURE
```
web-ai/
├── src/
│   ├── app.tsx                    # Hash router — screen switch on window.location.hash
│   ├── main.tsx                   # Preact render entry point
│   ├── styles.css                 # CSS custom properties / design tokens
│   ├── bridge/
│   │   ├── types.ts               # MahoBridge interface — full typed contract
│   │   ├── rpc.ts                 # JSON-RPC 2.0 dispatcher (postMessage → native)
│   │   └── platform-detect.ts     # iOS / Android / Desktop detection
│   ├── hooks/
│   │   ├── use-bridge.ts          # useBridge() — stable MahoBridge implementation
│   │   ├── use-hash-route.ts      # useHashRoute() — parses #screen[?params]
│   │   └── use-streaming.ts       # useStreaming() — chat event poll loop
│   ├── screens/
│   │   └── byok/                  # ✅ BYOK Settings (Phase A reference)
│   │       ├── byok-screen.tsx    # Full screen with load/validate/save/delete flows
│   │       └── provider-card.tsx  # Per-provider row with validation state
│   ├── ui/                        # Shared primitives (no external deps)
│   │   ├── button.tsx
│   │   ├── input.tsx
│   │   ├── list-item.tsx
│   │   ├── status-banner.tsx
│   │   └── icon.tsx               # Inline Lucide SVG paths (no lucide-react import)
│   └── utils/
│       └── clsx.ts                # Minimal class joiner
├── tests/
│   ├── setup.ts
│   ├── bridge.test.ts             # RPC dispatcher unit tests (5 tests)
│   └── byok-screen.test.tsx       # BYOK screen integration tests (9 tests)
├── index.html
├── package.json
├── vite.config.ts
└── tsconfig.json
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add a new screen | `src/screens/<name>/<name>-screen.tsx` + add case to `src/app.tsx` |
| Add a bridge method | `src/bridge/types.ts` (interface) + `src/hooks/use-bridge.ts` (impl) |
| Add a UI primitive | `src/ui/` |
| Add a Lucide icon | `src/ui/icon.tsx` — add to `IconName` union + `paths` record |
| Wire Android method | `maho/android-shell/.../ui/webview/WebViewBridge.kt` — `dispatchMethod()` |
| Wire iOS method | `maho/ios-shell/Support/WebViewBridgeController.swift` — `dispatchMethod()` |
| Change CSS tokens | `src/styles.css` — or native injects `--color-*` vars via `evaluateJavaScript` |

## SCREEN URL FORMAT
```
#<screen>[?key=value&key=value]

Examples:
  #byok
  #chat?sessionId=abc&pageUrl=https%3A%2F%2Fexample.com
  #space-ai-config?spaceId=xyz
  #offline-model
  #conversations
  #agent
```

## CONVENTIONS
- **No tailwind** — uses CSS custom properties (`var(--color-*)`) with inline utility classes manually written. Do not add Tailwind.
- **Preact, not React** — `jsxImportSource: "preact"`, `import from 'preact/hooks'`. Never `react` or `react-dom`.
- **Explicit prop interfaces** — do NOT extend `JSX.HTMLAttributes`. Preact types `class` as `Signalish<string | undefined>`, which is incompatible with `clsx`. Define props explicitly.
- **No external UI library** — keep bundle tiny. Add primitives to `src/ui/` as needed.
- **useReducer for screen-level state** — all screen state lives in a reducer, not scattered `useState` calls.
- **Bridge calls are async** — every `bridge.*()` call returns a Promise. Use `void fn()` pattern or `useCallback + useEffect` for event handlers to satisfy `noImplicitReturns`.
- **Tests use @testing-library/preact** — query by role or aria-label, not by class.
- `vi.fn<(arg: Type) => ReturnType>()` — vitest v3 single-function-type syntax.

## ANTI-PATTERNS
- Do NOT extend `JSX.HTMLAttributes` in component prop interfaces (Signalish type pollution).
- Do NOT import `lucide-react` (too large; use `src/ui/icon.tsx` inline paths).
- Do NOT use `getByText` for elements that share text with section headers — use `getByRole('button', { name: ... })` or `getByText` when only one text node exists.
- Do NOT add new screens to `src/screens/` without adding a case to `src/app.tsx`.
- Do NOT add new bridge methods without wiring them in both `WebViewBridge.kt` AND `WebViewBridgeController.swift`.

## BUILD OUTPUT (Phase A actual metrics)
```
dist/ai-bundle.js    36.52 kB │ gzip: 12.34 kB
dist/ai-bundle.css    1.96 kB │ gzip:  0.57 kB
```
Total gzipped: **12.91 kB** — well under 80 KB budget.

## COMMANDS
```bash
cd maho/web-ai

bun install                # install deps
bun run dev                # Vite dev server on :5173
bun run build              # tsc --noEmit + vite build → dist/
bun run typecheck          # tsc --noEmit only
bun run test               # vitest run (14 tests)
bun run test:watch         # vitest watch mode

# Copy built bundle to Android assets
cp -r dist/* ../android-shell/app/src/main/assets/web-ai/

# Copy built bundle to iOS bundle resource
cp -r dist/* ../ios-shell/web-ai/
```

## MIGRATION PHASES
| Phase | Status | Screens |
|-------|--------|---------|
| A — Foundation + BYOK | ✅ Complete | BYOK Settings (reference) |
| B — Core Chat | 🔜 Next | Chat, Conversation List, Space AI Config |
| C — Remaining + cleanup | ⬜ Planned | Privacy, Offline Model, Agent, Visual Search Results |
| D — Desktop alignment | ⬜ Planned | Align `chrome://maho-ai` to use shared bundle |

## NATIVE BRIDGE FILES
- Android: `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/WebViewBridge.kt`
- Android host: `maho/android-shell/app/src/main/java/dev/maho/browser/ui/webview/AIWebViewFragment.kt`
- iOS: `maho/ios-shell/Support/WebViewBridgeController.swift`

## RELATED PLANS
- Migration plan: `.omo/plans/maho-mobile-ai-web-migration.md`
