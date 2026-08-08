# Arc Space test matrix

This matrix is the execution companion for `docs/arc-space-parity.md`. It is written to drive implementation and verification of Arc-style Space parity without guessing at undefined behavior.

## Evidence labels

- **Confirmed** means the expected behavior is already established by the repo evidence in this conversation.
- **Inferred** means the expected outcome follows from the confirmed contract, but the repo evidence does not directly prove it.
- **Unknown** means the case exposes an ambiguity that implementation work must not silently resolve.

## Pass and fail fields

Each scenario below should be tracked with these fields during execution:

- **Status**: `pass`, `fail`, or `blocked`
- **Expected outcome**: short statement of the user-visible result
- **Notes**: any mismatch, regression, or ambiguity found

## Functional scenarios

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| F-01 | Switch to a different Space from the main shell | Confirmed | Multiple Spaces exist | Select Space B | Active Space becomes B, visible tabs update, core state reflects the switch |  |
| F-02 | Rename a Space | Confirmed | A Space exists with a visible name | Trigger rename, enter new label, confirm | Name updates everywhere it is shown and persists |  |
| F-03 | Reorder Spaces | Confirmed | Two or more Spaces exist | Move Space C before Space A | Order changes, identities stay the same, active Space remains correct |  |
| F-04 | Open a tab into a selected Space from Maho Mini | Confirmed | Maho Mini is open and multiple Spaces exist | Use the `⌘⌥O` flow and pick a Space | The tab opens in the selected Space |  |
| F-05 | Open in current Space from Maho Mini | Confirmed | Maho Mini is open | Use the current Space open flow | Tab opens in the current global active Space |  |
| F-06 | Active Space persists after relaunch | Inferred | App state has at least one switched Space | Quit and relaunch | The same active Space is restored |  |

## Edge cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| E-01 | Rename to an empty string | Confirmed | Rename prompt is available | Submit empty name | Rename is rejected with a validation error and the current Space name remains unchanged |  |
| E-02 | Rename to a duplicate name | Confirmed | Two Spaces with distinct names | Rename one Space to match the other | Rename is rejected with a duplicate-name validation error and both Space names remain unchanged |  |
| E-03 | Reorder while a Space is active | Confirmed | A Space is active | Move another Space around it | Active Space does not unexpectedly change unless explicitly selected |  |
| E-04 | Switch Space while a rename prompt is open | Inferred | Rename UI is open | Trigger a Space change elsewhere | Prompt state must not corrupt core state or lose the rename operation unexpectedly |  |
| E-05 | Use Maho Mini when only one Space exists | Inferred | Single Space profile | Open the Space picker flow | Picker should still behave consistently, even if the target set is trivial |  |

## Regression cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| R-01 | Duplicate core update emission does not cause double UI refresh | Confirmed | Instrument update flow | Perform one Space change | Only one logical state transition is observed in shell |  |
| R-02 | Reorder does not recreate Space identity | Confirmed | Space order changes | Move a Space and inspect references | The same Space IDs remain in use after reorder |  |
| R-03 | Rename does not overwrite unrelated Space config | Inferred | A Space has editable metadata | Rename a Space | Only the intended fields change |  |
| R-04 | Space change does not break normal tab state | Inferred | Several tabs exist in a Space | Switch to another Space and back | Tab state is preserved and still associated with the correct Space |  |

## Keyboard cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| K-01 | `⌘⌥O` opens the Space picker in Maho Mini | Confirmed | Maho Mini window is open | Press `⌘⌥O` | The Space selection menu appears |  |
| K-02 | `⌘O` opens in the current Space | Confirmed | Maho Mini window is open | Press `⌘O` | Content opens in the current active Space |  |
| K-03 | Keyboard selection chooses the right Space | Inferred | Space picker is visible | Use keyboard navigation to select a Space | Selected target Space receives the action |  |
| K-04 | Keyboard actions respect the global active-space model | Confirmed | Multiple normal windows are open | Change Space in one window, observe the other | Both windows remain aligned to the same active Space model |  |

## Maho Mini cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| L-01 | Maho Mini shows available Spaces | Confirmed | Multiple Spaces exist | Open the `⌘⌥O` flow | Picker lists the available Spaces |  |
| L-02 | Maho Mini targets the chosen Space, not the previous one | Confirmed | A different Space is currently active | Open picker and choose another Space | Content opens in the chosen Space |  |
| L-03 | Maho Mini follows reordered Space order | Inferred | Spaces have been reordered | Open the picker | Picker order matches the canonical order |  |
| L-04 | Maho Mini respects renamed Space labels | Confirmed | A Space has been renamed | Open the picker | The new label is shown consistently |  |

## Persistence and restore cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| P-01 | Active Space survives app relaunch | Inferred | A non-default Space is active | Quit and relaunch | Same active Space is restored |  |
| P-02 | Reordered Space list survives app relaunch | Inferred | Spaces have been reordered | Quit and relaunch | Order remains the same |  |
| P-03 | Renamed Space survives app relaunch | Inferred | A Space has a custom label | Quit and relaunch | Name remains intact |  |
| P-04 | Profile-linked Space state survives profile reopen | Inferred | Profile linkage is active | Close and reopen the profile context | Space state is restored for that profile |  |

## Multi-window cases

| ID | Scenario | Label | Setup | Steps | Expected outcome | Status |
|---|---|---|---|---|---|---|
| M-01 | Two normal windows share the same active Space | Confirmed | Two normal windows are open | Switch Space in window A | Window B reflects the same global active Space model |  |
| M-02 | Space changes stay in sync across windows | Confirmed | Two normal windows are open | Rename or reorder a Space in one window | The other window shows the same canonical Space state |  |
| M-03 | Window isolation is not introduced in this wave | Confirmed | Multiple normal windows are open | Compare behavior after Space switches | No per-window isolated Space model appears for normal windows |  |
| M-04 | Blank-window behavior is not tested in this wave | Confirmed | N/A | N/A | Deferred to the future Blank-window track |  |

## Acceptance checks

| ID | Check | Label | Expected outcome | Status |
|---|---|---|---|---|
| A-01 | Rename prompt path is wired end to end | Confirmed | Rename prompt changes core state and shell presentation correctly |  |
| A-02 | Reorder UI path is wired end to end | Confirmed | Reorder action updates order, persists it, and refreshes UI once |  |
| A-03 | `spaceOrderChanged` consumer is live | Confirmed | Reorder emits one canonical update and the consumer handles it |  |
| A-04 | Icon and config edit path works | Confirmed | Space metadata edits persist and round-trip cleanly |  |
| A-05 | Duplicate core update emission is removed | Confirmed | One user action produces one logical update path |  |

## Notes for execution

- If a case is marked `unknown`, the right result is to stop and record the ambiguity, not to invent a new rule.
- If a case fails because the behavior is not implemented yet, keep the failure visible. Do not rewrite the matrix to hide the gap.
- Implementation subagents should use this matrix to verify the contract in `docs/arc-space-parity.md` line by line.
