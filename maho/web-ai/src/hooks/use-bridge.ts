/**
 * useBridge — hook that returns a stable MahoBridge implementation
 * backed by the JSON-RPC dispatcher. Components call bridge methods
 * directly without knowing which platform they run on.
 */
import { useMemo } from 'preact/hooks';
import { call } from '../bridge/rpc';
import type {
  HapticStyle,
  MahoBridge,
} from '../bridge/types';

function makeBridge(): MahoBridge {
  return {
    // --- BYOK ---
    byokGetProviders: () => call('byokGetProviders'),
    byokGetKey: (provider) => call('byokGetKey', { provider }),
    byokSetKey: (provider, key) => call('byokSetKey', { provider, key }),
    byokDeleteKey: (provider) => call('byokDeleteKey', { provider }),
    byokValidateKey: (provider, key) => call('byokValidateKey', { provider, key }),

    // --- AI provider settings ---
    getAiSettings: () => call('getAiSettings'),
    setAiProvider: (provider) => call('setAiProvider', { provider }),
    setAiBaseUrl: (url) => call('setAiBaseUrl', { url }),
    setAiApiKey: (key) => call('setAiApiKey', { key }),
    setAiModel: (model) => call('setAiModel', { model }),

    // --- Chat Session ---
    chatSessionStart: (opts) => call('chatSessionStart', opts as unknown as Record<string, unknown>),
    chatSessionResume: (conversationId) => call('chatSessionResume', { handle: conversationId }),
    chatSessionFree: (handle) => call('chatSessionFree', { handle }),
    chatSendMessage: (handle, content) => call('chatSendMessage', { handle, content }),
    chatCancelTurn: (handle) => call('chatCancelTurn', { handle }),
    chatPollEvents: (handle) => call('chatPollEvents', { handle }),
    chatRegisterTool: (handle, tool) => call('chatRegisterTool', { handle, tool }),
    chatSendToolResult: (handle, toolCallId, result) => call('chatSendToolResult', { handle, toolCallId, result }),
    chatAppendAssistantMessage: (handle, content) => call('chatAppendAssistantMessage', { handle, content }),
    chatGetHistory: (handle) => call('chatGetHistory', { handle }),

    // --- Conversations ---
    conversationCreate: (meta) => call('conversationCreate', { meta }),
    conversationList: () => call('conversationList'),
    conversationGet: (id) => call('conversationGet', { id }),
    conversationDelete: (id) => call('conversationDelete', { id }),
    conversationRename: (id, title) => call('conversationRename', { id, title }),
    conversationGetMessages: (id) => call('conversationGetMessages', { id }),
    saveConversationMessage: (sessionId, role, content) =>
      call('saveConversationMessage', { sessionId, role, content }),

    // --- Space AI Config ---
    getSpaceAIConfig: (spaceId) => call('getSpaceAIConfig', { spaceId }),
    setSpaceAIConfig: (spaceId, config) => call('setSpaceAIConfig', { spaceId, config }),

    // --- Pinch ---
    pinchEstimateCost: (pageContent, requestedModel) =>
      call('pinchEstimateCost', { pageContent, model: requestedModel }),

    // --- Agent Session ---
    agentCreateSession: (sessionId) => call('agentCreateSession', { sessionId }),
    agentFreeSession: (handle) => call('agentFreeSession', { handle }),
    agentSendMessage: (handle, message) => call('agentSendMessage', { handle, message }),
    agentCancel: (handle) => call('agentCancel', { handle }),
    agentPollEvent: (handle) => call('agentPollEvent', { handle }),
    agentListTools: (handle) => call('agentListTools', { handle }),

    // --- Native-only actions ---
    capturePhoto: () => call('capturePhoto'),
    captureScreenshot: () => call('captureScreenshot'),
    openSettings: () => call('openSettings'),
    hapticFeedback: (style: HapticStyle) => call('hapticFeedback', { style }),

    // --- Relay auth (gated onboarding) ---
    relaySignIn: (email, password) => call('relaySignIn', { email, password }),
    relaySignUp: (email, password, displayName) => call('relaySignUp', { email, password, displayName }),
    relayAccountStatus: () => call('relayAccountStatus'),
    openDefaultBrowserSettings: () => call('openDefaultBrowserSettings'),
    completeOnboarding: () => call('completeOnboarding'),
  };
}

export function useBridge(): MahoBridge {
  // Stable reference — recreated only if the module reloads.
  return useMemo(() => makeBridge(), []);
}
