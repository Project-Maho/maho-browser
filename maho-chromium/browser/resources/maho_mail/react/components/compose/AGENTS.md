# COMPOSE KNOWLEDGE BASE

**Scope:** `src/components/compose/`

## OVERVIEW
Compose flow for new mail, replies, forwards, drafts, and the popout compose window. The modal and window variants share the same editing concepts, so keep orchestration and editor behavior aligned.

## STRUCTURE
```
compose/
├── ComposeModal.tsx       # In-app compose surface
├── ComposeWindow.tsx      # Popout compose window
├── RichTextEditor.tsx     # Tiptap editor wrapper
└── ContactAutocomplete.tsx # Recipient lookup/autocomplete
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Compose orchestration | `ComposeModal.tsx` | Reply/forward/new/delegate/send-later paths |
| Popout compose shell | `ComposeWindow.tsx` | Window-hosted variant used by `main.tsx` |
| Editor behavior | `RichTextEditor.tsx` | Formatting, attachments, HTML/body handling |
| Recipient lookup | `ContactAutocomplete.tsx` | Contacts/groups resolution for To/Cc/Bcc |

## CONVENTIONS
- Keep send/draft state transitions consistent between modal and window.
- Use the existing editor wrapper rather than embedding raw editor setup in feature parents.
- Compose logic should stay here; only orchestration/selection belongs in `App.tsx`.

## ANTI-PATTERNS
- No mixing recipient lookup, editor setup, and send transport in one component when a smaller boundary exists.
- No new compose path that bypasses the shared modal/window contract.
- No duplicating reply/forward body initialization logic across multiple files.

## NOTES
- The modal and popout compose views are intentionally separate entry points, but they should share behavior and error handling.
- This folder is a good place to check first when send/draft regressions appear.
