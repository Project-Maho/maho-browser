import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightBlog from 'starlight-blog';
import react from '@astrojs/react';
import sitemap from '@astrojs/sitemap';
import remarkGfm from 'remark-gfm';

// Canonical site URL. Override at build time with SITE_URL env var.
// Used by Astro for canonical URLs, sitemap, OG tags, and JSON-LD via Astro.site.
const SITE_URL = process.env.SITE_URL || 'https://mahobrowser.com';

export default defineConfig({
  site: SITE_URL,
  redirects: {
    '/waitlist': '/download',
  },
  // Starlight overwrites `markdown.remarkPlugins` with its own list, dropping
  // Astro's default GFM. Re-inject remark-gfm here so tables, strikethrough,
  // task lists, and autolinks work in both .md and .mdx docs.
  markdown: {
    remarkPlugins: [remarkGfm],
  },
  integrations: [
    starlight({
      title: 'Maho',
      description: 'Maho is a browser and email client for people who took browsers seriously — sidebar Spaces, split view, a command bar, and optional BYOK AI on maintained Chromium.',
      logo: { src: './src/assets/logo.png' },
      favicon: '/favicon.svg',
      components: {
        Head: './src/components/StarlightHead.astro',
        SiteTitle: './src/components/StarlightSiteTitle.astro',
      },
      sidebar: [
        { label: 'Maho Browser', autogenerate: { directory: 'browser' } },
        {
          label: 'Maho Mail',
          items: [
            { label: 'GUI Guide', link: '/mail/gui' },
            { label: 'MCP Server', link: '/mail/mcp' },
            { label: 'CLI Reference', autogenerate: { directory: 'cli' } },
            {
              label: 'API Reference',
              items: [
                { label: 'Overview', link: '/api/' },
                { label: 'mail_list_accounts', link: '/api/list-accounts/' },
                { label: 'mail_list_folders', link: '/api/list-folders/' },
                { label: 'mail_list_emails', link: '/api/list-emails/' },
                { label: 'mail_get_email', link: '/api/get-email/' },
                { label: 'mail_search_emails', link: '/api/search-emails/' },
                { label: 'mail_list_thread', link: '/api/list-thread/' },
              ],
            },
          ]
        },
        {
          label: 'API',
          items: [
            {
              label: 'Agent Protocol',
              autogenerate: { directory: 'api/agent-protocol' },
            },
          ],
        },
      ],
      plugins: [
        starlightBlog({
          title: 'Blog',
          prefix: 'blog',
          rss: true,
          navigation: 'none',
        }),
      ],
      // ⚠️ marketing.css is intentionally NOT here — it loads via BaseLayout.astro only.
      // Adding it here would leak marketing styles into Starlight docs pages.
      customCss: ['./src/styles/global.css', './src/styles/starlight-theme.css'],
    }),
    react(),
    sitemap({
      filter: (page) => !page.includes('/capture/'),
      // Per-route priority and changefreq.
      // Order matters — first matching rule wins via early-return in serialize().
      // TODO: filter future-dated blog posts from sitemap (requires reading blog frontmatter date at build time)
      serialize(item) {
        const url = item.url;

        // Blog index
        if (/\/blog\/?$/i.test(url)) {
          item.priority = 0.9;
          item.changefreq = 'weekly';
          return item;
        }

        // Tag archive pages (must precede generic blog post matcher)
        if (/\/blog\/tags\//i.test(url)) {
          item.priority = 0.5;
          item.changefreq = 'monthly';
          return item;
        }

        // Author pages and pagination (must precede generic blog post matcher)
        if (/\/blog\/(authors\/|\d+\/?$)/i.test(url)) {
          item.priority = 0.4;
          item.changefreq = 'monthly';
          return item;
        }

        // Pillar/glossary blog posts (high SEO value)
        if (/\/blog\/(what-is-|byok-architecture-|tool-calling|side-panel-|page-context-|building-an-agentic|agentic-browser-permission|sync-architecture|browser-history-encryption|why-sqlite-)/i.test(url)) {
          item.priority = 0.9;
          item.changefreq = 'weekly';
          return item;
        }

        // Comparison posts (commercial intent, secondary SEO)
        if (/\/blog\/maho-vs-/i.test(url) || /\/blog\/agentic-browser-comparison-/i.test(url)) {
          item.priority = 0.8;
          item.changefreq = 'weekly';
          return item;
        }

        // All other blog posts
        if (/\/blog\/[a-z]/i.test(url)) {
          item.priority = 0.7;
          item.changefreq = 'weekly';
          return item;
        }

        // Marketing pages
        if (/\/(pricing|security|privacy|terms|waitlist|why|about|download|roadmap|compare\/?[a-z]*)\/?$/i.test(url)) {
          item.priority = 0.8;
          item.changefreq = 'monthly';
          return item;
        }

        // Browser/mail/cli/api docs
        if (/\/(browser|mail|cli|api)(\/|$)/i.test(url)) {
          item.priority = 0.7;
          item.changefreq = 'monthly';
          return item;
        }

        // Home
        if (url === `${SITE_URL}/` || url === SITE_URL) {
          item.priority = 1.0;
          item.changefreq = 'weekly';
          return item;
        }

        // Default
        item.priority = 0.5;
        item.changefreq = 'monthly';
        return item;
      },
    }),
  ],
});
