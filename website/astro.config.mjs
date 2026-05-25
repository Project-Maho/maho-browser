import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightBlog from 'starlight-blog';
import react from '@astrojs/react';

export default defineConfig({
  site: 'https://maho.app',
  integrations: [
    starlight({
      title: 'Maho',
      logo: { src: './src/assets/logo.svg' },
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
  ],
});
