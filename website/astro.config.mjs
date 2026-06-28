import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightBlog from 'starlight-blog';
import react from '@astrojs/react';
import sitemap from '@astrojs/sitemap';

export default defineConfig({
  site: 'https://maho.app',
  integrations: [
    starlight({
      title: 'Maho',
      logo: { src: './src/assets/logo.svg' },
      components: {
        Head: './src/components/StarlightHead.astro',
      },
      sidebar: [
        { label: 'Maho Browser', autogenerate: { directory: 'browser' } },
        {
          label: 'Maho Mail',
          items: [
            { label: 'GUI Guide', link: '/mail/gui' },
            { label: 'MCP Server', link: '/mail/mcp' },
            { label: 'CLI Reference', autogenerate: { directory: 'cli' } },
            { label: 'API Reference', autogenerate: { directory: 'api' } }
          ]
        },
      ],
      plugins: [
        starlightBlog({
          title: 'Blog',
          prefix: 'blog',
          rss: true,
        }),
      ],
      customCss: ['./src/styles/global.css'],
    }),
    react(),
    sitemap({
      // Per-route priority and changefreq.
      // Order matters — first matching rule wins via early-return in serialize().
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
        if (/\/(pricing|security|privacy|terms|waitlist|why|about|download|compare\/?[a-z]*)\/?$/i.test(url)) {
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
        if (url === 'https://maho.app/' || url === 'https://maho.app') {
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
