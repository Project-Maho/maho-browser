import React from 'react';
import type {EssentialSite} from '../../maho_welcome.mojom-webui.js';

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
  obsidian: 'chrome://maho-welcome/icons/essentials/obsidian.svg',
  discord: 'chrome://maho-welcome/icons/essentials/discord.svg',
  trello: 'chrome://maho-welcome/icons/essentials/trello.svg',
  slack: 'chrome://maho-welcome/icons/essentials/slack.svg',
  github: 'chrome://maho-welcome/icons/essentials/github.svg',
  tuta: 'chrome://maho-welcome/icons/essentials/tuta.svg',
  notion: 'chrome://maho-welcome/icons/essentials/notion.svg',
  calendar: 'chrome://maho-welcome/icons/essentials/calendar.svg',
  figma: 'chrome://maho-welcome/icons/essentials/figma.svg',
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
      <span className="welcome-essentials-tile__letter">
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
      className="welcome-essentials-tile__icon"
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
    <div className="welcome-essentials-mock">
      <div className="welcome-essentials-mock__container">
        <div className="welcome-essentials-mock__sidebar">
          <div className="welcome-essentials-mock__win-buttons" aria-hidden="true">
            <div></div><div></div><div></div>
          </div>
          <div className="welcome-essentials-mock__grid">
            {orderedSites.map(site => {
              const isSelected = selected.has(site.url);
              return (
                <button
                  key={site.url}
                  type="button"
                  className={`welcome-essentials-tile${isSelected ? ' is-selected' : ''}`}
                  onClick={() => onToggle(site.url)}
                  aria-pressed={isSelected}
                  aria-label={site.name}
                  title={site.name}>
                  <EssentialTileIcon site={site} />
                </button>
              );
            })}
            <div className="welcome-essentials-mock__extra"></div>
            <div className="welcome-essentials-mock__extra"></div>
          </div>
        </div>
      </div>
    </div>
  );
}
