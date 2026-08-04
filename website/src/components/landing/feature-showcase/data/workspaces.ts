import type { WorkspaceIndex } from "../spaces-carousel";
import type { MahoTheme } from "../theme/tokens";

export type WorkspaceBadge =
  | { readonly kind: "img"; readonly src: string; readonly label: string; readonly rounded?: boolean }
  | {
      readonly kind: "glyph";
      readonly label: string;
      readonly background: string;
      readonly foreground: string;
      readonly rounded?: boolean;
    };

export type WorkspaceRow =
  | {
      readonly kind: "tab";
      readonly label: string;
      readonly badge: WorkspaceBadge;
      readonly active?: boolean;
      readonly indented?: boolean;
    }
  | {
      readonly kind: "folder";
      readonly label: string;
      readonly open: boolean;
    };

export type WorkspaceDefinition = {
  readonly index: WorkspaceIndex;
  readonly key: "files" | "media" | "research" | "reading";
  readonly label: string;
  readonly address: string;
  readonly themeColor: string;
  readonly theme: MahoTheme;
  readonly pill: string;
  readonly tile: string;
  readonly primaryRows: readonly WorkspaceRow[];
  readonly secondaryRows: readonly WorkspaceRow[];
};

export type FavoriteTileDefinition = {
  readonly label: string;
  readonly badge: WorkspaceBadge;
};

const fav = (name: string): string => `/demo/favicons/${name}.png`;

const img = (src: string, label: string, rounded = false): WorkspaceBadge => ({ kind: "img", src, label, rounded });

type ThemeInput = {
  readonly window: string;
  readonly sidebar: string;
  readonly chrome: string;
  readonly active: string;
  readonly textPrimary: string;
  readonly textSecondary: string;
  readonly textMuted: string;
  readonly accent: string;
  readonly borderStrong: string;
  readonly borderSubtle: string;
  readonly hoverSurface: string;
};

const makeTheme = (input: ThemeInput): MahoTheme => ({
  window: input.window,
  sidebarTop: input.sidebar,
  sidebarBottom: input.sidebar,
  sidebarGradient: `linear-gradient(180deg, ${input.sidebar}, ${input.sidebar})`,
  contentTint: input.chrome,
  chromeSurface: input.chrome,
  activeSurface: input.active,
  hoverSurface: input.hoverSurface,
  borderStrong: input.borderStrong,
  borderSubtle: input.borderSubtle,
  textPrimary: input.textPrimary,
  textSecondary: input.textSecondary,
  textMuted: input.textMuted,
  accent: input.accent,
  focusRing: "rgba(120,120,120,0.55)",
  shadowColor: "rgba(0,0,0,0.35)",
  backdropScrim: "rgba(0,0,0,0.45)",
  backdropBlur: "0px",
});

// Sidebar colors sampled pixel-exact from the recording reference frames.
const FILES_THEME = makeTheme({
  window: "#000000",
  sidebar: "#bcbabd",
  chrome: "#bebbc0",
  active: "#979296",
  textPrimary: "#242326",
  textSecondary: "#3f3e42",
  textMuted: "#6b6a6e",
  accent: "#4b4a4e",
  borderStrong: "rgba(0,0,0,0.12)",
  borderSubtle: "rgba(0,0,0,0.07)",
  hoverSurface: "rgba(0,0,0,0.06)",
});
const MEDIA_THEME = makeTheme({
  window: "#202124",
  sidebar: "#babcc2",
  chrome: "#bebec3",
  active: "#919497",
  textPrimary: "#212226",
  textSecondary: "#3c4046",
  textMuted: "#6a6e77",
  accent: "#4a4d54",
  borderStrong: "rgba(0,0,0,0.12)",
  borderSubtle: "rgba(0,0,0,0.07)",
  hoverSurface: "rgba(0,0,0,0.06)",
});
const RESEARCH_THEME = makeTheme({
  window: "#262626",
  sidebar: "#323e3e",
  chrome: "#3c4848",
  active: "#5b6666",
  textPrimary: "#eef1f1",
  textSecondary: "#ccd0d0",
  textMuted: "#8b9494",
  accent: "#f28c28",
  borderStrong: "rgba(255,255,255,0.13)",
  borderSubtle: "rgba(255,255,255,0.07)",
  hoverSurface: "rgba(255,255,255,0.08)",
});
const READING_THEME = makeTheme({
  window: "#ffffff",
  sidebar: "#3d373b",
  chrome: "#453f43",
  active: "#655f60",
  textPrimary: "#efedef",
  textSecondary: "#d0cdcf",
  textMuted: "#97929a",
  accent: "#c94b53",
  borderStrong: "rgba(255,255,255,0.13)",
  borderSubtle: "rgba(255,255,255,0.07)",
  hoverSurface: "rgba(255,255,255,0.08)",
});

// Pinned favorites row (identical across every space in the recording).
export const FAVORITE_TILES = [
  { label: "Notion", badge: img(fav("notion"), "Notion") },
  { label: "Slack", badge: img(fav("slack"), "Slack") },
  { label: "Figma", badge: img(fav("figma"), "Figma") },
  { label: "Google", badge: img(fav("google"), "Google") },
  { label: "Linear", badge: img(fav("linear"), "Linear") },
  { label: "GitHub", badge: img(fav("github"), "GitHub") },
] as const satisfies readonly FavoriteTileDefinition[];

export const WORKSPACES = [
  {
    index: 0,
    key: "files",
    label: "Files",
    address: "postfiles.pstatic.net",
    themeColor: "#bcbabd",
    theme: FILES_THEME,
    pill: "#b0aeb2",
    tile: "#b0aeb2",
    primaryRows: [
      { kind: "tab", label: "Canva", badge: img(fav("canva"), "Canva") },
      { kind: "tab", label: "Dropbox Paper", badge: img(fav("dropbox"), "Dropbox Paper") },
    ],
    secondaryRows: [
      {
        kind: "tab",
        label: "DSC09635.jpg (2567×3491)",
        badge: { kind: "glyph", label: "IMG", background: "#cfe5ff", foreground: "#245a7a", rounded: true },
        active: true,
      },
      { kind: "tab", label: "The AI workspace that wor...", badge: img(fav("notion"), "Notion") },
      { kind: "tab", label: "Trello", badge: img(fav("trello"), "Trello") },
    ],
  },
  {
    index: 1,
    key: "media",
    label: "Media",
    address: "google.com",
    themeColor: "#babcc2",
    theme: MEDIA_THEME,
    pill: "#b0b0b5",
    tile: "#b0b0b5",
    primaryRows: [
      { kind: "tab", label: "YouTube", badge: img(fav("youtube"), "YouTube") },
      { kind: "tab", label: "Spotify", badge: img(fav("spotify"), "Spotify") },
      { kind: "tab", label: "Netflix", badge: img(fav("netflix"), "Netflix") },
      { kind: "tab", label: "Vimeo", badge: img(fav("vimeo"), "Vimeo") },
    ],
    secondaryRows: [
      { kind: "tab", label: "Google", badge: img(fav("google"), "Google"), active: true },
      { kind: "tab", label: "SoundCloud", badge: img(fav("soundcloud"), "SoundCloud") },
      { kind: "tab", label: "Twitch", badge: img(fav("twitch"), "Twitch") },
    ],
  },
  {
    index: 2,
    key: "research",
    label: "Research",
    address: "wolframalpha.com",
    themeColor: "#323e3e",
    theme: RESEARCH_THEME,
    pill: "#3c4848",
    tile: "#3c4848",
    primaryRows: [
      { kind: "tab", label: "Google", badge: img(fav("google"), "Google") },
      { kind: "tab", label: "Wikipedia", badge: img(fav("wikipedia"), "Wikipedia") },
      { kind: "tab", label: "MDN Web Docs", badge: img(fav("mdn"), "MDN Web Docs") },
      { kind: "tab", label: "NASA", badge: img(fav("nasa"), "NASA") },
    ],
    secondaryRows: [
      { kind: "tab", label: "WolframAlpha", badge: img(fav("wolframalpha"), "WolframAlpha"), active: true },
      { kind: "tab", label: "Stack Overflow", badge: img(fav("stackoverflow"), "Stack Overflow") },
      { kind: "tab", label: "arXiv", badge: img(fav("arxiv"), "arXiv") },
      { kind: "tab", label: "Google Scholar", badge: img(fav("scholar"), "Google Scholar") },
    ],
  },
  {
    index: 3,
    key: "reading",
    label: "Reading",
    address: "aeon.co",
    themeColor: "#3d373b",
    theme: READING_THEME,
    pill: "#453f43",
    tile: "#453f43",
    primaryRows: [
      { kind: "tab", label: "The New York Times", badge: img(fav("nytimes"), "The New York Times") },
      { kind: "tab", label: "The Verge", badge: img(fav("theverge"), "The Verge") },
      { kind: "tab", label: "Medium", badge: img(fav("medium"), "Medium") },
      { kind: "tab", label: "Substack", badge: img(fav("substack"), "Substack") },
    ],
    secondaryRows: [
      { kind: "tab", label: "Hacker News", badge: img(fav("hackernews"), "Hacker News") },
      { kind: "tab", label: "Longreads", badge: img(fav("longreads"), "Longreads") },
      { kind: "tab", label: "Aeon", badge: img(fav("aeon"), "Aeon"), active: true },
      { kind: "tab", label: "The Guardian", badge: img(fav("guardian"), "The Guardian") },
    ],
  },
] as const satisfies readonly [WorkspaceDefinition, WorkspaceDefinition, WorkspaceDefinition, WorkspaceDefinition];
