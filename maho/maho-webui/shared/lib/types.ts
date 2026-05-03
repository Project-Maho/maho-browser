export enum SpaceColor {
  Red = 'red',
  Blue = 'blue',
  Green = 'green',
  Purple = 'purple',
  Orange = 'orange',
  Pink = 'pink',
  Cyan = 'cyan',
  Yellow = 'yellow',
}

export interface Tab {
  id: string;
  title: string;
  url: string;
  faviconUrl: string;
  isActive: boolean;
  isPinned: boolean;
  isAudible: boolean;
  isMuted: boolean;
  spaceId: string;
  lastAccessedAt: number;
}

export interface Space {
  id: string;
  name: string;
  color: SpaceColor;
  icon: string;
  tabIds: string[];
  isActive: boolean;
}

export type SuggestionType = 'tab' | 'history' | 'bookmark' | 'action';

export interface Suggestion {
  id: string;
  type: SuggestionType;
  title: string;
  subtitle: string;
  url: string;
  icon: string;
  score: number;
}

export type ChatRole = 'user' | 'assistant' | 'system';

export interface ChatMessage {
  id: string;
  role: ChatRole;
  content: string;
  timestamp: number;
  isStreaming: boolean;
}

export interface Conversation {
  id: string;
  title: string;
  messages: ChatMessage[];
  createdAt: number;
  updatedAt: number;
}

export interface Note {
  id: string;
  title: string;
  content: string;
  url: string;
  createdAt: number;
  updatedAt: number;
  tags: string[];
}

export type SettingValue = string | number | boolean;

export interface Settings {
  [key: string]: SettingValue;
  defaultBrowser: boolean;
  startPage: string;
  downloadsFolder: string;
  language: string;
  theme: string;
  fontSize: number;
  searchEngine: string;
  sendAnalytics: boolean;
  doNotTrack: boolean;
  blockThirdPartyCookies: boolean;
  aiProvider: string;
  aiEnabled: boolean;
  syncEnabled: boolean;
  syncBookmarks: boolean;
  syncHistory: boolean;
  syncTabs: boolean;
}
