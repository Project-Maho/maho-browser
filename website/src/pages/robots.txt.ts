import type { APIRoute } from 'astro';
import { getSiteUrl } from '../lib/site';

export const GET: APIRoute = ({ site }) => {
  const { absoluteUrl } = getSiteUrl({ site });
  const body = `User-agent: *
Allow: /

User-agent: GPTBot
Allow: /

User-agent: ClaudeBot
Allow: /

User-agent: PerplexityBot
Allow: /

User-agent: Google-Extended
Allow: /

User-agent: CCBot
Allow: /

Sitemap: ${absoluteUrl('/sitemap-index.xml')}
`;
  return new Response(body, {
    headers: {
      'Content-Type': 'text/plain; charset=utf-8',
    },
  });
};
