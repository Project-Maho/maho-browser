# Maho Icon System Cross-Platform Audit (2026)

This document maps all UI icons currently in use across the four non-WebUI Maho surfaces: Chromium Views (C++), macOS AppKit Shell, iOS SwiftUI/UIKit Shell, and Android Jetpack Compose Shell.

## 1. Cross-Platform Semantic Mapping Table

| Semantic Name | Views (C++ / Windows / Linux) | macOS Shell (AppKit) | iOS Shell (SwiftUI/UIKit) | Android Shell (Compose) | Description / Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **nav_back** | `vector_icons::kBackArrowIcon` | `"chevron.left"` | `"chevron.backward"`, `"arrow.backward"` | `Icons.AutoMirrored.Filled.ArrowBack` | Navigate back in history |
| **nav_forward** | `vector_icons::kSubmenuArrowChromeRefreshIcon` | `"chevron.right"` | `"chevron.forward"`, `"arrow.forward"` | `Icons.AutoMirrored.Filled.ArrowForward` | Navigate forward in history |
| **close** | `vector_icons::kCloseChromeRefreshIcon` | `"xmark"`, `"xmark.circle"` | `"xmark"`, `"xmark.circle.fill"`, `"xmark.circle"` | `Icons.Default.Close` | Close tab, dialog, sheet, or overlay |
| **reload_sync** | `vector_icons::kSyncChromeRefreshIcon` | `"arrow.clockwise"`, `"arrow.triangle.2.circlepath"` | `"arrow.clockwise"`, `"arrow.triangle.2.circlepath"` | `Icons.Default.Refresh`, `Icons.Filled.Sync` | Reload page or trigger data sync |
| **search** | `vector_icons::kSearchIcon`, `vector_icons::kSearchChromeRefreshIcon` | `"magnifyingglass"` | `"magnifyingglass"`, `"doc.text.magnifyingglass"` | `Icons.Default.Search` | Search bar, search input, query triggers |
| **globe** | `vector_icons::kGlobeIcon` | `"globe"` | `"globe"` | `Icons.Default.Language` | Default site favicon, Web indicator |
| **downloads** | `vector_icons::kFileDownloadOffChromeRefreshIcon` | `"arrow.down.circle"`, `"arrow.down.to.line"` | `"arrow.down.circle"` | `Icons.Filled.Download` | Download manager access and status |
| **archive** | `kArchiveboxIcon` (custom) | `"archivebox"` | `"archivebox"` | `Icons.Filled.Schedule` | Archived tabs library / history section |
| **spaces** | `vector_icons::kHomeIcon` | `"square.grid.2x2"`, `"square.on.square"` | `"square.on.square"` | `Icons.Default.Tab` (bottom bar context), `Icons.Filled.Folder` | Space Switcher / spaces organization |
| **boosts** | `vector_icons::kScienceIcon` | `"bolt"` | `—` | `Icons.Default.AutoAwesome` (Max AI / Boosts) | Boosts and custom AI actions |
| **media** | `vector_icons::kVideoLibraryIcon` | `"photo.on.rectangle"`, `"photo.on.rectangle.angled"` | `—` | `—` | Media library and player UI |
| **easels** | `vector_icons::kStickyNote2Icon` | `"paintbrush.pointed"`, `"pencil.and.scribble"` | `—` | `—` | Easels drawing and canvas tool |
| **notes** | `vector_icons::kStickyNote2Icon` | `"note.text"` | `"note.text"` | `Icons.Filled.NoteAlt` | Notes section |
| **bookmarks** | `vector_icons::kStarIcon` | `"star.fill"`, `"star"` | `"bookmark"`, `"star.fill"` | `Icons.Filled.Bookmark`, `Icons.Filled.BookmarkBorder` | Bookmarks library and item favorites |
| **history** | `vector_icons::kSearchIcon` | `"clock"` | `"clock"` | `Icons.Filled.History`, `Icons.Default.AccessTime` | Browser history access |
| **reading_list** | `—` | `"eyeglasses"` | `"eyeglasses"` | `Icons.Filled.Visibility` | Reading list section |
| **settings** | `—` | `"gearshape"`, `"gearshape.2"` | `"gearshape"`, `"gearshape.2"` | `Icons.Filled.Settings`, `Icons.Filled.Tune` | App settings and advanced settings |
| **profiles** | `—` | `"person.2"`, `"person.crop.circle"` | `"person.crop.circle"` | `Icons.Filled.Code`, `Icons.Filled.Person` | Profiles selector and manager |
| **notifications** | `—` | `"bell"` | `"bell"` | `Icons.Filled.Notifications` | Push notifications configuration |
| **autofill_key** | `—` | `"key"` | `"key"` | `Icons.Filled.Key` | Autofill passwords and forms settings |
| **privacy** | `—` | `"shield"`, `"shield.lefthalf.filled"` | `"shield"` | `Icons.Filled.Security` | Privacy, security, content blocker |
| **help_feedback** | `—` | `—` | `"envelope"` | `Icons.Filled.Feedback` | Feedback screen, user support |
| **clear_sweep** | `—` | `—` | `"arrow.counterclockwise"` | `Icons.Filled.ClearAll`, `Icons.Filled.DeleteSweep`, `Icons.Filled.RestartAlt` | Clear history, reset settings, sweep tabs |
| **delete** | `—` | `"trash"` | `"trash"` | `Icons.Filled.Delete` | Trash / delete action |
| **share** | `—` | `—` | `"square.and.arrow.up"` | `Icons.Filled.Share` | System share sheet trigger |
| **more_actions** | `—` | `"ellipsis.circle"` | `"ellipsis.circle"` | `Icons.Filled.MoreVert` | More menu / overflow actions |
| **info_lock** | `—` | `—` | `"lock.fill"`, `"lock.open"` | `Icons.Default.Lock` | Address bar security status indicator |
| **audio_indicator** | `vector_icons::kVolumeUpChromeRefreshIcon` / `vector_icons::kVolumeOffChromeRefreshIcon` | `"speaker.slash.fill"` | `"speaker.wave.2"`, `"speaker.slash"`, `"speaker.wave.2.fill"`, `"speaker.slash.fill"` | `—` | Tab audio and mute indicator |
| **sparkles_ai** | `—` | `"sparkles"`, `"sparkles.rectangle.stack"` | `"sparkles"` | `Icons.Default.AutoAwesome` | AI Panel trigger and onboarding |

## 2. Platform-Specific Exclusions

* **App Icons / Launcher Icons**: Excluded per scope.
  * macOS: `macos-shell/Resources/AppIcon.icns`
  * iOS: `ios-shell/Assets.xcassets/AppIcon.appiconset`
  * Android: `android-shell/app/src/main/res/mipmap-*`
* **Dynamic Web Icons**: Per-site favicons and thumbnails are fetched dynamically at runtime and not part of the static UI assets.
* **WebUI Icons**: Astro website and Chromium WebUI SVG files under `maho-chromium/browser/resources/` are out of scope.
