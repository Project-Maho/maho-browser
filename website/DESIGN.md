# Maho Website Design Contract

This document records the design system already implemented by the Maho
marketing site. It is a compatibility contract for future changes, not a
redesign brief.

## Visual language

- The default experience is dark, with an explicit light theme.
- Geist is the primary sans-serif family, Geist Mono is used for labels and
  metadata, and Instrument Serif supplies editorial display headings.
- Marketing surfaces use the semantic color variables declared in
  `src/styles/global.css`; components must not introduce parallel theme
  palettes.
- Content uses the shared `container-marketing`, `pad-x-ladder`, and
  `section-rhythm-*` primitives from `src/styles/marketing.css`.
- Section order, copy hierarchy, and the restrained editorial presentation are
  product behavior. Responsive work must preserve them.

## Responsive contract

- The landing page supports a minimum viewport width of 320 CSS pixels.
- The document must never create horizontal page scrolling. Deliberately
  clipped marquees may overflow only inside their own hidden or scrolling
  container.
- Layouts are single-column by default and may add columns only when their
  content remains readable without shrinking below its intrinsic needs.
- Typography must wrap naturally from 320 through 430 CSS pixels. Desktop-only
  no-wrap rules may start only at their documented breakpoint.
- All interactive controls exposed to a touch-capable viewport must provide a
  hit area of at least 44 by 44 CSS pixels.
- Mobile navigation must lock background scrolling while open, move focus into
  the drawer, close on Escape, and restore focus to its trigger.
- Media must declare intrinsic dimensions or an aspect ratio so responsive
  loading does not shift the surrounding layout.

## Motion and media

- Motion is progressive enhancement. The complete message must remain visible
  when `prefers-reduced-motion: reduce` is active.
- Reduced-motion, Save-Data, and coarse-pointer phone clients receive static
  posters instead of autoplay video.
- Below-the-fold video sources are attached only when their surface approaches
  the viewport.
- Phone clients must remain within the 2 MB initial image and video budget;
  responsive image candidates are required when a poster would exceed it.
- Fine-pointer desktop clients retain the current autoplay presentation when
  motion has not been reduced.

## Regression matrix

Responsive changes are verified at 320, 375, 390, 430, and 768 CSS pixels, plus
a 1280-pixel desktop regression viewport. Verification includes:

- no document-level horizontal overflow;
- no hydration, page, or console errors;
- keyboard and touch navigation behavior;
- 44 by 44 CSS pixel touch targets;
- light and dark themes;
- reduced-motion and Save-Data media behavior;
- initial mobile image and video transfer at or below 2 MB.
