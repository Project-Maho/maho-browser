# Maho Release Notes Specification

This directory serves as the single source of truth (SSOT) for all Maho release changelogs and release notes.

Each release note is stored as an individual Markdown file named after its semantic tag (e.g. `v1.0.0.md`).

## Format Specification

Every release file MUST include structured YAML frontmatter followed by optional Markdown body content.

### YAML Frontmatter Schema

```yaml
---
version: "1.0.0"          # Required. Semantic version string (e.g., "1.0.0")
date: "2026-08-12"        # Required. Release date in YYYY-MM-DD format
channel: "stable"         # Required. Release channel: "stable", "beta", or "nightly"
highlights:               # Required. List of top-level bullet highlights (non-empty)
  - "Highlight summary item 1"
  - "Highlight summary item 2"
sections:                 # Required. List of changelog sections
  - type: "added"         # Required. One of: "added", "fixed", "changed", "removed"
    title: "New Features" # Required. Human-readable section header
    items:                # Required. List of change descriptions
      - "Detailed item description without raw issue numbers"
---
```

### Schema Rules

1. **`version`** (`string`): Semantic version identifier matching the tag without the leading `v` (e.g. `"1.0.0"`).
2. **`date`** (`string`): ISO 8601 date string formatted as `YYYY-MM-DD`.
3. **`channel`** (`string`): Distribution channel, such as `"stable"`, `"beta"`, or `"nightly"`.
4. **`highlights`** (`list[string]`): Concise, high-level summary points highlighting the major changes in the release. Must not be empty.
5. **`sections`** (`list[object]`): Grouped change log entries.
   - `type`: Must be one of `added`, `fixed`, `changed`, or `removed`.
   - `title`: Display title for the section (e.g., `New Features`, `Bug Fixes`, `Improvements`).
   - `items`: List of change item strings.
6. **No Raw Issue References**: All issue/PR references must be rephrased as plain text descriptions rather than raw `#123` tokens.

### Body Content

The Markdown body below the frontmatter contains human-readable release notes, narrative summaries, and upgrade guidance.
