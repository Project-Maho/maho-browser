# Zen Reference Screenshots

These 8 PNGs are reference captures from Zen Browser's onboarding flow.

## Status
Currently placeholder PNGs. To regenerate from real Zen install:
1. Launch Zen Browser with fresh profile
2. Walk through onboarding to each screen
3. Capture viewport at 1280×720 with system theme = light
4. Save replacing the corresponding placeholder

## Files
- splash.png — Splash screen with title and CTA
- import-stage-a.png — Import screen Stage A (CTA + Skip)
- import-stage-b.png — Import screen Stage B (default browser radios after dialog)
- migration-dialog.png — Migration dialog with browser cards + checkboxes
- search-engine.png — Default search engine grid
- essentials.png — Essentials mock browser preview
- space-theme.png — Theme picker (gradient panel)
- completion.png — Completion + heart pulse

## Diff threshold
Tests pass when diff is < 5% pixels. See `__tests__/welcome_visual_regression.spec.ts`.
