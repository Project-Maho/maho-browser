# 6. i18n Architecture

## Status

**SUPERSEDED**, 2026-06-18.

The validation script (`maho/scripts/extract_i18n_keys.py`), WebUI
`_locales/` directory, and website `i18n/` directory that this ADR governed
were deleted as DEAD scaffolding (no `chrome.i18n.getMessage` call in
production WebUI; no Astro page consumes the website i18n collection).
When real translation demand or a routed multi-locale page exists in the
Chromium fork, a fresh ADR will be authored.

The original ADR text is preserved below for audit trail only.

---

## Status

Accepted (revised), 2026-06-17.

Scope was reduced after this ADR's first version: mobile shells (iOS, Android)
and the mail app are out of scope per the workspace direction. The i18n
architecture below covers the **Chromium fork only**: the Chromium WebUI and
the marketing website.

## Context

Maho's Chromium fork ships strings on two surfaces: the Chromium WebUI (custom
`chrome://maho-*` pages and panels) and the marketing website. Each surface
has its own canonical localization format and toolchain. Wave C Tier 1
targets four locales for launch: English (en), Korean (ko), Japanese (ja),
and Simplified Chinese (zh-Hans). A translation vendor is not yet contracted,
but infrastructure must land first so translation content can drop in without
code changes.

The key question is whether to unify localization into a single
source-of-truth format (e.g., XLIFF interchange) or maintain per-platform
native stores with a validation layer that ensures key parity across locales.

## Decision Drivers

- **Platform-native formats**: Chrome i18n expects `_locales/*/messages.json`,
  and Astro/Starlight expects flat JSON. Fighting these formats creates build
  friction.
- **Key parity assurance**: Regardless of format, every locale must have
  every key present (even as a placeholder) to avoid runtime crashes or
  missing-string fallback noise.
- **Translation pipeline readiness**: The vendor pipeline (AI draft → native
  review → merge) needs a clear file drop target per locale, not a single
  monolithic file.
- **Incremental rollout**: Tier 2 (es/pt/fr/de) and Tier 3 (ar/ru/vi) will
  follow. The architecture must not hard-code the number of locales.
- **No GRD dependency**: Chromium upstream uses GRD/GRDP files for its own
  l10n pipeline. Maho's WebUI strings are separate and must not enter the
  upstream GRD flow.

## Considered Options

### Option A. Single XLIFF source with per-platform export

Maintain one XLIFF file per locale as the source of truth. A build step
converts XLIFF → `messages.json` and flat JSON.

**Pros**
- Single canonical source per locale.
- Translation vendors commonly accept XLIFF.

**Cons**
- Requires a custom build step per platform.
- XLIFF round-trip fidelity is lossy for some targets.
- Developers must learn XLIFF editing for local iteration.

### Option B. Per-platform native stores with validation script (Chosen)

Each surface maintains its own locale files in the platform-canonical format.
A validation script (`extract_i18n_keys.py`) scans both surfaces and reports
missing or extra keys per locale. English is the source language; other
locales carry placeholder values until translation lands.

**Pros**
- Zero build-step friction: files are already in the format the platform
  consumes.
- Developers add keys in-place without format conversion.
- Validation script catches drift between locales in CI.
- Translation vendor receives per-platform files directly, no conversion
  needed.
- Incremental: adding a new locale = adding a directory, no schema changes.

**Cons**
- Key names must be manually kept consistent across surfaces (mitigated by
  the validation script).
- No single "open one file, see all translations" view (acceptable for 10
  starter keys; revisit if key count exceeds 200).

### Option C. ICU MessageFormat monorepo with codegen

Use ICU MessageFormat as the canonical format, generate platform files via
codegen.

**Pros**
- Rich plural/gender/select support from day one.

**Cons**
- Heavy toolchain dependency for 10 starter keys.
- Codegen must handle two output formats correctly.
- Overkill for current scale; revisit at Tier 2+ if plural complexity demands
  it.

## Decision Outcome

**Chosen: Option B.**

Each surface maintains its own i18n files in the canonical format:

| Surface | Format | Location |
|---------|--------|----------|
| WebUI | Chrome i18n JSON | `maho-chromium/browser/resources/_locales/{locale}/messages.json` |
| Website | Flat JSON | `website/src/content/i18n/{locale}/common.json` |

English (`en`) is the source language. The three other Tier 1 locales (`ko`,
`ja`, `zh-Hans`) carry English placeholder values with TODO comments until
translation content is provided.

The validation script `maho/scripts/extract_i18n_keys.py` runs in CI and
enforces:

1. Every key present in the source (`en`) is also present in all target
   locales.
2. No extra keys exist in target locales that are absent from the source.

Translation pipeline (when vendor is contracted):

1. AI drafts initial translations from English source strings.
2. Native speakers review and approve each string.
3. Approved strings replace placeholder values via PR.
4. CI runs `extract_i18n_keys.py` to confirm key parity before merge.

### Positive Consequences

- Infrastructure can land immediately without waiting for translation vendor.
- Each surface team can add keys independently without cross-surface
  coordination (validation catches drift).
- No custom build steps; standard platform toolchains work unmodified.
- Clear file-drop targets for the translation vendor.

### Negative Consequences

- Manual key naming consistency across two files per string addition (10
  keys is trivial; monitor at scale).
- No automatic plural/gender handling (acceptable for Tier 1 common strings;
  revisit for Tier 2 content-heavy strings).

## Tier Roadmap

| Tier | Locales | Timeline |
|------|---------|----------|
| 1 | en, ko, ja, zh-Hans | Wave C (this ADR) |
| 2 | es, pt, fr, de | Post-launch, vendor-dependent |
| 3 | ar, ru, vi | Market-dependent, RTL support required for ar |

## References

- `maho/scripts/extract_i18n_keys.py` — validation script (WebUI + website
  scope only).
- `maho-chromium/browser/resources/_locales/{en,ko,ja,zh_CN}/messages.json`
  — WebUI locale files.
- `website/src/content/i18n/{en,ko,ja,zh-Hans}/common.json` — Website locale
  files.

## Out-of-Scope (post-revision)

The first version of this ADR also covered iOS shell `Localizable.strings`
and Android `values{,-suffix}/strings.xml`. Those surfaces were dropped from
the workspace's active scope; their locale files have been removed. If
mobile shells are reintroduced, this ADR will be re-opened to add them back.

---

*Revised 2026-06-17 — Chromium-fork-only scope; iOS/Android dropped.*
