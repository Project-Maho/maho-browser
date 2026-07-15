import React from 'react';
import {Check} from 'lucide-react';

import {cn} from '@lib/utils';
import type {EssentialSite} from '../../maho_welcome.mojom-webui.js';

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';

const ESSENTIAL_SITE_ORDER = [
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

type EssentialIconKey = typeof ESSENTIAL_SITE_ORDER[number];

const ESSENTIAL_ICONS: Record<EssentialIconKey, string> = {
  obsidian: `${ASSET_ORIGIN}/icons/essentials/obsidian.svg`,
  discord: `${ASSET_ORIGIN}/icons/essentials/discord.svg`,
  trello: `${ASSET_ORIGIN}/icons/essentials/trello.svg`,
  slack: `${ASSET_ORIGIN}/icons/essentials/slack.svg`,
  github: `${ASSET_ORIGIN}/icons/essentials/github.svg`,
  tuta: `${ASSET_ORIGIN}/icons/essentials/tuta.svg`,
  notion: `${ASSET_ORIGIN}/icons/essentials/notion.svg`,
  calendar: `${ASSET_ORIGIN}/icons/essentials/calendar.svg`,
  figma: `${ASSET_ORIGIN}/icons/essentials/figma.svg`,
};

interface Props {
  sites: readonly EssentialSite[];
  selected: ReadonlySet<string>;
  onToggle: (url: string) => void;
}

function getEssentialIconKey(site: EssentialSite): EssentialIconKey | null {
  let hostname = '';
  try {
    hostname = new URL(site.url).hostname.replace(/^www\./, '');
  } catch {
    return null;
  }

  switch (hostname) {
    case 'obsidian.md':
      return 'obsidian';
    case 'discord.com':
      return 'discord';
    case 'trello.com':
      return 'trello';
    case 'slack.com':
      return 'slack';
    case 'github.com':
      return 'github';
    case 'app.tuta.com':
      return 'tuta';
    case 'notion.com':
      return 'notion';
    case 'calendar.google.com':
      return 'calendar';
    case 'figma.com':
      return 'figma';
    default:
      return null;
  }
}

function EssentialTileIcon({site}: {site: EssentialSite}) {
  const [didFail, setDidFail] = React.useState(false);
  const iconKey = getEssentialIconKey(site);
  const iconUrl = !didFail ? (site.iconPath || (iconKey ? ESSENTIAL_ICONS[iconKey] : '')) : '';

  if (!iconUrl) {
    return (
      <span className="relative z-10 text-xl font-semibold text-foreground">
        {site.name.charAt(0)}
      </span>
    );
  }

  return (
    <img
      src={iconUrl}
      width={32}
      height={32}
      alt=""
      className="relative z-10 size-8 object-contain"
      onError={() => setDidFail(true)}
    />
  );
}

export function EssentialsSidebar() {
  return (
    <>
      <h1>Pin Your Essential Apps</h1>
      <p>Choose the apps you use every day. They'll be pinned to your sidebar — always one click away.</p>
      <p>Pinned apps stay visible across every workspace.</p>
    </>
  );
}

export function EssentialsContent({sites, selected, onToggle}: Props) {
  const orderedSites = React.useMemo(() => {
    const sitesByKey = new Map<EssentialIconKey, EssentialSite>();

    for (const site of sites) {
      const iconKey = getEssentialIconKey(site);
      if (iconKey && !sitesByKey.has(iconKey)) {
        sitesByKey.set(iconKey, site);
      }
    }

    return ESSENTIAL_SITE_ORDER.map(key => sitesByKey.get(key)).filter(
        (site): site is EssentialSite => Boolean(site));
  }, [sites]);

  return (
    <div className="flex h-full w-full items-end justify-end">
      <div className="flex h-[80%] w-[70%] min-w-[70%] overflow-hidden rounded-tl-2xl border border-b-0 border-r-0 border-border bg-background/50 pr-[20%] shadow-2xl shadow-black/20 backdrop-blur-md">
        <div className="flex h-full flex-1 flex-col gap-5 rounded-tl-2xl bg-muted/40 p-5 backdrop-blur-md">
          <div className="flex items-center gap-2" aria-hidden="true">
            <div className="size-3 rounded-full bg-red-400" />
            <div className="size-3 rounded-full bg-yellow-400" />
            <div className="size-3 rounded-full bg-green-400" />
          </div>
          <div className="grid grid-cols-3 gap-4">
            {orderedSites.map(site => {
              const isSelected = selected.has(site.url);
              return (
                <button
                  key={site.url}
                  type="button"
                  className={cn(
                      'relative flex min-h-24 min-w-20 items-center justify-center rounded-xl border border-border bg-card/70 p-3 text-foreground shadow-sm transition duration-150 hover:-translate-y-0.5 hover:bg-muted/60 hover:shadow-xl focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring',
                      'after:absolute after:left-1/2 after:top-1/2 after:size-8 after:-translate-x-1/2 after:-translate-y-1/2 after:rounded-md after:bg-primary/15 after:opacity-60 after:content-[""]',
                      isSelected && 'scale-[1.05] border-primary bg-primary/10 shadow-xl ring-2 ring-primary/20 after:opacity-80')}
                  onClick={() => onToggle(site.url)}
                  aria-pressed={isSelected}
                  aria-label={site.name}
                  title={site.name}>
                  {isSelected ? (
                    <span className="absolute right-1.5 top-1.5 z-20 flex size-5 items-center justify-center rounded-full bg-primary text-primary-foreground shadow-lg shadow-primary/20">
                      <Check className="size-3" aria-hidden="true" />
                    </span>
                  ) : null}
                  <EssentialTileIcon site={site} />
                </button>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
}
