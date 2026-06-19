/**
 * useBridge — React hook that returns a stable MahoBridge implementation
 * backed by the JSON-RPC dispatcher. Components call bridge methods
 * directly without knowing which platform they run on.
 */
import { useMemo } from 'preact/hooks';
import { callBridge } from '../bridge/rpc';
import type {
  ConversationSession,
  ConversationTurn,
  HapticStyle,
  MahoBridge,
  OfflineModelInfo,
  PhotoCapture,
  SpaceAIConfig,
} from '../bridge/types';

function makeBridge(): MahoBridge {
  return {
    // --- BYOK ---
    byokGetProviders: () => callBridge<string[]>('byokGetProviders'),
    byokGetKey: (provider) => callBridge<string | null>('byokGetKey', [provider]),
    byokSetKey: (provider, key) => callBridge<boolean>('byokSetKey', [provider, key]),
    byokDeleteKey: (provider) => callBridge<boolean>('byokDeleteKey', [provider]),
    byokValidateKey: (provider, key) => callBridge<boolean>('byokValidateKey', [provider, key]),

    // --- Chat Session ---
    chatSessionNew: (apiKey, endpoint, model, systemInstruction) =>
      callBridge<number>('chatSessionNew', [apiKey, endpoint, model, systemInstruction]),
    chatSendUserTurn: (sessionPtr, message) =>
      callBridge<boolean>('chatSendUserTurn', [sessionPtr, message]),
    chatSendImage: (sessionPtr, mime, data) =>
      callBridge<boolean>('chatSendImage', [sessionPtr, mime, Array.from(data)]),
    chatSendTextWithImage: (sessionPtr, text, mime, data) =>
      callBridge<boolean>('chatSendTextWithImage', [sessionPtr, text, mime, Array.from(data)]),
    chatCancel: (sessionPtr) => callBridge<void>('chatCancel', [sessionPtr]),
    chatSessionFree: (sessionPtr) => callBridge<void>('chatSessionFree', [sessionPtr]),
    chatSessionPollEvent: (sessionPtr) =>
      callBridge<string | null>('chatSessionPollEvent', [sessionPtr]),

    // --- Conversations ---
    createConversation: (id, title, spaceId, model) =>
      callBridge<boolean>('createConversation', [id, title, spaceId, model]),
    listConversations: (limit = 100) =>
      callBridge<ConversationSession[]>('listConversations', [limit]),
    getConversationMessages: (sessionId) =>
      callBridge<ConversationTurn[]>('getConversationMessages', [sessionId]),
    saveConversationMessage: (sessionId, role, content, urlContext) =>
      callBridge<boolean>('saveConversationMessage', [sessionId, role, content, urlContext]),
    deleteConversation: (sessionId) => callBridge<boolean>('deleteConversation', [sessionId]),
    renameConversation: (sessionId, title) =>
      callBridge<boolean>('renameConversation', [sessionId, title]),

    // --- Space AI Config ---
    getSpaceAIConfig: (spaceId) => callBridge<SpaceAIConfig | null>('getSpaceAIConfig', [spaceId]),
    setSpaceAIConfig: (spaceId, config) =>
      callBridge<boolean>('setSpaceAIConfig', [spaceId, JSON.stringify(config)]),

    // --- Offline Model ---
    offlineModelInfo: () => callBridge<OfflineModelInfo | null>('offlineModelInfo'),
    offlineModelIsDownloaded: () => callBridge<boolean>('offlineModelIsDownloaded'),
    offlineModelDelete: () => callBridge<boolean>('offlineModelDelete'),

    // --- Native-only actions ---
    capturePhoto: () => callBridge<PhotoCapture | null>('capturePhoto'),
    captureScreenshot: () => callBridge<Uint8Array | null>('captureScreenshot'),
    openSettings: () => callBridge<void>('openSettings'),
    hapticFeedback: (style: HapticStyle) => callBridge<void>('hapticFeedback', [style]),
  };
}

export function useBridge(): MahoBridge {
  // Stable reference — recreated only if the module reloads.
  return useMemo(() => makeBridge(), []);
}
