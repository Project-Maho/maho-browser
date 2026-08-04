import {describe, expect, it} from 'vitest';
import {getEssentialIconKey} from '../pages/essentials-icons-helper.js';
import type {EssentialSite} from '../../maho_welcome.mojom-webui.js';

describe('getEssentialIconKey resolution logic', () => {
  it('resolves obsidian from hostname or URL/name', () => {
    const siteByUrl: EssentialSite = {
      name: 'Obsidian App',
      url: 'https://obsidian.md',
      iconPath: '',
    };
    expect(getEssentialIconKey(siteByUrl)).toBe('obsidian');

    const siteByName: EssentialSite = {
      name: 'Obsidian Notes',
      url: 'https://custom-domain.org',
      iconPath: '',
    };
    expect(getEssentialIconKey(siteByName)).toBe('obsidian');
  });

  it('resolves known apps: Discord, Trello, Slack, GitHub, Tuta, Notion, Calendar, Figma', () => {
    const knownSites: Array<[string, string, string]> = [
      ['Discord', 'https://discord.com', 'discord'],
      ['Trello Workspaces', 'https://trello.com', 'trello'],
      ['Slack Web', 'https://slack.com', 'slack'],
      ['GitHub Dashboard', 'https://github.com', 'github'],
      ['Tuta Mail', 'https://app.tuta.com', 'tuta'],
      ['Notion Workspace', 'https://notion.so', 'notion'],
      ['Google Calendar', 'https://calendar.google.com', 'calendar'],
      ['Figma Design', 'https://figma.com', 'figma'],
    ];

    for (const [name, url, expectedKey] of knownSites) {
      const site: EssentialSite = {name, url, iconPath: ''};
      expect(getEssentialIconKey(site)).toBe(expectedKey);
    }
  });

  it('returns null for unknown generic sites', () => {
    const unknownSite: EssentialSite = {
      name: 'My Custom Internal App',
      url: 'https://internal.company.net',
      iconPath: '',
    };
    expect(getEssentialIconKey(unknownSite)).toBeNull();
  });
});
