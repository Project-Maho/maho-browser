/**
 * useBridge — hook that returns a stable MahoBridge implementation
 * backed by the JSON-RPC dispatcher. Components call bridge methods
 * directly without knowing which platform they run on.
 */
import { useMemo } from 'preact/hooks';
import { call } from '../bridge/rpc';
import { detectPlatform } from '../bridge/platform-detect';
import type {
  BrowserToolInvokeResult,
  ComposerDraft,
  HapticStyle,
  MahoBridge,
} from '../bridge/types';

function makeBridge(): MahoBridge {
  const browserToolInvoke = detectPlatform() === 'desktop'
    ? undefined
    : (name: string, args: Record<string, unknown>) =>
        call<BrowserToolInvokeResult>('browserToolInvoke', { name, args });

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
    chatSessionResume: (conversationId, opts) => call('chatSessionResume', {
      handle: conversationId,
      ...opts,
    }),
    chatSessionFree: (handle) => call('chatSessionFree', { handle }),
    chatSendMessage: (handle, content) => call('chatSendMessage', { handle, content }),
    chatCancelTurn: (handle) => call('chatCancelTurn', { handle }),
    chatPollEvents: (handle) => call('chatPollEvents', { handle }),
    chatRegisterTool: (handle, tool) => call('chatRegisterTool', { handle, tool }),
    chatSendToolResult: (handle, toolCallId, result, options) => call('chatSendToolResult', {
      handle,
      toolCallId,
      result,
      toolName: options.toolName,
      trigger: options.trigger,
    }),
    ...(browserToolInvoke === undefined ? {} : { browserToolInvoke }),
    chatAppendAssistantMessage: (handle, content, toolCallsJson) => call('chatAppendAssistantMessage', {
      handle,
      content,
      ...(toolCallsJson === undefined ? {} : { toolCallsJson }),
    }),
    chatGetHistory: (handle) => call('chatGetHistory', { handle }),

    // --- Conversations ---
    conversationCreate: (meta) => call('conversationCreate', { meta }),
    conversationList: (options = {}) => call('conversationList', {
      state: options.state ?? 'active',
      limit: options.limit ?? 100,
    }),
    conversationGet: (id) => call('conversationGet', { id }),
    conversationDelete: (id) => call('conversationDelete', { id }),
    conversationRename: (id, title) => call('conversationRename', { id, title }),
    conversationArchive: (id) => call('conversationArchive', { id }),
    conversationUnarchive: (id) => call('conversationUnarchive', { id }),
    conversationBulk: (op, ids) => call('conversationBulk', { op, ids }),
    conversationGetAutoArchivePolicy: async () => {
      const days = await call<number>('conversationGetAutoArchivePolicy');
      return days === -1 ? null : days;
    },
    conversationSetAutoArchivePolicy: (afterDays) =>
      call('conversationSetAutoArchivePolicy', { afterDays }),
    conversationGetMessages: (id) => call('conversationGetMessages', { id }),
    conversationProjectList: () => call('conversationProjectList'),
    conversationProjectCreate: (name) => call('conversationProjectCreate', { name }),
    conversationProjectRename: (id, name) => call('conversationProjectRename', { id, name }),
    conversationProjectDelete: (id) => call('conversationProjectDelete', { id }),
    conversationProjectMove: (ids, projectId) => call('conversationProjectMove', { ids, projectId }),
    saveConversationMessage: (sessionId, role, content) =>
      call('saveConversationMessage', { sessionId, role, content }),

    // --- Composer drafts ---
    composerDraftGet: (scope) => call<ComposerDraft | null>('composerDraftGet', { scope }),
    composerDraftSet: (scope, text) => call<boolean>('composerDraftSet', { scope, text }),
    composerDraftDelete: (scope) => call<boolean>('composerDraftDelete', { scope }),

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
    agentListArtifacts: (handle) => call('agentListArtifacts', { handle }),
    artifactShare: (handle, artifactId) => call('artifactShare', { handle, artifactId }),
    agentResolveInteraction: (handle, requestId, answerJson) =>
      call('agentResolveInteraction', { handle, requestId, answerJson }),
    agentSetRuntimeConfig: (handle, config) => call('agentSetRuntimeConfig', { handle, config }),

    // --- Native-only actions ---
    capturePhoto: () => call('capturePhoto'),
    captureScreenshot: () => call('captureScreenshot'),
    openSettings: () => call('openSettings'),
    hapticFeedback: (style: HapticStyle) => call('hapticFeedback', { style }),

    // --- Relay auth (gated onboarding) ---
    relaySignIn: (email, password) => call('relaySignIn', { email, password }),
    relaySignUp: (email, password, displayName) => call('relaySignUp', { email, password, displayName }),
    relaySignInWithGoogle: () => call('relaySignInWithGoogle'),
    relayAccountStatus: () => call('relayAccountStatus'),
    openDefaultBrowserSettings: () => call('openDefaultBrowserSettings'),
    completeOnboarding: () => call('completeOnboarding'),
  };
}

export function useBridge(): MahoBridge {
  // Stable reference — recreated only if the module reloads.
  return useMemo(() => makeBridge(), []);
}
