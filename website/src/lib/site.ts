/**
 * Site URL helper for JSON-LD and other absolute URL needs.
 *
 * Source of truth: `astro.config.mjs` `site` field, which reads `SITE_URL` env var
 * with a default of `https://mahobrowser.com`.
 *
 * Use this from any `.astro` frontmatter to build canonical absolute URLs.
 *
 * @example
 *   const { siteUrl, absoluteUrl } = getSiteUrl(Astro);
 *   const homepage = siteUrl;                  // "https://mahobrowser.com"
 *   const aboutUrl = absoluteUrl("/about");    // "https://mahobrowser.com/about"
 *
 * If `Astro.site` is undefined (rare; only when site is not configured),
 * this throws to fail loudly at build time rather than emitting broken URLs.
 */
export function getSiteUrl(astro: { site?: URL }): {
  siteUrl: string;
  absoluteUrl: (path: string) => string;
} {
  if (!astro.site) {
    throw new Error(
      'Astro.site is not configured. Set `site` in astro.config.mjs or SITE_URL env var.'
    );
  }
  const siteUrl = astro.site.origin;
  const absoluteUrl = (path: string): string => {
    const cleanPath = path.startsWith('/') ? path : `/${path}`;
    return `${siteUrl}${cleanPath}`;
  };
  return { siteUrl, absoluteUrl };
}
