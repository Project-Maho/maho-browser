import type { APIRoute } from 'astro';
import { getSiteUrl } from '../lib/site';

export const GET: APIRoute = ({ site }) => {
  const { siteUrl, absoluteUrl } = getSiteUrl({ site });
  const body = `User-agent: *
Allow: /

Sitemap: ${absoluteUrl('/sitemap-index.xml')}
`;
  return new Response(body, {
    headers: {
      'Content-Type': 'text/plain; charset=utf-8',
    },
  });
};
