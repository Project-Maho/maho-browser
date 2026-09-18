import React, {act} from 'react';
import {createRoot} from 'react-dom/client';
import {expect, it, vi} from 'vitest';
import {DomainPaneContent} from '../domain_panes';
import {ALL_PANE_DEFINITIONS} from '../../schema/panes';
import {AIConnectionState} from '../__tests__/mojo_stub';

Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});

async function setupMultiProvider() {
  let modelsSettings = {
    defaultProviderId: 'openai',
    defaultModelId: 'gpt-4o',
    hasRelaySession: true,
    providers: [
      {
        id: 'openai',
        label: 'OpenAI',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: true,
        lastModelId: 'gpt-4o',
        models: [{id: 'gpt-4o', label: 'GPT-4o'}],
        modelsLoading: false,
      },
      {
        id: 'anthropic',
        label: 'Anthropic',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: false,
        lastModelId: 'stale-nonexistent-model',
        models: [{id: 'claude-3-7-sonnet', label: 'Claude 3.7 Sonnet'}],
        modelsLoading: false,
      }
    ],
    taskRoutes: [
      {
        taskId: 'tab_tidy',
        inheritsDefault: false,
        configuredProviderId: 'openai',
        configuredModelId: 'gpt-4o',
        effectiveProviderId: 'openai',
        effectiveModelId: 'gpt-4o',
        available: true,
      }
    ]
  };

  const handler = {
    getAIModelsSettings: vi.fn(async () => ({settings: modelsSettings})),
    getAISettings: vi.fn(async () => ({settings: {provider: 'openai', model: 'gpt-4o', hasRelaySession: true}})),
    fetchProviderModels: vi.fn().mockResolvedValue({modelIds: [], fromCache: false}),
    setDefaultAIModel: vi.fn(async (providerId: string, modelId: string) => {
      modelsSettings.defaultProviderId = providerId;
      modelsSettings.defaultModelId = modelId;
      return {success: true};
    }),
    disconnectAIProvider: vi.fn(async (_pid: string, _defP?: string, _defM?: string, _repls?: any[]) => {
      return {accepted: false, affectedTasks: ['tab_tidy'], error: 'All affected tasks must be explicitly accounted for in task replacements.'};
    }),
  };

  const snapshot = {settings: []};
  const store = {
    getSnapshot: () => snapshot,
    subscribe: () => () => {},
    getHandler: () => handler,
    getCallbackRouter: () => ({
      providerModelsRefreshed: {addListener: () => 1},
      onAIModelsSettingsChanged: {addListener: () => 1},
      removeListener: vi.fn(),
    }),
    selectPane: vi.fn(),
    refreshAccountStatus: vi.fn(),
  };

  const container = document.createElement('div');
  document.body.append(container);
  const root = createRoot(container);

  await act(async () => {
    root.render(
      <DomainPaneContent
        pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'ai')!}
        settings={[]}
        store={store as any}
      />
    );
  });

  return {
    handler,
    modelsSettings,
    container,
    cleanup() {
      act(() => root.unmount());
      container.remove();
    },
  };
}

it('skips stale lastModelId and selects valid catalog model when activating default', async () => {
  const h = await setupMultiProvider();
  try {
    const radios = h.container.querySelectorAll('input[type="radio"]');
    expect(radios.length).toBeGreaterThan(1);
    await act(async () => {
      (radios[1] as HTMLInputElement).click();
    });
    expect(h.handler.setDefaultAIModel).toHaveBeenCalledWith('anthropic', 'claude-3-7-sonnet');
  } finally {
    h.cleanup();
  }
});

it('rejects malformed disconnect and keeps provider state unchanged', async () => {
  const h = await setupMultiProvider();
  try {
    const openaiRow = h.container.querySelector('[data-provider-row="openai"]');
    expect(openaiRow).not.toBeNull();
    const disconnectBtn = Array.from(openaiRow!.querySelectorAll('button')).find(b => b.textContent?.includes('Disconnect'));
    expect(disconnectBtn).toBeDefined();
    await act(async () => {
      disconnectBtn!.click();
    });

    const allButtons = Array.from(document.querySelectorAll('button'));
    const confirmBtn = allButtons.reverse().find(b => b.textContent?.trim() === 'Disconnect' && b !== disconnectBtn);
    expect(confirmBtn).toBeDefined();
    await act(async () => {
      confirmBtn!.click();
    });

    expect(h.handler.disconnectAIProvider).toHaveBeenCalled();
    expect(h.modelsSettings.defaultProviderId).toBe('openai');
    expect(h.modelsSettings.taskRoutes[0].configuredProviderId).toBe('openai');
  } finally {
    h.cleanup();
  }
});

it('preserves current default model if valid in newly activated provider catalog', async () => {
  let modelsSettings = {
    defaultProviderId: 'openai',
    defaultModelId: 'common-model',
    hasRelaySession: true,
    providers: [
      {
        id: 'openai',
        label: 'OpenAI',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: true,
        lastModelId: 'common-model',
        models: [{id: 'common-model', label: 'Common Model'}],
        modelsLoading: false,
      },
      {
        id: 'custom',
        label: 'Custom',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: false,
        lastModelId: undefined,
        models: [
          {id: 'other-model', label: 'Other Model'},
          {id: 'common-model', label: 'Common Model'},
        ],
        modelsLoading: false,
      }
    ],
    taskRoutes: []
  };

  const handler = {
    getAIModelsSettings: vi.fn(async () => ({settings: modelsSettings})),
    getAISettings: vi.fn(async () => ({settings: {provider: 'openai', model: 'common-model', hasRelaySession: true}})),
    fetchProviderModels: vi.fn().mockResolvedValue({modelIds: [], fromCache: false}),
    setDefaultAIModel: vi.fn(async (providerId: string, modelId: string) => {
      modelsSettings.defaultProviderId = providerId;
      modelsSettings.defaultModelId = modelId;
      return {success: true};
    }),
  };

  const snapshot1 = {settings: []};
  const store = {
    getSnapshot: () => snapshot1,
    subscribe: () => () => {},
    getHandler: () => handler,
    getCallbackRouter: () => ({
      providerModelsRefreshed: {addListener: () => 1},
      onAIModelsSettingsChanged: {addListener: () => 1},
      removeListener: vi.fn(),
    }),
    selectPane: vi.fn(),
    refreshAccountStatus: vi.fn(),
  };

  const container = document.createElement('div');
  document.body.append(container);
  const root = createRoot(container);

  await act(async () => {
    root.render(
      <DomainPaneContent
        pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'ai')!}
        settings={[]}
        store={store as any}
      />
    );
  });

  try {
    const radios = container.querySelectorAll('input[type="radio"]');
    await act(async () => {
      (radios[1] as HTMLInputElement).click();
    });
    expect(handler.setDefaultAIModel).toHaveBeenCalledWith('custom', 'common-model');
  } finally {
    act(() => root.unmount());
    container.remove();
  }
});

it('opens configure modal when activating provider with empty models catalog', async () => {
  let modelsSettings = {
    defaultProviderId: 'openai',
    defaultModelId: 'gpt-4o',
    hasRelaySession: true,
    providers: [
      {
        id: 'openai',
        label: 'OpenAI',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: true,
        lastModelId: 'gpt-4o',
        models: [{id: 'gpt-4o', label: 'GPT-4o'}],
        modelsLoading: false,
      },
      {
        id: 'empty_prov',
        label: 'Empty Provider',
        authLabel: 'API key',
        state: AIConnectionState.kConnected,
        isDefaultProvider: false,
        lastModelId: undefined,
        models: [],
        modelsLoading: false,
      }
    ],
    taskRoutes: []
  };

  const handler = {
    getAIModelsSettings: vi.fn(async () => ({settings: modelsSettings})),
    getAISettings: vi.fn(async () => ({settings: {provider: 'openai', model: 'gpt-4o', hasRelaySession: true}})),
    fetchProviderModels: vi.fn().mockResolvedValue({modelIds: [], fromCache: false}),
    setDefaultAIModel: vi.fn(),
  };

  const snapshot2 = {settings: []};
  const store = {
    getSnapshot: () => snapshot2,
    subscribe: () => () => {},
    getHandler: () => handler,
    getCallbackRouter: () => ({
      providerModelsRefreshed: {addListener: () => 1},
      onAIModelsSettingsChanged: {addListener: () => 1},
      removeListener: vi.fn(),
    }),
    selectPane: vi.fn(),
    refreshAccountStatus: vi.fn(),
  };

  const container = document.createElement('div');
  document.body.append(container);
  const root = createRoot(container);

  await act(async () => {
    root.render(
      <DomainPaneContent
        pane={ALL_PANE_DEFINITIONS.find(p => p.contentKind === 'ai')!}
        settings={[]}
        store={store as any}
      />
    );
  });

  try {
    const radios = container.querySelectorAll('input[type="radio"]');
    await act(async () => {
      (radios[1] as HTMLInputElement).click();
    });
    expect(handler.setDefaultAIModel).not.toHaveBeenCalled();
    const modalHeading = document.body.querySelector('[role="dialog"]');
    expect(modalHeading).not.toBeNull();
  } finally {
    act(() => root.unmount());
    container.remove();
  }
});
