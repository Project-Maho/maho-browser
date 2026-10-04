import type {EssentialSite} from '../../maho_welcome.mojom-webui.js';

export const ESSENTIAL_SITE_ORDER = [
  'obsidian',
  'discord',
  'trello',
  'slack',
  'github',
  'tuta',
  'notion',
  'calendar',
  'figma',
] as const;

export type EssentialIconKey = typeof ESSENTIAL_SITE_ORDER[number];

export function getEssentialIconKey(site: EssentialSite): EssentialIconKey | null {
  if (!site) return null;
  const textToMatch = `${site.name || ''} ${site.url || ''}`.toLowerCase();

  let hostname = '';
  try {
    if (site.url) {
      hostname = new URL(site.url).hostname.replace(/^www\./, '');
    }
  } catch {
  }

  if (hostname === 'obsidian.md' || textToMatch.includes('obsidian')) return 'obsidian';
  if (hostname === 'discord.com' || textToMatch.includes('discord')) return 'discord';
  if (hostname === 'trello.com' || textToMatch.includes('trello')) return 'trello';
  if (hostname === 'slack.com' || textToMatch.includes('slack')) return 'slack';
  if (hostname === 'github.com' || textToMatch.includes('github')) return 'github';
  if (hostname === 'app.tuta.com' || hostname === 'tuta.com' || textToMatch.includes('tuta')) return 'tuta';
  if (hostname === 'notion.com' || hostname === 'notion.so' || textToMatch.includes('notion')) return 'notion';
  if (hostname === 'calendar.google.com' || textToMatch.includes('calendar')) return 'calendar';
  if (hostname === 'figma.com' || textToMatch.includes('figma')) return 'figma';

  return null;
}
