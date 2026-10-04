# maho/web-ai

Unified AI screen bundle for Maho Browser on iOS and Android.

Single Preact + TypeScript source that compiles to `dist/ai-bundle.js` — loaded by `WKWebView` (iOS) and `WebView` (Android) and controlled via a postMessage bridge.

## Build commands

```bash
cd maho/web-ai

bun install           # install deps
bun run dev           # Vite dev server → http://localhost:5173
bun run typecheck     # tsc --noEmit strict check
bun run build         # typecheck + vite build → dist/ + copy to platform dirs
bun run test          # vitest unit tests
```

## Where output goes

| Path | Description |
|------|-------------|
| `dist/index.html` | Entry point |
| `dist/ai-bundle.js` | Main ES module bundle |
| `dist/ai-bundle.css` | Styles |
| `../android-shell/app/src/main/assets/web-ai/` | Android asset copy |
| `../ios-shell/web-ai/` | iOS bundle resource copy |

The copy happens automatically on `bun run build` via `scripts/copy-to-platforms.ts`.

## Hash routing

Navigate via URL hash: `index.html#byok`, `#chat`, `#conversations`, `#offline-model`, `#privacy`, `#space-ai-config`, `#agent`.

Parameters pass as query-style strings after the hash: `#chat?sessionId=abc`.

## Screens

| Hash | Status |
|------|--------|
| `#byok` | ✅ Phase A complete |
| `#chat` | 🔜 Phase B |
| `#conversations` | 🔜 Phase B |
| `#space-ai-config` | 🔜 Phase B |
| `#offline-model` | ⬜ Phase C |
| `#privacy` | ⬜ Phase C |
| `#agent` | ⬜ Phase C |
