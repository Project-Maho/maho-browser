# HOOKS KNOWLEDGE BASE

**Scope:** `src/hooks/`

## OVERVIEW
React state and lifecycle orchestration for the app shell. This folder contains the mail client state machine, cross-window sync, platform detection, mobile navigation, and the smaller behavior hooks that App.tsx composes together.

## STRUCTURE
```
hooks/
├── useMailClient.ts        # Mail/accounts/folder state machine
├── usePopoutWindow.ts      # Reader/compose window coordination
├── useAutoSync.ts          # Background sync listener wiring
├── useHotkeys.ts           # Keyboard shortcut registration
├── useMobileNavigation.ts  # Mobile screen stack
├── useTheme.ts / useSettings.ts / useTranslation.ts
├── useAppLifecycle.ts / useAppUpdate.ts / useAuthStatus.ts
├── useNetworkStatus.ts / useNotifications.ts / useTrayBadge.ts
├── useDeepLink.ts / useEmailSecurity.ts
└── *.test.ts[x]            # Hook tests, mostly co-located
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Mail state and pagination | `useMailClient.ts` | Selection, aggregate inbox cursors, selection mutations |
| Popout window bridge | `usePopoutWindow.ts` | Main/reader/compose window coordination and emitted updates |
| Keyboard shortcuts | `useHotkeys.ts` | App-level keybinding registry |
| Mobile navigation | `useMobileNavigation.ts` | Screen stack for drawer/list/detail/compose/settings |
| Background sync | `useAutoSync.ts` | Tauri sync events and notification hooks |
| Platform / safety | `useEmailSecurity.ts`, `useNetworkStatus.ts`, `useTheme.ts` | Security and environment-sensitive hooks |

## CONVENTIONS
- Keep stateful orchestration in hooks, not in `App.tsx`.
- Prefer small, purpose-specific hooks over shared catch-all state containers.
- Tests usually live beside the hook; `__tests__/` is used when the hook file would get noisy.
- `useMailClient.ts` is the central coordination point; changes there usually ripple into `App.tsx` and the layout components.

## ANTI-PATTERNS
- No direct Tauri calls from components when a hook already owns the behavior.
- No mixing hook logic with visual component code.
- No new hook that duplicates `useMailClient` selection or pagination logic without a clear boundary.

## NOTES
- `useHotkeys.test.tsx` exists outside `__tests__/` and is the lone exception to the usual test layout.
- Mobile-specific hooks and desktop/system hooks intentionally live together; split only if one side grows a clear subdomain.
