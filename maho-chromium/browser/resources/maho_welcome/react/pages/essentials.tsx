import React from 'react';
import {Check} from 'lucide-react';

import {cn} from '@lib/utils';
import type {EssentialSite} from '../../maho_welcome.mojom-webui.js';
import {
  ESSENTIAL_SITE_ORDER,
  EssentialIconKey,
  getEssentialIconKey,
} from './essentials-icons-helper.js';

export {getEssentialIconKey};

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';

const ESSENTIAL_ICONS: Record<EssentialIconKey, string> = {
  obsidian: `${ASSET_ORIGIN}/icons/essentials/obsidian.png`,
  discord: `${ASSET_ORIGIN}/icons/essentials/discord.png`,
  trello: `${ASSET_ORIGIN}/icons/essentials/trello.png`,
  slack: `${ASSET_ORIGIN}/icons/essentials/slack.png`,
  github: `${ASSET_ORIGIN}/icons/essentials/github.svg`,
  tuta: `${ASSET_ORIGIN}/icons/essentials/tuta.png`,
  notion: `${ASSET_ORIGIN}/icons/essentials/notion.png`,
  calendar: `${ASSET_ORIGIN}/icons/essentials/calendar.png`,
  figma: `${ASSET_ORIGIN}/icons/essentials/figma.png`,
};

interface Props {
  sites: readonly EssentialSite[];
  selected: ReadonlySet<string>;
  onToggle: (url: string) => void;
}

export function EssentialTileIcon({site}: {site: EssentialSite}) {
  const iconKey = getEssentialIconKey(site);
  const localIconUrl = iconKey ? ESSENTIAL_ICONS[iconKey] : null;
  const iconUrl = site.iconPath || localIconUrl;

  if (iconUrl) {
    return (
      <img
        src={iconUrl}
        data-essential={iconKey ?? undefined}
        width={32}
        height={32}
        alt=""
        className="relative z-10 size-8 object-contain"
      />
    );
  }


  return null;
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
    <div className="flex h-full w-full items-center justify-center">
      <style>{`[data-theme="light"] img[data-essential="github"]{filter:invert(1);}`}</style>
      <div className="aspect-square w-[min(420px,80%)] overflow-hidden rounded-2xl border border-border bg-background/50 shadow-2xl shadow-black/20 backdrop-blur-md">
        <div className="flex h-full flex-col gap-5 bg-muted/40 p-5 backdrop-blur-md">
          <div className="flex items-center gap-2" aria-hidden="true">
            <div className="size-3 rounded-full bg-red-400" />
            <div className="size-3 rounded-full bg-yellow-400" />
            <div className="size-3 rounded-full bg-green-400" />
          </div>
          <div className="grid min-h-0 flex-1 grid-cols-3 grid-rows-3 gap-3">
            {orderedSites.map(site => {
              const isSelected = selected.has(site.url);
              return (
                <button
                  key={site.url}
                  type="button"
                  className={cn(
                      'relative flex size-full items-center justify-center rounded-xl border border-border bg-card/70 p-3 text-foreground shadow-sm transition duration-150 hover:-translate-y-0.5 hover:bg-muted/60 hover:shadow-xl focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring',
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
