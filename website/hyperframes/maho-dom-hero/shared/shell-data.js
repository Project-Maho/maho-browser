function icon(paths = "", viewBox = "0 0 24 24") {
  return `<svg viewBox="${viewBox}" aria-hidden="true" focusable="false">${paths}</svg>`;
}

const icons = {
  archive: icon('<path d="M4 7.5h16v12H4z"/><path d="M3 4.5h18v3H3z"/><path d="M9 11.5h6"/>'),
  back: icon('<path d="m14.5 6-6 6 6 6"/>'),
  clear: icon('<path d="M12 3v12"/><path d="m7.5 10.5 4.5 4.5 4.5-4.5"/><path d="M5 20h14"/>'),
  code: icon('<path d="m9 7-5 5 5 5"/><path d="m15 7 5 5-5 5"/><path d="m13.5 4-3 16"/>'),
  design: icon('<circle cx="8" cy="8" r="3"/><circle cx="16" cy="8" r="3"/><circle cx="8" cy="16" r="3"/><path d="M13 13h3a3 3 0 1 1-3 3z"/>'),
  forward: icon('<path d="m9.5 6 6 6-6 6"/>'),
  globe: icon('<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a15 15 0 0 1 0 18M12 3a15 15 0 0 0 0 18"/>'),
  grid: icon('<rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><rect x="14" y="14" width="6" height="6" rx="1"/>'),
  inbox: icon('<path d="M4 5h16l-2 14H6z"/><path d="M5 13h4l1.5 2h3L15 13h4"/>'),
  layout: icon('<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>'),
  maho: icon('<path d="M4 18V6l4.5 7L12 6l3.5 7L20 6v12"/>'),
  notes: icon('<path d="M6 3h9l3 3v15H6z"/><path d="M15 3v4h4M9 11h6M9 15h6"/>'),
  plus: icon('<path d="M12 5v14M5 12h14"/>'),
  reload: icon('<path d="M20 11a8 8 0 1 0-2 6"/><path d="M20 5v6h-6"/>'),
  search: icon('<circle cx="10.5" cy="10.5" r="6.5"/><path d="m15.5 15.5 4 4"/>'),
  team: icon('<circle cx="9" cy="9" r="3"/><circle cx="17" cy="8" r="2"/><path d="M4 19c.5-3.2 2.2-5 5-5s4.5 1.8 5 5M14 14c2.9-.4 4.8 1.3 5 4"/>'),
  tidy: icon('<path d="m4 20 10-10"/><path d="m14 4 .8 2.2L17 7l-2.2.8L14 10l-.8-2.2L11 7l2.2-.8z"/><path d="m19 12 .5 1.5L21 14l-1.5.5L19 16l-.5-1.5L17 14l1.5-.5z"/>'),
};

export const SHELL_DATA = {
  address: "research.maho.local",
  toolbarControls: [
    { key: "sidebar", label: "Sidebar", icon: icons.layout },
    { key: "back", label: "Back", icon: icons.back },
    { key: "forward", label: "Forward", icon: icons.forward },
    { key: "reload", label: "Reload", icon: icons.reload },
    { key: "inbox", label: "Archive", icon: icons.inbox },
  ],
  shortcuts: [
    { key: "notes", label: "Notes", tone: "paper", icon: icons.notes },
    { key: "team", label: "Team", tone: "spectrum", icon: icons.team },
    { key: "design", label: "Design", tone: "violet", icon: icons.design },
    { key: "search", label: "Search", tone: "color", icon: icons.search },
    { key: "maho", label: "Maho", tone: "ink", icon: icons.maho },
    { key: "code", label: "Code", tone: "graphite", icon: icons.code },
  ],
  workspaceLabel: "Research",
  primaryTabs: [
    { key: "atlas", label: "Atlas Search", icon: icons.globe },
    { key: "field-notes", label: "Field Notes", icon: icons.notes },
    { key: "reference", label: "Reference Library", icon: icons.grid },
    { key: "studio", label: "Studio Board", icon: icons.design },
  ],
  secondaryTabs: [
    { key: "active", label: "Maho Browser", icon: icons.maho, active: true },
    { key: "reading", label: "Long-form Reading", icon: icons.notes },
    { key: "prototype", label: "Prototype Review", icon: icons.layout },
    { key: "team-room", label: "Team Room", icon: icons.team },
  ],
  tabActions: [
    { key: "new-tab", label: "New Tab", icon: icons.plus },
    { key: "tidy", label: "Tidy", icon: icons.tidy },
    { key: "clear", label: "Clear", icon: icons.clear },
  ],
  footerControls: {
    leading: { key: "archive", label: "Archive", icon: icons.archive },
    trailing: { key: "new-space", label: "New Space", icon: icons.plus },
    dots: ["personal", "research", "media", "work"],
    activeDot: "research",
  },
};
