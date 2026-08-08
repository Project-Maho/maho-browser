import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {MahoAiStore} from '../../store.js';
import {OutputsSection} from '../features/compact/outputs-section.js';
import {
  type AISettingsInfo,
  type ArtifactInfo,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RuntimeEvent,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createRouter(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  return router;
}

function createSession(sessionId: string): SessionInfo {
  return {
    sessionId,
    title: sessionId,
    summary: '',
    createdAt: 1,
    updatedAt: 1,
    adapterName: 'OpenCode',
    isActive: true,
    isReadOnly: false,
    status: SessionStatus.kActive,
    eventCount: 0,
    toolCallCount: 0,
    runtimeSessionId: sessionId,
    lastRuntimeState: 'idle',
  };
}

function createArtifact(artifactId: string, displayName = `${artifactId}.html`): ArtifactInfo {
  return {
    artifactId,
    sessionId: 's1',
    displayName,
    mimeType: 'text/html',
    sizeBytes: 512n,
    createdAt: 1,
  };
}

function outputsButton(container: HTMLElement): HTMLButtonElement {
  return Array.from(container.querySelectorAll('button'))
      .find(b => b.textContent?.startsWith('Outputs')) as HTMLButtonElement;
}

function openArtifactMenu(container: HTMLElement, displayName: string): void {
  const trigger = container.querySelector<HTMLElement>(`[aria-label="Actions for ${displayName}"]`)!;
  act(() => {
    trigger.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      button: 2,
      clientX: 20,
      clientY: 20,
    }));
  });
}

function menuItem(label: string): HTMLElement {
  return Array.from(document.querySelectorAll<HTMLElement>('[role="menuitem"]'))
      .find(item => item.textContent?.trim() === label)!;
}

describe('OutputsSection', () => {
  let handler: PageHandlerRemote;
  let store: MahoAiStore;
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(async () => {
    globalThis.requestAnimationFrame =
        ((cb: FrameRequestCallback) => {
          cb(0);
          return 0;
        }) as typeof requestAnimationFrame;
    handler = new PageHandlerRemote();
    vi.spyOn(handler, 'getSessionHistory').mockResolvedValue({events: [], totalCount: 0});
    handler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
    handler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
    handler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });
    handler.openSettingsPane = vi.fn().mockResolvedValue(undefined);
    store = new MahoAiStore(handler, createRouter());
    await store.ingestAskMahoSessionAccepted('req-1', createSession('s1'));
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
  });

  async function seed(artifacts: ArtifactInfo[]) {
    handler.listArtifacts = vi.fn().mockResolvedValue({artifacts});
    await act(async () => {
      await store.loadArtifacts('s1');
    });
  }

  it('renders the empty state when there are no artifacts', () => {
    act(() => root.render(<OutputsSection store={store} />));
    expect(container.textContent).toContain('No outputs yet');
  });

  it('renders one row per artifact from store state', async () => {
    await seed([createArtifact('a1'), createArtifact('a2')]);
    act(() => root.render(<OutputsSection store={store} />));
    expect(container.textContent).toContain('a1.html');
    expect(container.textContent).toContain('a2.html');
    expect(outputsButton(container).textContent).toBe('Outputs (2)');
  });

  it('collapses and expands', async () => {
    await seed([createArtifact('a1')]);
    act(() => root.render(<OutputsSection store={store} />));
    expect(container.textContent).toContain('a1.html');
    act(() => outputsButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true})));
    expect(container.textContent).not.toContain('a1.html');
    act(() => outputsButton(container).dispatchEvent(new MouseEvent('click', {bubbles: true})));
    expect(container.textContent).toContain('a1.html');
  });

  it('removes a row when delete succeeds', async () => {
    await seed([createArtifact('a1'), createArtifact('a2')]);
    handler.deleteArtifact = vi.fn().mockResolvedValue({success: true});
    act(() => root.render(<OutputsSection store={store} />));
    openArtifactMenu(container, 'a1.html');
    await act(async () => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(container.textContent).not.toContain('a1.html');
    expect(container.textContent).toContain('a2.html');
  });

  it('keeps the row and exposes an accessible error when delete fails', async () => {
    await seed([createArtifact('a1')]);
    handler.deleteArtifact = vi.fn().mockResolvedValue({success: false});
    act(() => root.render(<OutputsSection store={store} />));
    openArtifactMenu(container, 'a1.html');
    await act(async () => {
      menuItem('Delete').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    expect(container.textContent).toContain('a1.html');
    expect(container.querySelector('[role="alert"]')?.textContent).toBe('Delete failed');
  });

  it('surfaces a rename validation error', async () => {
    await seed([createArtifact('a1', 'old.html')]);
    handler.renameArtifact = vi.fn().mockResolvedValue({success: false, error: 'unsafe name'});
    act(() => root.render(<OutputsSection store={store} />));
    openArtifactMenu(container, 'old.html');
    act(() => {
      menuItem('Rename').dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    const input = container.querySelector<HTMLInputElement>('input[aria-label="Artifact name"]')!;
    act(() => {
      const setter = Object.getOwnPropertyDescriptor(
          window.HTMLInputElement.prototype, 'value')!.set!;
      setter.call(input, '../evil');
      input.dispatchEvent(new Event('input', {bubbles: true}));
    });
    await act(async () => {
      input.dispatchEvent(new KeyboardEvent('keydown', {bubbles: true, key: 'Enter'}));
    });
    expect(container.querySelector('[role="alert"]')?.textContent).toBe('unsafe name');
    expect(container.querySelector('input[aria-label="Artifact name"]')).not.toBeNull();
  });
});
