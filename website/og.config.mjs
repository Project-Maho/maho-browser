// Single, site-wide brand OG card. Produces public/og/default.png,
// used as the default og:image for every page (see BaseLayout.astro).
export const ogConfigs = [
  {
    slug: 'default',
    brand: 'Maho',
    tagline: 'The browser tools you liked. Still shipping.',
    accent: 'browser',
  },
];

export const ogSize = { width: 1200, height: 630 };
