# macos-shell

## OVERVIEW

Native macOS shell (Swift/AppKit). Wraps maho-core via FFI. Handles UI, WebView, LLM integration, settings. 30 source directories, ~22 AI module files.

## STRUCTURE

| Directory | Role |
|-----------|------|
| `AI/` | LLM integration — providers, streaming, MCP, secure storage |
| `App/` | AppDelegate, lifecycle |
| `Bridge/` | FFI bridge to maho-core (MahoBridge) |
| `Settings/` | Preferences UI (AI, general, etc.) |
| `Sidebar/` | Sidebar panels including LiveFolders |
| `CommandBar/` | Omnibox / command bar UI |
| `Window/` | Window management |
| `Shared/` | Shared utilities, colors, extensions |

### AI Module (`Sources/AI/`)

| File | Role |
|------|------|
| `SecureLocalStore.swift` | AES-256-GCM encrypted local file store (replaces Keychain) |
| `APIKeyStore.swift` | API key management — delegates to SecureLocalStore |
| `LLMService.swift` | LLM provider orchestration, streaming |
| `AIChatHandler.swift` | Chat session management |
| `AIChatView.swift` | Chat UI |
| `MCPServer.swift` | MCP server integration |
| `MCPConnection.swift` | MCP WebSocket connection |
| `MCPToolRegistry.swift` | MCP tool registration |
| `InlineEditHandler.swift` | In-page AI editing |
| `PageContextExtractor.swift` | Page content extraction for LLM context |

## WHERE TO LOOK

| Task | File |
|------|------|
| Secret/credential storage | `AI/SecureLocalStore.swift` |
| API key CRUD | `AI/APIKeyStore.swift` |
| Add new LLM provider | `AI/LLMService.swift` + new provider file |
| AI settings UI | `Settings/AISettingsView.swift` |
| LiveFolder tokens | `Sidebar/LiveFolders/LiveFolderManager.swift` → SecureLocalStore |
| Core FFI calls | `Bridge/` |

## CONVENTIONS

- All secrets stored in local encrypted file (`~/Library/Application Support/Maho/secure_store.enc`) — never Keychain
- SecureLocalStore: AES-256-GCM via CryptoKit, HKDF-SHA256 key derivation from hardware UUID
- APIKeyStore wraps SecureLocalStore with `apikey.{provider}` key prefix
- LLMService delegates key lookup to APIKeyStore — no direct storage access
- Shell-only data (API keys, provider config) stays in Shell — never sent to Core
- Core data flows through MahoBridge FFI

## ANTI-PATTERNS

- ❌ macOS Keychain (Security.framework) — use SecureLocalStore
- ❌ `import Security` — removed, do not re-add
- ❌ `SecItemAdd` / `SecItemCopyMatching` / `SecItemDelete` — forbidden
- ❌ Direct Keychain access from any file — always go through SecureLocalStore
- ❌ Storing secrets in UserDefaults or plain files
- ❌ Passing API keys to maho-core — Shell-only concern

## WINDOW STARTUP SHRINK BUG

- Root cause: `Sources/Window/BrowserWindowController.swift` `setupSplitView()` assigned `splitView` directly to `window.contentView`, and AppKit let split-view fitting/layout negotiation shrink the startup window from the intended `1200x800`.
- Rejected as root cause: `SidebarAddressBarView` and delegate timing theories. They were investigated, but the wrapperless content view assignment was the real issue.
- Final fix: keep `splitView` inside a plain `contentViewWrapper` `NSView` before assigning it to `window.contentView`. Quartz verification after this change showed a visible `1200x800` `Maho` window.
- Do not simplify this away, and do not remove the wrapper-contentView pattern.
## VISUAL REFERENCE

Screenshots for Arc vs Maho sidebar comparison live in `docs/screenshots/`.

### Arc Reference (`docs/screenshots/arc/`)

| File | Content |
|------|---------|
| `sidebar.jpeg` | Full Arc sidebar — favorites grid, pinned tabs, regular tabs |
| `overview.jpeg` | Arc full window — sidebar width ratio, color scheme |
| `empty-favorites.png` | Arc empty favorites placeholder (dashed outline card) |
| `palette-tab-active.png` | Command palette overlay on active tab |
| `command-palette.jpeg` | Arc command palette |
| `settings.png` | Arc settings window |
| `SCR-20260418-tgxd.png` | Favorites — single tile detail |
| `SCR-20260418-tgyo.png` | Favorites — 2 tiles, 2-column grid |
| `SCR-20260418-tgzs.png` | Favorites — 3+ tiles layout |
| `SCR-20260418-thau.png` | Favorites — expanded grid |
| `SCR-20260418-thbt.png` | Favorites — full grid with many tiles |
| `SCR-20260418-thdm.png` | Favorites — section detail |
| `SCR-20260418-theq.png` | Sidebar — pinned section, 3-column tile grid |
| `SCR-20260418-thft.png` | Sidebar — compact tab tile grid, spacing detail |

### Maho Current (`docs/screenshots/maho/`)

| File | Content |
|------|---------|
| `sidebar-current.png` | Maho sidebar — tree view, folders, tabs, address bar |
| `latest.png` | Latest Maho sidebar state (post address bar implementation) |
