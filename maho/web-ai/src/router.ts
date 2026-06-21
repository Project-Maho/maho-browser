/**
 * router.ts — Hash-based router for maho-web-ai.
 *
 * Reads window.location.hash and re-exports the useHashRoute hook.
 * All screen navigation is hash-driven: #byok, #chat, #conversations,
 * #offline-model, #privacy, #space-ai-config, #pinch-summary, #agent.
 *
 * No third-party routing library — pure DOM hashchange events.
 *
 * Usage:
 *   import { useHashRoute } from './router';
 *   const route = useHashRoute(); // { name: string; params: Record<string, string> }
 */
export { useHashRoute, type HashRoute } from './hooks/use-hash-route';

/** All known screen names. Add new screens here as they graduate from placeholder. */
export const KNOWN_SCREENS = [
  'byok',
  'chat',
  'conversations',
  'offline-model',
  'privacy',
  'space-ai-config',
  'pinch-summary',
  'agent',
] as const;

export type ScreenName = (typeof KNOWN_SCREENS)[number];
