# Maho Icon Registry

This directory contains the canonical registry and audit records for Maho's cross-platform icon system across non-WebUI surfaces.

## Registry Schema (`registry.toml`)

The registry defines semantic icon constants and maps them to native asset/symbol identifiers on each platform.

Each icon is defined as a block in `registry.toml`:

```toml
[[icon]]
name = "semantic_name"                     # Canonical lowercase_snake_case identifier
description = "Clear usage description"    # Context for developers and designers
sizes = ["16", "20"]                       # Intended display sizes in density-independent pixels (dp)

  [icon.platforms]
  views   = "vector_icons::kSomeIcon"      # Views C++ icon symbol (upstream or Maho-owned)
  apple   = "some.symbol.name"             # SF Symbols name (shared macOS/iOS or base identifier)
  android = "Icons.Default.SomeIcon"       # Jetpack Compose Material icon or custom vector
```

## Adding a New Icon Workflow

To introduce a new UI icon in Maho:

1. **Register the Icon**: Open `maho/icons/registry.toml` and add a new `[[icon]]` entry with a unique semantic name, description, size expectations, and any known native identifiers.
2. **Implement Platform Constants**:
   * **Apple (macOS & iOS)**: Add the case to `MahoIcon` enum in `MahoIcon.swift` mapped to the semantic name.
   * **Android**: Add the entry to the Kotlin `MahoIcon` enum in `MahoIcon.kt`.
   * **Views C++**: Reference the correct upstream symbol or coordinate the creation of a Maho custom vector icon.
3. **Audit**: Run the audit script `python3 maho/icons/audit_icons.py` to ensure all usages correspond to registered entries.

---

## Proposal: Future Home for Maho-Owned Custom Views Icons

Currently, Maho-defined custom Views icons (e.g. `kArchiveboxIcon`) are defined inline within specific translation units (`maho_sidebar_library_rail_view.cc`). This practice scatters asset definitions and violates clean separation of concerns.

### Proposed Architecture

We propose creating a centralized vector icon target inside `maho-chromium/browser/ui/`:

1. **Vector Asset Directory**: `maho-chromium/browser/ui/vector_icons/`
   * Place `.icon` files (containing path drawing commands) directly inside this folder (e.g., `maho_archivebox.icon`).
2. **GN Integration**:
   * Add a `vector_icons` template invocation inside `maho-chromium/browser/ui/vector_icons/BUILD.gn` that compiles these `.icon` files into C++ classes.
   * This automatically generates a header exposing `vector_icons::kMahoArchiveboxIcon` under a unified `maho` or `vector_icons` namespace.
3. **Usage**:
   * Other views targets can depend on `//maho-chromium/browser/ui/vector_icons` and import the generated header.
   * The inline `kArchiveboxIcon` definition in `maho_sidebar_library_rail_view.cc` can then be safely deleted and replaced with `vector_icons::kMahoArchiveboxIcon`.

*Note: Implementing this proposal requires modifications to `BUILD.gn` and is deferred behind explicit user approval per `AGENTS.md` conventions.*
