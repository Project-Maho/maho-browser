import {describe, it, expect, vi, beforeEach} from 'vitest';
import {MahoAiStore} from '../../store.js';
import {
  type AISettingsInfo,
  type ArtifactInfo,
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  type RuntimeEvent,
  RuntimeEventKind,
  type SessionInfo,
  SessionStatus,
} from '../../maho_ai.mojom-webui.js';

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createPageCallbackRouterFake(): PageCallbackRouter {
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

function createArtifact(
    sessionId: string, artifactId: string, displayName = `${artifactId}.html`): ArtifactInfo {
  return {
    artifactId,
    sessionId,
    displayName,
    mimeType: 'text/html',
    sizeBytes: 128n,
    createdAt: 1,
  };
}

function createArtifactEvent(
    sessionId: string, artifact: ArtifactInfo, sequence: number): RuntimeEvent {
  return {
    sessionId,
    kind: RuntimeEventKind.kArtifactCreated,
    sequence,
    timestamp: sequence,
    artifact,
  };
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(next => {
    resolve = next;
  });
  return {promise, resolve};
}

describe('MahoAiStore — artifact state and event indexing', () => {
  let mockHandler: PageHandlerRemote;
  let mockRouter: PageCallbackRouter;
  let store: MahoAiStore;

  beforeEach(() => {
    mockHandler = new PageHandlerRemote();
    mockRouter = createPageCallbackRouterFake();
    vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
      events: [],
      totalCount: 0,
    });
    mockHandler.getAiProfiles = vi.fn().mockResolvedValue({profilesJson: '[]'});
    mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({workspacesJson: '[]'});
    mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });
    mockHandler.openSettingsPane = vi.fn().mockResolvedValue(undefined);
    store = new MahoAiStore(mockHandler, mockRouter);
  });

  function runtimeListener(): Callback<[RuntimeEvent]> {
    const listener = vi.mocked(mockRouter.onRuntimeEvent.addListener).mock.calls[0]?.[0];
    expect(listener).toBeTypeOf('function');
    return listener as Callback<[RuntimeEvent]>;
  }

  it('indexes a kArtifactCreated event into artifactsBySessionId', async () => {
    const session = createSession('s1');
    await store.ingestAskMahoSessionAccepted('req-1', session);
    const artifact = createArtifact('s1', 'a1');

    runtimeListener()(createArtifactEvent('s1', artifact, 1));

    expect(store.getSnapshot().artifactsBySessionId['s1']).toEqual([artifact]);
  });

  it('de-dupes a replayed artifact by artifactId', async () => {
    const session = createSession('s1');
    await store.ingestAskMahoSessionAccepted('req-1', session);
    const artifact = createArtifact('s1', 'a1');
    const renamed = createArtifact('s1', 'a1', 'renamed.html');

    const fire = runtimeListener();
    fire(createArtifactEvent('s1', artifact, 1));
    fire(createArtifactEvent('s1', renamed, 2));

    const list = store.getSnapshot().artifactsBySessionId['s1'];
    expect(list).toHaveLength(1);
    expect(list?.[0].displayName).toBe('renamed.html');
  });

  it('does not crash on an artifact event for an unknown session', async () => {
    const session = createSession('known');
    await store.ingestAskMahoSessionAccepted('req-1', session);
    const fire = runtimeListener();
    expect(() => fire(createArtifactEvent('unknown', createArtifact('unknown', 'x'), 1)))
        .not.toThrow();
  });

  it('loadArtifacts populates state from listArtifacts', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1'), createArtifact('s1', 'a2')],
    });
    await store.loadArtifacts('s1');
    expect(store.getSnapshot().artifactsBySessionId['s1']).toHaveLength(2);
    expect(store.getSnapshot().artifactsLoadingBySessionId['s1']).toBe(false);
  });

  it('renameArtifact success updates state and returns null', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1', 'old.html')],
    });
    await store.loadArtifacts('s1');
    mockHandler.renameArtifact = vi.fn().mockResolvedValue({success: true, error: null});

    const error = await store.renameArtifact('s1', 'a1', 'new.html');

    expect(error).toBeNull();
    expect(store.getSnapshot().artifactsBySessionId['s1']?.[0].displayName).toBe('new.html');
  });

  it('renameArtifact failure returns the error and leaves state unchanged', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1', 'old.html')],
    });
    await store.loadArtifacts('s1');
    mockHandler.renameArtifact =
        vi.fn().mockResolvedValue({success: false, error: 'unsafe name'});

    const error = await store.renameArtifact('s1', 'a1', '../x');

    expect(error).toBe('unsafe name');
    expect(store.getSnapshot().artifactsBySessionId['s1']?.[0].displayName).toBe('old.html');
  });

  it('deleteArtifact removes the artifact and its inline conversation event', async () => {
    const session = createSession('s1');
    await store.ingestAskMahoSessionAccepted('req-1', session);
    const first = createArtifact('s1', 'a1');
    const second = createArtifact('s1', 'a2');
    const fire = runtimeListener();
    fire(createArtifactEvent('s1', first, 1));
    fire(createArtifactEvent('s1', second, 2));
    mockHandler.deleteArtifact = vi.fn().mockResolvedValue({success: true});

    const ok = await store.deleteArtifact('s1', 'a1');

    expect(ok).toBe(true);
    expect(store.getSnapshot().artifactsBySessionId['s1']?.map(a => a.artifactId)).toEqual(['a2']);
    expect(store.getSnapshot().eventsBySessionId['s1']
        ?.map(entry => entry.event.artifact?.artifactId)
        .filter(Boolean)).toEqual(['a2']);
  });

  it('coalesces Delete/Delete and rejects Rename/preview while deletion is pending', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1', 'old.html')],
    });
    await store.loadArtifacts('s1');
    const deletion = deferred<{success: boolean}>();
    mockHandler.deleteArtifact = vi.fn(() => deletion.promise);
    mockHandler.renameArtifact = vi.fn().mockResolvedValue({success: true, error: null});
    mockHandler.getArtifactPreviewUrl = vi.fn().mockResolvedValue({url: 'preview-url'});

    const firstDelete = store.deleteArtifact('s1', 'a1');
    const secondDelete = store.deleteArtifact('s1', 'a1');
    const rename = store.renameArtifact('s1', 'a1', 'new.html');
    const preview = store.getArtifactPreviewUrl('a1');

    expect(mockHandler.deleteArtifact).toHaveBeenCalledTimes(1);
    expect(await rename).toBe('Artifact is being deleted');
    expect(await preview).toBeNull();
    expect(mockHandler.renameArtifact).not.toHaveBeenCalled();
    expect(mockHandler.getArtifactPreviewUrl).not.toHaveBeenCalled();

    deletion.resolve({success: true});
    expect(await firstDelete).toBe(true);
    expect(await secondDelete).toBe(true);
  });

  it('discards stale rename and preview results after successful deletion', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1', 'old.html')],
    });
    await store.loadArtifacts('s1');
    const renameResult = deferred<{success: boolean; error: string|null}>();
    const previewResult = deferred<{url: string|null}>();
    mockHandler.renameArtifact = vi.fn(() => renameResult.promise);
    mockHandler.getArtifactPreviewUrl = vi.fn(() => previewResult.promise);
    mockHandler.deleteArtifact = vi.fn().mockResolvedValue({success: true});

    const rename = store.renameArtifact('s1', 'a1', 'new.html');
    const preview = store.getArtifactPreviewUrl('a1');
    expect(await store.deleteArtifact('s1', 'a1')).toBe(true);
    renameResult.resolve({success: true, error: null});
    previewResult.resolve({url: 'stale-preview-url'});

    expect(await rename).toBe('Artifact no longer exists');
    expect(await preview).toBeNull();
    expect(store.getSnapshot().artifactsBySessionId['s1']).toEqual([]);
    expect(await store.getArtifactPreviewUrl('a1')).toBeNull();
    expect(mockHandler.getArtifactPreviewUrl).toHaveBeenCalledTimes(1);
  });

  it('does not resurrect a deleted artifact from an older list result or runtime event', async () => {
    const session = createSession('s1');
    await store.ingestAskMahoSessionAccepted('req-1', session);
    const item = createArtifact('s1', 'a1');
    runtimeListener()(createArtifactEvent('s1', item, 1));
    const staleList = deferred<{artifacts: ArtifactInfo[]}>();
    mockHandler.listArtifacts = vi.fn(() => staleList.promise);
    mockHandler.deleteArtifact = vi.fn().mockResolvedValue({success: true});

    const load = store.loadArtifacts('s1');
    expect(await store.deleteArtifact('s1', 'a1')).toBe(true);
    staleList.resolve({artifacts: [item]});
    await load;
    runtimeListener()(createArtifactEvent('s1', item, 2));

    expect(store.getSnapshot().artifactsBySessionId.s1).toEqual([]);
    expect(store.getSnapshot().eventsBySessionId.s1).toEqual([]);

    mockHandler.getSessionHistory = vi.fn().mockResolvedValue({
      events: [createArtifactEvent('s1', item, 3)],
      totalCount: 1,
    });
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({artifacts: [item]});
    await store.loadHistory('s1');
    await Promise.resolve();
    expect(store.getSnapshot().eventsBySessionId.s1).toEqual([]);
    expect(store.getSnapshot().artifactsBySessionId.s1).toEqual([]);
  });

  it('keeps artifact state when deletion returns false or throws', async () => {
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1')],
    });
    await store.loadArtifacts('s1');
    mockHandler.deleteArtifact = vi.fn().mockResolvedValueOnce({success: false})
                                      .mockRejectedValueOnce(new Error('offline'));

    expect(await store.deleteArtifact('s1', 'a1')).toBe(false);
    expect(store.getSnapshot().artifactsBySessionId['s1']).toHaveLength(1);
    expect(await store.deleteArtifact('s1', 'a1')).toBe(false);
    expect(store.getSnapshot().artifactsBySessionId['s1']).toHaveLength(1);
  });

  it('caches preview URLs and refreshes them on list load', async () => {
    const previewSpy = vi.fn().mockResolvedValue({
      url: 'chrome-untrusted://maho-ai-artifact-preview/cap',
    });
    mockHandler.getArtifactPreviewUrl = previewSpy;
    mockHandler.listArtifacts = vi.fn().mockResolvedValue({
      artifacts: [createArtifact('s1', 'a1')],
    });

    await store.loadArtifacts('s1');
    const first = await store.getArtifactPreviewUrl('a1');
    const second = await store.getArtifactPreviewUrl('a1');
    expect(first).toBe('chrome-untrusted://maho-ai-artifact-preview/cap');
    expect(second).toBe(first);
    expect(previewSpy).toHaveBeenCalledTimes(1);

    await store.loadArtifacts('s1');
    await store.getArtifactPreviewUrl('a1');
    expect(previewSpy).toHaveBeenCalledTimes(2);
  });
});
