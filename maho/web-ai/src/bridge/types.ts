// Bridge type contracts — shared across all platforms.
// Implementations: rpc.ts (mobile postMessage) and the existing Mojo page handler (desktop).

export interface ConversationSession {
  id: string;
  title: string | null;
  spaceId: string | null;
  model: string | null;
  createdAt: string;
  updatedAt: string;
}

export interface ConversationTurn {
  id: string;
  sessionId: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  urlContext: string | null;
  createdAt: string;
}

export interface SpaceAIConfig {
  spaceId: string;
  model: string | null;
  systemInstruction: string | null;
  enabled: boolean;
}

export interface OfflineModelInfo {
  name: string;
  sizeBytes: number;
  quantization: string;
  downloadedAt: string | null;
}

export interface PhotoCapture {
  mime: string;
  data: Uint8Array;
}

export type HapticStyle = 'light' | 'medium' | 'heavy';

/**
 * Typed bridge surface. Every method returns a Promise — the RPC
 * dispatcher maps each call to the appropriate platform mechanism:
 *   iOS   → window.webkit.messageHandlers.mahoBridge
 *   Android → window.MahoBridgeAndroid.rpc(json)
 *   Desktop → existing Mojo page handler (unchanged)
 */
export interface MahoBridge {
  // --- BYOK ---
  byokGetProviders(): Promise<string[]>;
  byokGetKey(provider: string): Promise<string | null>;
  byokSetKey(provider: string, key: string): Promise<boolean>;
  byokDeleteKey(provider: string): Promise<boolean>;
  byokValidateKey(provider: string, key: string): Promise<boolean>;

  // --- Chat Session ---
  chatSessionNew(
    apiKey: string,
    endpoint: string,
    model: string,
    systemInstruction: string,
  ): Promise<number>;
  chatSendUserTurn(sessionPtr: number, message: string): Promise<boolean>;
  chatSendImage(sessionPtr: number, mime: string, data: Uint8Array): Promise<boolean>;
  chatSendTextWithImage(
    sessionPtr: number,
    text: string,
    mime: string,
    data: Uint8Array,
  ): Promise<boolean>;
  chatCancel(sessionPtr: number): Promise<void>;
  chatSessionFree(sessionPtr: number): Promise<void>;
  chatSessionPollEvent(sessionPtr: number): Promise<string | null>;

  // --- Conversations ---
  createConversation(
    id: string,
    title: string | null,
    spaceId: string | null,
    model: string | null,
  ): Promise<boolean>;
  listConversations(limit?: number): Promise<ConversationSession[]>;
  getConversationMessages(sessionId: string): Promise<ConversationTurn[]>;
  saveConversationMessage(
    sessionId: string,
    role: string,
    content: string,
    urlContext: string | null,
  ): Promise<boolean>;
  deleteConversation(sessionId: string): Promise<boolean>;
  renameConversation(sessionId: string, title: string): Promise<boolean>;

  // --- Space AI Config ---
  getSpaceAIConfig(spaceId: string): Promise<SpaceAIConfig | null>;
  setSpaceAIConfig(spaceId: string, config: SpaceAIConfig): Promise<boolean>;

  // --- Offline Model ---
  offlineModelInfo(): Promise<OfflineModelInfo | null>;
  offlineModelIsDownloaded(): Promise<boolean>;
  offlineModelDelete(): Promise<boolean>;

  // --- Native-only actions (platform provides; Rust not involved) ---
  capturePhoto(): Promise<PhotoCapture | null>;
  captureScreenshot(): Promise<Uint8Array | null>;
  openSettings(): Promise<void>;
  hapticFeedback(style: HapticStyle): Promise<void>;
}
