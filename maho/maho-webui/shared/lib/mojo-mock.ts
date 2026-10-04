import type {Tab, Space, ChatMessage, Conversation, Note, Settings, SettingValue} from './types.js';
import {SpaceColor} from './types.js';

export interface MahoSidebarPageHandler {
  switchTab(id: string): Promise<void>;
  closeTab(id: string): Promise<void>;
  getTabs(): Promise<Tab[]>;
  getSpaces(): Promise<Space[]>;
  switchSpace(id: string): Promise<void>;
}

export interface MahoAIPageHandler {
  sendMessage(text: string): Promise<ChatMessage>;
  getConversation(id: string): Promise<Conversation>;
  getConversations(): Promise<Conversation[]>;
  streamResponse(callback: (chunk: string) => void): Promise<void>;
}

export interface MahoSettingsPageHandler {
  getSettings(): Promise<Settings>;
  updateSetting(key: string, value: SettingValue): Promise<void>;
}

export interface MahoNotesPageHandler {
  getNotes(): Promise<Note[]>;
  createNote(data: Partial<Note>): Promise<Note>;
  updateNote(id: string, data: Partial<Note>): Promise<Note>;
  deleteNote(id: string): Promise<void>;
}

export interface MahoBoostPageHandler {
  getStats(): Promise<{ adsBlocked: number; trackersBlocked: number; httpsUpgrades: number; bandwidthSaved: string }>;
  getSettings(): Promise<{ adBlocking: boolean; trackerBlocking: boolean; httpsUpgrade: boolean; fingerprintProtection: boolean }>;
  toggleSetting(key: string, value: boolean): Promise<void>;
}

export interface MahoEaselPageHandler {
  getCanvases(): Promise<Array<{ id: string; name: string; createdAt: string; thumbnail: string }>>;
  createCanvas(name: string): Promise<{ id: string }>;
  deleteCanvas(id: string): Promise<void>;
}

export interface MahoSpacesPageHandler {
  getSpaces(): Promise<Space[]>;
  createSpace(name: string, color: string, icon: string): Promise<Space>;
  deleteSpace(id: string): Promise<void>;
  updateSpace(id: string, updates: Partial<Space>): Promise<Space>;
}

const MOCK_TABS: Tab[] = [
  {id: 't1', title: 'GitHub - maho-browser/maho', url: 'https://github.com/maho-browser/maho', faviconUrl: '', isActive: true, isPinned: true, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now()},
  {id: 't2', title: 'Chromium Code Search', url: 'https://source.chromium.org/', faviconUrl: '', isActive: false, isPinned: true, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now() - 60000},
  {id: 't3', title: 'Lit — Simple. Fast. Web Components.', url: 'https://lit.dev/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now() - 120000},
  {id: 't4', title: 'MDN Web Docs', url: 'https://developer.mozilla.org/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now() - 180000},
  {id: 't5', title: 'TypeScript Documentation', url: 'https://www.typescriptlang.org/docs/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now() - 240000},
  {id: 't6', title: 'YouTube - lofi hip hop radio', url: 'https://www.youtube.com/watch?v=jfKfPfyJRdk', faviconUrl: '', isActive: false, isPinned: false, isAudible: true, isMuted: false, spaceId: 's2', lastAccessedAt: Date.now() - 300000},
  {id: 't7', title: 'Reddit - r/programming', url: 'https://www.reddit.com/r/programming/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's2', lastAccessedAt: Date.now() - 360000},
  {id: 't8', title: 'Twitter / X', url: 'https://x.com/home', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's2', lastAccessedAt: Date.now() - 420000},
  {id: 't9', title: 'Hacker News', url: 'https://news.ycombinator.com/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's2', lastAccessedAt: Date.now() - 480000},
  {id: 't10', title: 'Notion - Project Planning', url: 'https://www.notion.so/project-planning', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's2', lastAccessedAt: Date.now() - 540000},
  {id: 't11', title: 'Amazon.com Shopping Cart', url: 'https://www.amazon.com/gp/cart', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's3', lastAccessedAt: Date.now() - 600000},
  {id: 't12', title: 'Google Scholar - WebUI research', url: 'https://scholar.google.com/', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's3', lastAccessedAt: Date.now() - 660000},
  {id: 't13', title: 'Stack Overflow - Lit element questions', url: 'https://stackoverflow.com/questions/tagged/lit', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's3', lastAccessedAt: Date.now() - 720000},
  {id: 't14', title: 'Wikipedia - Chromium (web browser)', url: 'https://en.wikipedia.org/wiki/Chromium_(web_browser)', faviconUrl: '', isActive: false, isPinned: false, isAudible: false, isMuted: false, spaceId: 's3', lastAccessedAt: Date.now() - 780000},
  {id: 't15', title: 'Gmail - Inbox', url: 'https://mail.google.com/mail/u/0/#inbox', faviconUrl: '', isActive: false, isPinned: true, isAudible: false, isMuted: false, spaceId: 's1', lastAccessedAt: Date.now() - 840000},
];

const MOCK_SPACES: Space[] = [
  {id: 's1', name: 'Work', color: SpaceColor.Blue, icon: '💼', tabIds: ['t1', 't2', 't3', 't4', 't5', 't15'], isActive: true},
  {id: 's2', name: 'Personal', color: SpaceColor.Green, icon: '🏠', tabIds: ['t6', 't7', 't8', 't9', 't10'], isActive: false},
  {id: 's3', name: 'Research', color: SpaceColor.Purple, icon: '🔬', tabIds: ['t11', 't12', 't13', 't14'], isActive: false},
];

const MOCK_CONVERSATIONS: Conversation[] = [
  {
    id: 'c1', title: 'Help with CSS Grid layout', createdAt: Date.now() - 3600000, updatedAt: Date.now() - 3000000,
    messages: [
      {id: 'm1', role: 'user', content: 'How do I create a responsive grid layout with CSS Grid?', timestamp: Date.now() - 3600000, isStreaming: false},
      {id: 'm2', role: 'assistant', content: 'You can use `display: grid` with `grid-template-columns: repeat(auto-fill, minmax(280px, 1fr))` to create a responsive grid that automatically adjusts the number of columns based on available space.\n\n```css\n.grid {\n  display: grid;\n  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));\n  gap: 16px;\n}\n```\n\nThis will create columns that are at least 280px wide and grow to fill available space.', timestamp: Date.now() - 3500000, isStreaming: false},
      {id: 'm3', role: 'user', content: 'Can I make some items span multiple columns?', timestamp: Date.now() - 3400000, isStreaming: false},
      {id: 'm4', role: 'assistant', content: 'Yes! Use `grid-column: span 2` on the items you want to span multiple columns. You might also want to add a media query to handle cases where there aren\'t enough columns to span.', timestamp: Date.now() - 3300000, isStreaming: false},
    ],
  },
  {
    id: 'c2', title: 'TypeScript decorators', createdAt: Date.now() - 7200000, updatedAt: Date.now() - 7000000,
    messages: [
      {id: 'm5', role: 'user', content: 'What are TypeScript decorators and when should I use them?', timestamp: Date.now() - 7200000, isStreaming: false},
      {id: 'm6', role: 'assistant', content: 'TypeScript decorators are special declarations that can modify classes, methods, properties, and parameters. They\'re commonly used in frameworks like Angular and Lit.\n\nIn **Lit**, decorators like `@customElement` and `@property` make web component development cleaner:\n\n```typescript\n@customElement(\'my-element\')\nclass MyElement extends LitElement {\n  @property({type: String})\n  name = \'World\';\n}\n```', timestamp: Date.now() - 7100000, isStreaming: false},
      {id: 'm7', role: 'user', content: 'Are they stable in TypeScript 5?', timestamp: Date.now() - 7000000, isStreaming: false},
    ],
  },
];

const MOCK_SETTINGS: Settings = {
  defaultBrowser: true,
  startPage: 'chrome://maho-home',
  downloadsFolder: '~/Downloads',
  language: 'en-US',
  theme: 'system',
  fontSize: 14,
  searchEngine: 'Google',
  sendAnalytics: false,
  doNotTrack: true,
  blockThirdPartyCookies: true,
  aiProvider: 'claude',
  aiEnabled: true,
  syncEnabled: true,
  syncBookmarks: true,
  syncHistory: true,
  syncTabs: false,
};

function delay(ms: number = 50): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function filterByQuery<T extends {title: string}>(items: T[], query: string): T[] {
  if (!query) return items;
  const q = query.toLowerCase();
  return items.filter(item => item.title.toLowerCase().includes(q));
}

const mockSidebarHandler: MahoSidebarPageHandler = {
  async switchTab(_id: string) { await delay(); },
  async closeTab(_id: string) { await delay(); },
  async getTabs() { await delay(); return MOCK_TABS; },
  async getSpaces() { await delay(); return MOCK_SPACES; },
  async switchSpace(_id: string) { await delay(); },
};

const mockAIHandler: MahoAIPageHandler = {
  async sendMessage(text: string) {
    await delay(200);
    return {id: `m_${Date.now()}`, role: 'assistant' as const, content: `I received your message: "${text}". This is a mock response.`, timestamp: Date.now(), isStreaming: false};
  },
  async getConversation(id: string) { await delay(); return MOCK_CONVERSATIONS.find(c => c.id === id) ?? MOCK_CONVERSATIONS[0]; },
  async getConversations() { await delay(); return MOCK_CONVERSATIONS; },
  async streamResponse(callback: (chunk: string) => void) {
    const words = 'This is a streamed mock response from the AI assistant.'.split(' ');
    for (const word of words) {
      await delay(100);
      callback(word + ' ');
    }
  },
};

const mockSettingsHandler: MahoSettingsPageHandler = {
  async getSettings() { await delay(); return {...MOCK_SETTINGS}; },
  async updateSetting(_key: string, _value: SettingValue) { await delay(); },
};

const MOCK_NOTES: Note[] = [
  {id: 'n1', title: 'CSS Grid Layout Notes', content: 'Key concepts: grid-template-columns, grid-template-rows, gap, auto-fill vs auto-fit. Use minmax() for responsive columns.', url: 'https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_grid_layout', createdAt: Date.now() - 86400000, updatedAt: Date.now() - 3600000, tags: ['css', 'layout']},
  {id: 'n2', title: 'Lit Element Lifecycle', content: 'connectedCallback → firstUpdated → updated. Use requestUpdate() for manual re-render. Properties trigger updates automatically.', url: 'https://lit.dev/docs/components/lifecycle/', createdAt: Date.now() - 172800000, updatedAt: Date.now() - 7200000, tags: ['lit', 'webcomponents']},
  {id: 'n3', title: 'Chromium WebUI Architecture', content: 'WebUI pages live in chrome/browser/resources/. Mojo IPC connects JS to C++ browser process. CrLitElement is the standard base class.', url: 'https://chromium.org/developers/design-documents/', createdAt: Date.now() - 259200000, updatedAt: Date.now() - 86400000, tags: ['chromium', 'architecture']},
  {id: 'n4', title: 'Meeting Notes - Q4 Planning', content: 'Focus areas: sidebar polish, command palette shipping, AI integration prototype. Launch target: end of quarter.', url: '', createdAt: Date.now() - 345600000, updatedAt: Date.now() - 172800000, tags: ['meeting', 'planning']},
];

const mockNotesHandler: MahoNotesPageHandler = {
  async getNotes() { await delay(); return [...MOCK_NOTES]; },
  async createNote(data: Partial<Note>) {
    await delay();
    return {id: `n_${Date.now()}`, title: data.title ?? 'Untitled', content: data.content ?? '', url: data.url ?? '', createdAt: Date.now(), updatedAt: Date.now(), tags: data.tags ?? []};
  },
  async updateNote(id: string, data: Partial<Note>) {
    await delay();
    const note = MOCK_NOTES.find(n => n.id === id) ?? MOCK_NOTES[0];
    return {...note, ...data, updatedAt: Date.now()};
  },
  async deleteNote(_id: string) { await delay(); },
};

const mockBoostHandler: MahoBoostPageHandler = {
  async getStats() {
    await delay();
    return {adsBlocked: 14832, trackersBlocked: 8291, httpsUpgrades: 3417, bandwidthSaved: '247 MB'};
  },
  async getSettings() {
    await delay();
    return {adBlocking: true, trackerBlocking: true, httpsUpgrade: true, fingerprintProtection: false};
  },
  async toggleSetting(_key: string, _value: boolean) { await delay(); },
};

const mockEaselHandler: MahoEaselPageHandler = {
  async getCanvases() { await delay(); return []; },
  async createCanvas(_name: string) { await delay(); return {id: `canvas_${Date.now()}`}; },
  async deleteCanvas(_id: string) { await delay(); },
};

const mockSpacesHandler: MahoSpacesPageHandler = {
  async getSpaces() { await delay(); return [...MOCK_SPACES, {id: 's4', name: 'Shopping', color: SpaceColor.Orange, icon: '🛒', tabIds: [], isActive: false}]; },
  async createSpace(name: string, color: string, icon: string) {
    await delay();
    return {id: `s_${Date.now()}`, name, color: color as SpaceColor, icon, tabIds: [], isActive: false};
  },
  async deleteSpace(_id: string) { await delay(); },
  async updateSpace(id: string, updates: Partial<Space>) {
    await delay();
    const space = MOCK_SPACES.find(s => s.id === id) ?? MOCK_SPACES[0];
    return {...space, ...updates};
  },
};

const handlers: Record<string, unknown> = {
  'MahoSidebarPageHandler': mockSidebarHandler,
  'MahoAIPageHandler': mockAIHandler,
  'MahoSettingsPageHandler': mockSettingsHandler,
  'MahoNotesPageHandler': mockNotesHandler,
  'MahoBoostPageHandler': mockBoostHandler,
  'MahoEaselPageHandler': mockEaselHandler,
  'MahoSpacesPageHandler': mockSpacesHandler,
};

export function getMojoHandler<T>(name: string): T {
  const handler = handlers[name];
  if (!handler) {
    throw new Error(`Unknown Mojo handler: ${name}`);
  }
  return handler as T;
}
