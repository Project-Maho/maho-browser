/**
 * useHashRoute — reads window.location.hash and returns the current screen
 * name plus any URL search params encoded after the hash fragment.
 *
 * Format: #<screen>[?key=value&key=value]
 * Examples:
 *   #byok
 *   #chat?sessionId=abc&pageUrl=https%3A%2F%2Fexample.com
 *   #space-ai-config?spaceId=xyz
 */
import { useEffect, useState } from 'preact/hooks';

export interface HashRoute {
  name: string;
  params: Record<string, string>;
}

function parseHash(hash: string): HashRoute {
  // Strip leading '#'
  const raw = hash.startsWith('#') ? hash.slice(1) : hash;
  const qIdx = raw.indexOf('?');

  if (qIdx === -1) {
    return { name: raw || 'unknown', params: {} };
  }

  const name = raw.slice(0, qIdx);
  const search = raw.slice(qIdx + 1);
  const params: Record<string, string> = {};

  for (const part of search.split('&')) {
    const eqIdx = part.indexOf('=');
    if (eqIdx === -1) continue;
    const k = decodeURIComponent(part.slice(0, eqIdx));
    const v = decodeURIComponent(part.slice(eqIdx + 1));
    params[k] = v;
  }

  return { name: name || 'unknown', params };
}

export function useHashRoute(): HashRoute {
  const [route, setRoute] = useState<HashRoute>(() => parseHash(window.location.hash));

  useEffect(() => {
    const handler = () => setRoute(parseHash(window.location.hash));
    window.addEventListener('hashchange', handler);
    return () => window.removeEventListener('hashchange', handler);
  }, []);

  return route;
}
