# ARC-Library Active Item 001, Library Rail Breathing Room

**Item ID:** ARC-LIBRARY-ACTIVE-ITEM-001

**Title:** Library rail breathing-room polish

**Status:** Active execution item

**Binding scope:** This item is only for a narrow visual polish pass on the Library rail so it feels less cramped while preserving the current verified structure, including the fact that the first rail item now reads `Archive`.

---

## 1. Exact problem statement

The current runtime baseline shows that the Archive/Library structure is in place and the first rail item is already `Archive`, which confirms the rail ordering problem is solved. The remaining visible debt is that the Library rail still feels tight and narrow overall.

This item exists to make a small, bounded breathing-room improvement to the Library rail presentation only. It must not reopen shell layout, body mode behavior, archive pane design, container behavior, or width-token policy.

---

## 2. Allowed files

Only the Library rail files may be edited for this item:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

If an implementation idea requires any other file, that is a stop condition, not a justification to expand scope.

---

## 3. Explicitly forbidden files

The following files and file groups are out of scope for this item and must not be edited:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`
- any downloads, media, action-pane, or unrelated sidebar feature files
- any tests, build files, docs, or harness files unless this item is explicitly stopped and replanned first

Forbidden scope also includes any shell, body-mode, archive-pane, container, width-token, or unrelated compile-fix work.

---

## 4. Required evidence before completion

Before this item can be closed, the following evidence must exist:

1. `lsp_diagnostics` on the modified Library rail file(s) with zero errors.
2. A runtime screenshot or equivalent artifact showing the Library rail after the polish.
3. A brief confirmation that the first rail item still reads `Archive` and that the Archive/Library structure remains intact.
4. If the change needs any validation beyond the rail files, the item must stop and replan instead of borrowing extra scope.

Accepted baseline evidence for context may include:

- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-archive-verify-20260510f/library.png`
- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-archive-verify-20260510f/archive.png`
- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-launch-sanity-20260510/launch.log`

Those paths are reference context only. They do not expand the allowed file set.

---

## 5. Stop and replan triggers

Stop immediately and replan if any of the following happens:

- a proposed change needs `maho_sidebar_view.*`
- a proposed change needs `maho_sidebar_container_view.cc`
- a proposed change needs `maho_sidebar_layout_tokens.h`
- a proposed change needs `maho_sidebar_archive_view.*`
- the work starts turning into shell spacing, container width, body-mode, or archive-pane redesign
- the rail polish cannot be achieved without touching files outside the allowed file list
- the visible issue turns out to be a width-token or container-behavior problem instead of a rail-only polish problem
- any unrelated compile blocker appears and would require touching files outside the allowed list

A stop condition is not a failure. It is a required handoff to a new plan item.

---

## 6. Step-by-step execution sequence

1. Reconfirm the current baseline mentally before editing: Library exists, the first item is `Archive`, and the only remaining debt is rail breathing room.
2. Open only the allowed Library rail file(s).
3. Make the smallest possible rail-only polish that increases perceived breathing room without changing shell geometry or width tokens.
4. Keep the implementation localized to the rail presentation code, not the surrounding sidebar shell.
5. Do not introduce new behavior, new modes, or new container rules.
6. After the edit, run diagnostics on the modified file(s).
7. Capture or reference a runtime artifact that shows the Library rail after the polish.
8. Verify that `Archive` is still the first rail item and that no other sidebar surface changed.
9. If any step requires an out-of-scope file, stop and replan instead of forcing the change.

---

## 7. Acceptance criteria

This item is complete only if all of the following are true:

- the Library rail feels visibly less cramped in the runtime artifact
- the first rail item still reads `Archive`
- the Archive/Library structure is unchanged
- only the allowed Library rail file(s) were edited
- no shell, container, width-token, or archive-pane file was changed
- diagnostics on the changed file(s) are clean
- the change is small, local, and obviously tied to rail breathing room only

---

## 8. What counts as failure or drift

This item fails if any of the following occurs:

- any file outside the allowed file list is edited
- the work expands into shell, body-mode, container, archive-pane, or width-token changes
- the item is used to clean up unrelated compile errors
- the result changes rail ordering, and especially if `Archive` stops being the first rail item
- the change is broad enough that it no longer reads as a single rail breathing-room polish step
- the implementation depends on a hidden workaround or an unlisted side effect
- completion is claimed without the required diagnostics and runtime evidence

If drift occurs, the correct response is to stop, record the blocker, and replan a new item. Do not patch around the drift inside this item.
