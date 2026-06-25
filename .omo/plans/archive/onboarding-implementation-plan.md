# Maho Browser Onboarding / Welcome Screen — Implementation Plan

**Created:** 2026-06-09
**Status:** Ready to implement
**Feature name:** `maho_welcome`
**URL:** `chrome://maho-welcome/`
**Reference:** Zen Browser's onboarding (Firefox fork), adapted to Chromium WebUI + React

---

## Overview

First-run onboarding wizard that guides new users through browser setup: import data, set default browser, choose search engine, customize space theme, and begin browsing. Renders as a full-tab WebUI page (`chrome://maho-welcome/`) opened automatically on first launch.

**Goal:** On first launch -> browser opens with a single tab at `chrome://maho-welcome/` -> user completes 5-step wizard -> tab closes -> normal browsing begins (NTP or restored session on subsequent launches).

---

## Reference: Zen Browser's Onboarding

### Architecture

Zen Browser (Firefox fork) built their onboarding from scratch, bypassing Firefox's `about:welcome`.

- **Trigger:** `zen.welcome-screen.seen` boolean pref, checked at startup in `ZenStartup.mjs`. Set `true` immediately before loading the script (prevents double-show on crash).
- **UI Delivery:** XUL overlay injected into `#browser` div via `Services.scriptloader.loadSubScript()`. Hides all browser elements, shows fullscreen onboarding, restores browser chrome on finish.
- **Layout:** Split-pane wizard card -- left sidebar (40%: titles + buttons), right content area (60%: interactive content per page).
- **Window:** Starts at 875x560, centers on screen, then maximizes when entering multi-page flow.
- **Animation:** Spring physics via `gZenUIManager.motion` (Motion One wrapper). Staggered slide-in/out with blur transitions. Buttons disabled (`pointerEvents: none`) during entry animation.
- **Key files:** `src/zen/welcome/ZenWelcome.mjs`, `zen-welcome.css`, `ZenStartup.mjs`

### Flow (5 Pages)

1. **Splash:** "Welcome to a calmer internet" -- animated title with staggered fade-in + blur
2. **Import:** Import bookmarks/history/passwords + set default browser radio
3. **Search Engine:** Grid of available engines with radio selection
4. **Essentials:** Pick pinned "essential" tabs from preset grid (lazy tab creation with cached favicons via TabStateCache)
5. **Workspace Colors:** Theme picker panel embedded inline
6. **Finish:** Heart animation -> restore browser chrome + toast

### Page Object Pattern

Each page: `{ text[], buttons[], fadeIn(), fadeOut(), dontFadeOut? }`. Controller class `nsZenWelcomePages` manages transitions.

---

## Maho Architecture

```
First launch detected (pref "maho.welcome.completed" == false)
  -> startup_browser_creator_impl patch injects chrome://maho-welcome/ into startup tabs
  -> MahoWelcomeUI serves React WebUI
  -> User proceeds through 6 pages
  -> On completion: PageHandler sets pref true + closes tab + opens NTP
```

**Key design decisions:**
1. **Full-tab WebUI** (not overlay/bubble) -- simplest in Chromium, works with existing patterns
2. **Profile pref** (`maho.welcome.completed`) -- not local_state, so each profile gets onboarding
3. **Startup trigger** -- hook into existing `startup_browser_creator_impl.cc` override to inject the welcome URL as a startup tab when the pref is false
4. **Import/search engine/default browser** -- delegates to existing Chromium C++ APIs (`ImporterHost`, `TemplateURLService`, `shell_integration`)

---

## 1. First-Run Detection & Trigger

### Pref Registration

**File:** `browser/maho_welcome_prefs.h` / `.cc`

```cpp
// maho_welcome_prefs.h
namespace maho::welcome {
inline constexpr char kWelcomeCompleted[] = "maho.welcome.completed";
void RegisterProfilePrefs(PrefRegistrySimple* registry);
}

// maho_welcome_prefs.cc
void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kWelcomeCompleted, false);
}
```

### Startup Hook

**File:** `chromium_src/chrome/browser/ui/startup/startup_browser_creator_impl.cc` (extend existing override)

The existing override already suppresses NTP and enables zero-tab startup. Extend with:

```
// In DetermineStartupTabs(): After the block that suppresses NTP tabs,
// check profile pref `maho.welcome.completed`. If false:
//   tabs.emplace_back(GURL("chrome://maho-welcome/"));
// This makes the welcome page the sole startup tab on first run.
```

### Interaction with Session Restore

When `maho.welcome.completed` is false AND it's the first launch:
- SessionStartupPref defaults to LAST, but there's no previous session data
- The welcome URL injection in DetermineStartupTabs runs before restore logic
- On subsequent launches, the pref is true -> no welcome URL injected -> normal restore flow

---

## 2. Mojo Interface Design

**File:** `browser/ui/webui/maho_welcome/maho_welcome.mojom`

```mojom
module maho_welcome.mojom;

struct BrowserProfile {
  string name;         // "Chrome", "Firefox", "Safari"
  string browser_type; // internal importer ID
  bool available;      // profile exists on this machine
};

struct ImportProgress {
  bool complete;
  string status_message;
};

struct SearchEngineInfo {
  string id;
  string name;
  string icon_url;
  bool is_default;
};

struct SpaceThemePreset {
  string id;
  string name;
  string theme_json;   // serialized SpaceThemeData
};

interface PageHandlerFactory {
  CreatePageHandler(pending_remote<Page> page,
                    pending_receiver<PageHandler> handler);
};

interface PageHandler {
  // Page 2: Import
  GetAvailableBrowsers() => (array<BrowserProfile> browsers);
  StartImport(string browser_type, bool import_bookmarks,
              bool import_history, bool import_passwords) => (bool started);

  // Page 3: Default Browser
  IsDefaultBrowser() => (bool is_default);
  SetAsDefaultBrowser();

  // Page 4: Search Engine
  GetSearchEngines() => (array<SearchEngineInfo> engines);
  SetDefaultSearchEngine(string engine_id);

  // Page 5: Space Theme
  GetThemePresets() => (array<SpaceThemePreset> presets);
  PreviewTheme(string theme_json);
  ApplyTheme(string theme_json);

  // Completion
  CompleteOnboarding();
  SkipOnboarding();
};

interface Page {
  OnImportProgress(ImportProgress progress);
};
```

---

## 3. C++ Backend Files

### Directory: `browser/ui/webui/maho_welcome/`

| File | Responsibility |
|------|---------------|
| `maho_welcome.mojom` | Mojo IDL |
| `maho_welcome_ui.h/.cc` | `MahoWelcomeUIConfig` + `MahoWelcomeUI` (WebUIDataSource setup) |
| `maho_welcome_page_handler.h/.cc` | All Mojo method implementations |
| `BUILD.gn` | `mojom("mojo_bindings")` + `source_set("maho_welcome")` |

### UI Controller Pattern

```cpp
// maho_welcome_ui.cc
MahoWelcomeUIConfig::MahoWelcomeUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoWelcomeHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoWelcomeUI)

MahoWelcomeUI::MahoWelcomeUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/true) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoWelcomeHost);
  webui::SetupWebUIDataSource(source, kMahoWelcomeResources,
                              IDR_MAHO_WELCOME_WELCOME_HTML);
}
```

### PageHandler Key Methods

| Method | Implementation |
|--------|---------------|
| `GetAvailableBrowsers()` | `importer::GetSourceProfiles()` -> map to `BrowserProfile` structs |
| `StartImport()` | Create `ExternalProcessImporterHost`, configure items bitmask, report progress via `page_->OnImportProgress()` |
| `GetSearchEngines()` | `TemplateURLService::GetTemplateURLs()` -> filter prepopulated |
| `SetDefaultSearchEngine()` | `TemplateURLService::SetUserSelectedDefaultSearchProvider()` |
| `IsDefaultBrowser()` | `shell_integration::GetDefaultBrowser()` |
| `SetAsDefaultBrowser()` | `shell_integration::SetAsDefaultBrowser()` |
| `GetThemePresets()` | Return hardcoded 6-9 theme preset JSON blobs |
| `PreviewTheme()` | `MahoSpaceThemeState::SetPreviewOverride()` |
| `ApplyTheme()` | Commit to maho-core via FFI bridge |
| `CompleteOnboarding()` | Set pref true -> close WebContents -> open NTP |
| `SkipOnboarding()` | Set pref true -> close WebContents |

---

## 4. React Frontend Architecture

### Directory Structure

```
browser/resources/maho_welcome/
|-- welcome.html
|-- welcome.css
|-- app.ts                              # TrustedTypes loader
|-- maho_welcome.mojom-webui.js.d.ts
|-- bundle_react.mjs
|-- BUILD.gn
|-- icons/                              # Search engine + browser logos (Lucide SVGs)
+-- react/
    |-- app.tsx                         # Root: createRoot + WelcomeApp
    |-- store.ts                        # WelcomeStore (state + Mojo bridge)
    |-- types.ts                        # WelcomeState, WelcomePage enum
    |-- tsconfig.json
    |-- pages/
    |   |-- welcome-splash.tsx
    |   |-- import-data.tsx
    |   |-- default-browser.tsx
    |   |-- search-engine.tsx
    |   |-- space-theme.tsx
    |   +-- completion.tsx
    |-- components/
    |   |-- wizard-layout.tsx           # Split-pane shell (40/60)
    |   |-- page-indicator.tsx          # Step dots
    |   |-- animated-container.tsx      # CSS transition wrapper
    |   +-- engine-card.tsx             # Search engine card
    +-- __tests__/
        |-- store.test.ts
        +-- pages.test.tsx
```

### Component Tree

```
<WelcomeApp store={store}>
  <WizardLayout currentPage={page} totalPages={6}>
    {/* Left pane: 40% */}
    <LeftPane>
      <PageTitle />
      <PageSubtitle />
      <NavigationButtons onNext onBack onSkip />
      <PageIndicator current={page} total={6} />
    </LeftPane>

    {/* Right pane: 60% */}
    <RightPane>
      <AnimatedContainer direction={direction}>
        {currentPage === 0 && <WelcomeSplash />}
        {currentPage === 1 && <ImportData />}
        {currentPage === 2 && <DefaultBrowser />}
        {currentPage === 3 && <SearchEngine />}
        {currentPage === 4 && <SpaceTheme />}
        {currentPage === 5 && <Completion />}
      </AnimatedContainer>
    </RightPane>
  </WizardLayout>
</WelcomeApp>
```

### Store Design

```typescript
// react/types.ts
export enum WelcomePage {
  Splash = 0,
  Import = 1,
  DefaultBrowser = 2,
  SearchEngine = 3,
  SpaceTheme = 4,
  Completion = 5,
}

export interface WelcomeState {
  currentPage: WelcomePage;
  direction: 'forward' | 'backward';
  animating: boolean;

  // Import page
  availableBrowsers: BrowserProfile[];
  importInProgress: boolean;
  importStatus: string;
  selectedBrowser: string | null;
  importBookmarks: boolean;
  importHistory: boolean;
  importPasswords: boolean;

  // Default browser
  isDefaultBrowser: boolean;

  // Search engine
  searchEngines: SearchEngineInfo[];
  selectedEngine: string | null;

  // Theme
  themePresets: SpaceThemePreset[];
  selectedTheme: string | null;
}
```

```typescript
// react/store.ts
import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_welcome.mojom-webui.js';

type Listener = () => void;

export class WelcomeStore {
  private state: WelcomeState;
  private listeners = new Set<Listener>();
  private handler: PageHandlerRemote;
  private router: PageCallbackRouter;

  constructor() {
    this.router = new PageCallbackRouter();
    this.handler = new PageHandlerRemote();
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.router.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());

    this.router.onImportProgress.addListener(
        (progress: ImportProgress) => {
          this.patch(draft => {
            draft.importInProgress = !progress.complete;
            draft.importStatus = progress.statusMessage;
          });
        });

    this.state = createInitialState();
  }

  getSnapshot(): WelcomeState { return this.state; }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  }

  async bootstrap(): Promise<void> {
    const [browsersResult, enginesResult, presetsResult, defaultResult] =
        await Promise.all([
          this.handler.getAvailableBrowsers(),
          this.handler.getSearchEngines(),
          this.handler.getThemePresets(),
          this.handler.isDefaultBrowser(),
        ]);
    this.patch(draft => {
      draft.availableBrowsers = browsersResult.browsers;
      draft.searchEngines = enginesResult.engines;
      draft.themePresets = presetsResult.presets;
      draft.isDefaultBrowser = defaultResult.isDefault;
      draft.selectedEngine =
          enginesResult.engines.find(e => e.isDefault)?.id ?? null;
    });
  }

  nextPage(): void { /* ... */ }
  prevPage(): void { /* ... */ }
  async startImport(): Promise<void> { /* ... */ }
  async setDefaultBrowser(): Promise<void> { /* ... */ }
  setSearchEngine(id: string): void { /* ... */ }
  selectTheme(id: string): void { /* ... */ }
  async complete(): Promise<void> { /* ... */ }
  async skip(): Promise<void> { /* ... */ }

  private patch(mutator: (draft: WelcomeState) => void): void {
    const draft = {...this.state};
    mutator(draft);
    this.state = draft;
    for (const listener of this.listeners) listener();
  }
}
```

---

## 5. Onboarding Flow (6 Pages)

### Page 1: Welcome Splash
- **Left pane:** "Welcome to Maho" headline with staggered fade-in, tagline, single "Get Started" button
- **Right pane:** Animated Maho logo/illustration (CSS keyframe animation)
- **Behavior:** Auto-advances on button click. No data fetching.

### Page 2: Import Data
- **Left pane:** "Bring your data" headline, description
- **Right pane:** List of detected browsers with checkboxes for data types (bookmarks/history/passwords)
- **Behavior:** `GetAvailableBrowsers()` on bootstrap. Import button -> `StartImport()` -> progress indicator. "Skip" available.

### Page 3: Default Browser
- **Left pane:** "Make Maho your default" headline
- **Right pane:** Large CTA button with current status indicator
- **Behavior:** `IsDefaultBrowser()` check on bootstrap. Button triggers OS dialog via `SetAsDefaultBrowser()`.

### Page 4: Search Engine
- **Left pane:** "Choose your search engine" headline
- **Right pane:** 2x3 grid of search engine cards (icon + name + radio)
- **Behavior:** `GetSearchEngines()` on bootstrap. Selection calls `SetDefaultSearchEngine()`. Pre-selects current default.

### Page 5: Space Theme
- **Left pane:** "Personalize your space" headline, explanation of spaces
- **Right pane:** Grid of 6-9 theme presets with color swatches. Clicking applies live preview via `PreviewTheme()`.
- **Behavior:** `GetThemePresets()` on bootstrap. Final choice persisted on completion.

### Page 6: Completion
- **Left pane:** "You're all set!" headline, "Start Browsing" button
- **Right pane:** Celebratory animation, brief tips
- **Behavior:** "Start Browsing" -> `CompleteOnboarding()` -> pref set -> tab closed

---

## 6. Animation Approach (CSS-Only)

No JS animation libraries. Chromium WebUI runs in a restricted renderer.

### Page Transitions

```css
.page-enter {
  opacity: 0;
  transform: translateX(40px);
}
.page-enter-active {
  opacity: 1;
  transform: translateX(0);
  transition: opacity 300ms ease-out, transform 300ms ease-out;
}
.page-exit-active {
  opacity: 0;
  transform: translateX(-40px);
  transition: opacity 200ms ease-in, transform 200ms ease-in;
}
/* Backward direction reversal */
.page-enter--backward { transform: translateX(-40px); }
.page-exit-active--backward { transform: translateX(40px); }
```

### Splash Animation

```css
.welcome-title span {
  opacity: 0;
  transform: translateY(20px);
  animation: fadeUp 600ms ease-out forwards;
}
.welcome-title span:nth-child(1) { animation-delay: 100ms; }
.welcome-title span:nth-child(2) { animation-delay: 200ms; }

@keyframes fadeUp {
  to { opacity: 1; transform: translateY(0); }
}
```

### Selection Feedback

```css
.engine-card {
  transition: border-color 150ms, box-shadow 150ms, transform 100ms;
}
.engine-card:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
.engine-card.is-selected { border-color: var(--maho-accent); box-shadow: 0 0 0 2px var(--maho-accent-dim); }
```

---

## 7. Build System Integration

### Frontend `browser/resources/maho_welcome/BUILD.gn`

```gn
import("//ui/webui/resources/tools/build_webui.gni")
import("//ui/webui/resources/tools/generate_grd.gni")

assert(!is_android)

action("bundle_react") {
  script = "//third_party/node/node.py"
  inputs = [
    "bundle_react.mjs",
    "react/app.tsx",
    "react/store.ts",
    "react/types.ts",
    # ... all react files
  ]
  outputs = [ "$target_gen_dir/app_bundle.js" ]
  args = [ /* ... */ ]
}

generate_grd("bundle_grdp") {
  grd_prefix = "maho_welcome_bundle"
  out_grd = "$target_gen_dir/maho_welcome_bundle.grdp"
  input_files = [ "app_bundle.js" ]
  input_files_base_dir = rebase_path(target_gen_dir, root_build_dir)
  deps = [ ":bundle_react" ]
}

build_webui("build") {
  grd_prefix = "maho_welcome"
  static_files = [ "welcome.html", "welcome.css" ]
  ts_files = [ "app.ts" ]
  ts_deps = [ "//ui/webui/resources/mojo:build_ts" ]
  webui_context_type = "trusted"
  mojo_files_deps =
      [ "//maho/browser/ui/webui/maho_welcome:mojo_bindings_ts__generator" ]
  mojo_files = [
    "$root_gen_dir/maho/browser/ui/webui/maho_welcome/maho_welcome.mojom-webui.ts",
  ]
  extra_grdp_deps = [ ":bundle_grdp" ]
  extra_grdp_files = [ "$target_gen_dir/maho_welcome_bundle.grdp" ]
}
```

### Backend `browser/ui/webui/maho_welcome/BUILD.gn`

```gn
import("//mojo/public/tools/bindings/mojom.gni")

mojom("mojo_bindings") {
  sources = [ "maho_welcome.mojom" ]
  webui_module_path = "/"
}

source_set("maho_welcome") {
  sources = [
    "maho_welcome_page_handler.cc",
    "maho_welcome_page_handler.h",
    "maho_welcome_ui.cc",
    "maho_welcome_ui.h",
  ]
  public_deps = [ ":mojo_bindings", "//base", "//content/public/browser" ]
  deps = [
    "//chrome/browser/importer",
    "//chrome/browser/profiles:profile",
    "//chrome/browser/search_engines",
    "//chrome/browser/shell_integration",
    "//components/prefs",
    "//components/search_engines",
    "//maho/components/constants",
    "//mojo/public/cpp/bindings",
    "//ui/webui",
    "//url",
  ]
}
```

---

## 8. Testing Strategy

### C++ Unit Tests (`maho_welcome_page_handler_unittest.cc`)

| Test | Verifies |
|------|----------|
| `GetAvailableBrowsers_ReturnsDetectedProfiles` | Correct importer mapping |
| `StartImport_CallsImporterHost` | Import items bitmask |
| `StartImport_ReportsProgressViaPage` | `page_->OnImportProgress()` called |
| `SetDefaultSearchEngine_UpdatesTemplateURLService` | Mock TemplateURLService |
| `CompleteOnboarding_SetsPrefAndClosesTab` | Pref written + WebContents closed |
| `SkipOnboarding_SetsPrefWithoutApplying` | Pref set, no theme/import |

### Browser Tests (`maho_welcome_browsertest.cc`)

| Test | Verifies |
|------|----------|
| `FirstRun_OpensWelcomePage` | Fresh profile -> chrome://maho-welcome/ |
| `SecondRun_DoesNotOpenWelcome` | After completion -> normal startup |
| `CompleteFlow_SetsAllPrefs` | All settings applied |
| `SkipFlow_MarksCompleted` | Skip correctly marks pref |

### Frontend Tests (vitest)

| Test file | Verifies |
|-----------|----------|
| `store.test.ts` | State transitions, navigation, bootstrap |
| `pages.test.tsx` | Component rendering, interactions |

### Manual QA Checklist

- [ ] Fresh profile launches welcome page
- [ ] All 6 pages navigable forward/backward
- [ ] Import detects installed browsers correctly
- [ ] Default browser prompt triggers OS dialog
- [ ] Search engine change persists after completion
- [ ] Theme preview applies live to browser chrome
- [ ] Completion closes tab and browser is usable
- [ ] Skip at any point marks onboarding complete
- [ ] Subsequent launches don't show welcome
- [ ] Keyboard navigation works (Tab/Enter/Escape)
- [ ] Page is responsive at different window sizes

---

## 9. Implementation Order (Phased)

### Phase 1: Skeleton (Commits 1-5) -- 3 Parallel Streams

#### Stream A: C++ Backend Skeleton (Commits 1-3)

**Commit 1:** `feat(welcome): add URL constants and pref registration`
- `components/constants/webui_url_constants.h` -- add constants
- `browser/maho_welcome_prefs.h/.cc` -- pref registration
- Wire into existing `RegisterProfilePrefs` aggregate

**Commit 2:** `feat(welcome): add mojo IDL and UI controller skeleton`
- All `browser/ui/webui/maho_welcome/` files with stub implementations
- `BUILD.gn`

**Commit 3:** `feat(welcome): register WebUI in interface binders`
- Update `chromium_src/.../chrome_browser_interface_binders_webui_parts_desktop.cc`

#### Stream B: Frontend Skeleton (Commit 4)

**Commit 4:** `feat(welcome): add frontend skeleton with build system`
- All `browser/resources/maho_welcome/` skeleton files
- Renders "Welcome to Maho" placeholder

#### Stream C: First-Run Trigger (Commit 5)

**Commit 5:** `feat(welcome): hook first-run detection into startup`
- Extend `chromium_src/.../startup_browser_creator_impl.cc`

### Phase 2: Page Implementation (Commits 6-11) -- Sequential

**Commit 6:** `feat(welcome): implement splash page with animations`
**Commit 7:** `feat(welcome): implement import data page`
**Commit 8:** `feat(welcome): implement default browser page`
**Commit 9:** `feat(welcome): implement search engine page`
**Commit 10:** `feat(welcome): implement space theme page`
**Commit 11:** `feat(welcome): implement completion page and flow finish`

### Phase 3: Polish & Tests (Commits 12-14)

**Commit 12:** `feat(welcome): add page transition animations and polish`
**Commit 13:** `test(welcome): add C++ unit tests`
**Commit 14:** `test(welcome): add browser tests and frontend tests`

**Invariant:** Each commit is independently buildable.

---

## 10. File Inventory

### New Files (35 total)

| Path | Type | Phase |
|------|------|-------|
| `browser/maho_welcome_prefs.h` | C++ header | 1A |
| `browser/maho_welcome_prefs.cc` | C++ source | 1A |
| `browser/ui/webui/maho_welcome/maho_welcome.mojom` | Mojo IDL | 1A |
| `browser/ui/webui/maho_welcome/maho_welcome_ui.h` | C++ header | 1A |
| `browser/ui/webui/maho_welcome/maho_welcome_ui.cc` | C++ source | 1A |
| `browser/ui/webui/maho_welcome/maho_welcome_page_handler.h` | C++ header | 1A |
| `browser/ui/webui/maho_welcome/maho_welcome_page_handler.cc` | C++ source | 1A |
| `browser/ui/webui/maho_welcome/BUILD.gn` | Build config | 1A |
| `browser/resources/maho_welcome/welcome.html` | HTML | 1B |
| `browser/resources/maho_welcome/welcome.css` | CSS | 1B |
| `browser/resources/maho_welcome/app.ts` | TypeScript | 1B |
| `browser/resources/maho_welcome/maho_welcome.mojom-webui.js.d.ts` | TS declarations | 1B |
| `browser/resources/maho_welcome/bundle_react.mjs` | Build script | 1B |
| `browser/resources/maho_welcome/BUILD.gn` | Build config | 1B |
| `browser/resources/maho_welcome/react/tsconfig.json` | TS config | 1B |
| `browser/resources/maho_welcome/react/types.ts` | TypeScript | 1B |
| `browser/resources/maho_welcome/react/store.ts` | TypeScript | 1B |
| `browser/resources/maho_welcome/react/app.tsx` | React TSX | 1B |
| `browser/resources/maho_welcome/react/pages/welcome-splash.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/pages/import-data.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/pages/default-browser.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/pages/search-engine.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/pages/space-theme.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/pages/completion.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/components/wizard-layout.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/components/page-indicator.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/components/animated-container.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/react/components/engine-card.tsx` | React TSX | 2 |
| `browser/resources/maho_welcome/icons/*.svg` | SVG icons | 2 |
| `browser/resources/maho_welcome/vitest.config.ts` | Test config | 3 |
| `browser/resources/maho_welcome/react/__tests__/store.test.ts` | Test | 3 |
| `browser/resources/maho_welcome/react/__tests__/pages.test.tsx` | Test | 3 |
| `browser/ui/webui/maho_welcome/maho_welcome_page_handler_unittest.cc` | Test | 3 |
| `browser/ui/webui/maho_welcome/maho_welcome_browsertest.cc` | Test | 3 |

### Modified Files (5 total)

| Path | Change | Phase |
|------|--------|-------|
| `components/constants/webui_url_constants.h` | Add `kMahoWelcomeHost` + `kMahoWelcomeURL` | 1A |
| `chromium_src/.../startup_browser_creator_impl.cc` | Extend override for welcome injection | 1C |
| `chromium_src/.../chrome_browser_interface_binders_webui_parts_desktop.cc` | Add Mojo binding entry | 1A |
| Pref registration aggregate file | Add `maho::welcome::RegisterProfilePrefs` call | 1A |
| Top-level `browser/BUILD.gn` or source set aggregate | Add `:maho_welcome` dependency | 1A |

---

## 11. Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| Import APIs changed in upstream Chromium | Check `chrome/browser/importer/` first; adapt or fall back to simplified import |
| Shell integration may not work on all platforms | Feature-detect via `GetDefaultBrowser()`. If `UNKNOWN`, hide the Default Browser page |
| Welcome tab racing with session restore | Welcome URL injection in `DetermineStartupTabs()` runs before restore; no race |
| Theme preview conflicts with active space state | Use isolated preview path (`SetPreviewOverride` from `maho_space_create`) |
| GN dependency cycles | Keep deps minimal. No deps on sidebar, AI, or other WebUI features |

---

## 12. Open Questions

1. **Per-profile vs per-installation?** Current: profile pref. New profile = re-shows onboarding. If installation-level preferred, use `local_state` instead.

2. **Window sizing:** Zen resizes to 875x560. Recommendation: don't resize -- make WebUI responsive with min-width/min-height on wizard container.

3. **Theme page scope:** Simplified preset grid (6-9 presets) vs full space-create picker. Recommendation: simplified grid for onboarding + "Customize more in Settings" note.

4. **Import granularity:** Per-data-type toggles vs single "Import everything". Recommendation: per-type toggles for user control.

5. **Search engine list source:** `TemplateURLService::GetTemplateURLs()` (respects locale) vs hardcoded list. Recommendation: prepopulated engines from TemplateURLService.
